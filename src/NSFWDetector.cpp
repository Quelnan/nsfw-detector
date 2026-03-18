#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <set>

NSFWDetector* NSFWDetector::s_inst = nullptr;

NSFWDetector* NSFWDetector::get() {
    if (!s_inst) {
        s_inst = new NSFWDetector();
        s_inst->m_sens = Mod::get()->getSettingValue<int64_t>("sensitivity");
    }
    return s_inst;
}

cocos2d::ccColor3B NSFWDetector::colorForPercent(float p) {
    if (p < 15.f) return {0, 255, 80};
    if (p < 40.f) return {255, 220, 0};
    if (p < 65.f) return {255, 140, 0};
    return {255, 60, 60};
}

static const std::string B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<unsigned char> b64decode(std::string const& in) {
    std::vector<unsigned char> out;
    int T[256]; std::fill_n(T, 256, -1);
    for (int i = 0; i < 64; i++) T[(int)B64[i]] = i;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back((val >> bits) & 0xFF); bits -= 8; }
    }
    return out;
}

std::string NSFWDetector::decodeLevelData(GJGameLevel* level) {
    if (!level) return "";
    std::string raw = level->m_levelString;
    if (raw.empty()) return "";

    // Already decompressed?
    if (raw.size() > 10) {
        auto check = raw.substr(0, std::min((size_t)2000, raw.size()));
        if (check.find(';') != std::string::npos) return raw;
    }
    if (raw.rfind("kS38", 0) == 0 || raw.rfind("kA", 0) == 0) return raw;

    std::string b64 = raw;
    for (char& c : b64) { if (c=='-') c='+'; else if (c=='_') c='/'; }
    auto decoded = b64decode(b64);
    if (decoded.empty()) return "";
    try {
        unsigned char* inflated = nullptr;
        auto len = ZipUtils::ccInflateMemory(decoded.data(), (unsigned int)decoded.size(), &inflated);
        if (inflated && len > 0) { std::string r((char*)inflated, len); free(inflated); return r; }
        if (inflated) free(inflated);
    } catch (...) {}
    std::string s(decoded.begin(), decoded.end());
    if (s.find(';') != std::string::npos) return s;
    return "";
}

std::vector<LevelObject> NSFWDetector::parseObjects(std::string const& data) {
    std::vector<LevelObject> objects;
    std::istringstream stream(data);
    std::string objStr;
    bool header = true;
    while (std::getline(stream, objStr, ';')) {
        if (header) { header = false; continue; }
        if (objStr.empty()) continue;
        LevelObject obj;
        std::istringstream os(objStr);
        std::string tok;
        std::vector<std::string> toks;
        while (std::getline(os, tok, ',')) toks.push_back(tok);
        for (size_t i = 0; i + 1 < toks.size(); i += 2) {
            int key = 0;
            try { key = std::stoi(toks[i]); } catch (...) { continue; }
            auto const& v = toks[i+1];
            try { switch (key) {
                case 1: obj.id = std::stoi(v); break;
                case 2: obj.x = std::stof(v); break;
                case 3: obj.y = std::stof(v); break;
                case 10: obj.duration = std::stof(v); break;
                case 21: obj.mainColor = std::stoi(v); break;
                case 22: obj.detailColor = std::stoi(v); break;
                case 23: obj.targetColor = std::stoi(v); break;
                case 24: obj.zLayer = std::stoi(v); break;
                case 25: obj.zOrder = std::stoi(v); break;
                case 28: obj.moveX = std::stof(v); break;
                case 29: obj.moveY = std::stof(v); break;
                case 32: obj.scale = std::stof(v); break;
                case 35: obj.opacity = std::stof(v); break;
                case 51: obj.targetGroup = std::stoi(v); break;
                case 57: {
                    std::istringstream gs(v); std::string g;
                    while (std::getline(gs, g, '.'))
                        if (!g.empty()) try { obj.groups.push_back(std::stoi(g)); } catch (...) {}
                    break;
                }
                case 71: obj.staticGroup = std::stoi(v); break;
            }} catch (...) {}
        }
        if (obj.id > 0) objects.push_back(obj);
    }
    return objects;
}

static bool isDecoObject(int id) {
    if (id >= 580 && id <= 590) return true;
    if (id >= 394 && id <= 400) return true;
    if (id >= 289 && id <= 310) return true;
    if (id >= 325 && id <= 340) return true;
    if (id == 917) return true;
    if (id >= 501 && id <= 520) return true;
    if (id >= 1700 && id <= 1800) return true;
    if (id >= 725 && id <= 740) return true;
    if (id >= 1764 && id <= 1830) return true;
    if (id >= 173 && id <= 190) return true;
    if (id >= 207 && id <= 215) return true;
    if (id >= 467 && id <= 480) return true;
    return false;
}

static bool isTrigger(int id) {
    return id==899||id==901||id==1006||id==1007||id==1049||id==1268||
           id==1346||id==1347||id==1520||id==1585||id==1595||id==1611||
           id==1811||id==1815||id==1817||id==1914||id==1916||id==2062||id==3600;
}

ScanResult NSFWDetector::analyze(std::vector<LevelObject> const& objects) {
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    ScanResult result;
    result.objectsScanned = (int)objects.size();
    if (objects.empty()) { result.error = "No objects"; return result; }

    float sens = m_sens / 50.f;

    constexpr float OFF_HIGH = 500.f;
    constexpr float OFF_LOW = -800.f;

    // Classify objects
    std::vector<LevelObject const*> offscreenDeco;
    std::vector<LevelObject const*> normalDeco;
    std::vector<LevelObject const*> triggers;
    std::vector<LevelObject const*> offscreenAll;

    for (auto const& o : objects) {
        bool offscreen = o.y > OFF_HIGH || o.y < OFF_LOW;
        if (isTrigger(o.id)) {
            triggers.push_back(&o);
        } else if (isDecoObject(o.id)) {
            if (offscreen) offscreenDeco.push_back(&o);
            else normalDeco.push_back(&o);
        }
        if (offscreen && !isTrigger(o.id)) {
            offscreenAll.push_back(&o);
        }
    }

    auto mk = [&](std::string nm, float p, std::vector<std::string> r) {
        CategoryScore c; c.name = nm;
        c.percent = std::clamp(p * sens, 0.f, 100.f);
        c.color = colorForPercent(c.percent);
        c.reasons = std::move(r);
        result.totalPercent += c.percent;
        result.categories.push_back(c);
    };

    // ===== 1. OFF-SCREEN ART =====
    {
        float pct = 0;
        std::vector<std::string> reasons;
        int decoOff = (int)offscreenDeco.size();

        if (decoOff > 30) {
            float minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;
            for (auto* o : offscreenDeco) {
                minX=std::min(minX,o->x); maxX=std::max(maxX,o->x);
                minY=std::min(minY,o->y); maxY=std::max(maxY,o->y);
            }
            float w=maxX-minX, h=maxY-minY;
            float area=std::max(w*h,1.f);
            float density=decoOff/(area/10000.f);

            if (w > 100 && h > 100 && density > 1.5f) {
                pct += 40;
                reasons.push_back(fmt::format("{} deco objects in dense off-screen cluster ({:.0f}x{:.0f})", decoOff, w, h));
            } else if (decoOff > 80) {
                pct += 30;
                reasons.push_back(fmt::format("{} deco objects off-screen", decoOff));
            } else {
                pct += 15;
                reasons.push_back(fmt::format("{} deco objects off-screen", decoOff));
            }
        }
        mk("Off-Screen Art", pct, reasons);
    }

    // ===== 2. HIDDEN COLORING =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Stacked color triggers
        struct PosKey { int x,y; bool operator==(PosKey const& o) const { return x==o.x&&y==o.y; } };
        struct PosHash { size_t operator()(PosKey const& k) const { return std::hash<int>()(k.x)^(std::hash<int>()(k.y)<<16); } };
        std::unordered_map<PosKey, int, PosHash> colorStacks;

        for (auto* t : triggers) {
            if (t->id == 899 || t->id == 1006) {
                PosKey pk{(int)(t->x/3.f),(int)(t->y/3.f)};
                colorStacks[pk]++;
            }
        }

        int heavyStacks = 0, maxStack = 0;
        for (auto& [pos,count] : colorStacks) {
            maxStack = std::max(maxStack, count);
            if (count >= 8) heavyStacks++;
        }

        if (heavyStacks > 0) {
            pct += std::min(50.f, heavyStacks * 10.f);
            reasons.push_back(fmt::format("{} positions with 8+ stacked color triggers (max: {})", heavyStacks, maxStack));
        }

        // Exclusive off-screen channels
        std::unordered_set<int> offCh, normCh;
        for (auto* o : offscreenDeco) {
            if (o->mainColor>0) offCh.insert(o->mainColor);
            if (o->detailColor>0) offCh.insert(o->detailColor);
        }
        for (auto* o : normalDeco) {
            if (o->mainColor>0) normCh.insert(o->mainColor);
            if (o->detailColor>0) normCh.insert(o->detailColor);
        }

        int exclusiveCount = 0;
        for (int ch : offCh) if (!normCh.count(ch)) exclusiveCount++;

        int susColorTrigs = 0;
        for (auto* t : triggers) {
            if ((t->id==899||t->id==1006) && offCh.count(t->targetColor) && !normCh.count(t->targetColor))
                susColorTrigs++;
        }

        if (exclusiveCount > 3) {
            pct += 25;
            reasons.push_back(fmt::format("{} color channels used only by off-screen objects", exclusiveCount));
        }
        if (susColorTrigs > 5) {
            pct += 15;
            reasons.push_back(fmt::format("{} color triggers target off-screen-only channels", susColorTrigs));
        }

        mk("Hidden Coloring", pct, reasons);
    }

    // ===== 3. LAG MACHINE =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_map<int,int> bins;
        for (auto* t : triggers) bins[(int)(t->x/60.f)]++;

        int denseBins = 0;
        for (auto& [b,c] : bins) if (c >= 80) denseBins++;

        if (denseBins > 0) {
            pct += std::min(60.f, denseBins * 25.f);
            reasons.push_back(fmt::format("{} zones with 80+ triggers packed together", denseBins));
        }

        // Spawn loops
        std::unordered_map<int, std::vector<int>> spawnMap;
        for (auto* t : triggers) {
            if (t->id == 1268) {
                for (int g : t->groups) spawnMap[g].push_back(t->targetGroup);
            }
        }
        bool foundLoop = false;
        for (auto& [a, targets] : spawnMap) {
            for (int b : targets) {
                if (spawnMap.count(b)) {
                    for (int c : spawnMap[b]) {
                        if (c == a) {
                            pct += 40;
                            reasons.push_back(fmt::format("Spawn loop: group {} <-> {}", a, b));
                            foundLoop = true; break;
                        }
                    }
                }
                if (foundLoop) break;
            }
            if (foundLoop) break;
        }

        mk("Lag Machine", pct, reasons);
    }

    // ===== 4. CAMERA TRICKS =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Static camera triggers (1914) - check if group objects are far away
        for (auto* t : triggers) {
            if (t->id == 1914 && t->staticGroup > 0) {
                // Find objects in the static trigger's group
                for (auto const& o : objects) {
                    if (isTrigger(o.id)) continue;
                    for (int g : o.groups) {
                        if (g == t->staticGroup && o.y > 500.f) {
                            pct += 25;
                            reasons.push_back(fmt::format("Static cam group {} points to object at Y={:.0f}", t->staticGroup, o.y));
                            goto camStaticDone;
                        }
                    }
                }
            }
        }
        camStaticDone:

        // Camera offset/static with extreme Y
        for (auto* t : triggers) {
            if (t->id == 1916 || t->id == 2062) {
                if (std::abs(t->moveY) > 500.f) {
                    float destY = (t->id == 1916) ? t->moveY : t->y + t->moveY;
                    int nearbyOff = 0;
                    for (auto* o : offscreenAll) {
                        if (std::abs(o->y - destY) < 1000.f) nearbyOff++;
                    }
                    if (nearbyOff > 15) {
                        pct += 30;
                        reasons.push_back(fmt::format("Camera jumps to Y={:.0f} where {} objects exist", destY, nearbyOff));
                        break;
                    } else if (std::abs(t->moveY) > 1200.f) {
                        pct += 15;
                        reasons.push_back(fmt::format("Camera extreme offset Y={:.0f}", t->moveY));
                    }
                }
            }

            if (t->id == 901) {
                float dist = std::sqrt(t->moveX*t->moveX + t->moveY*t->moveY);
                if (dist > 3000.f) {
                    pct += 10;
                    reasons.push_back(fmt::format("Move teleport: {:.0f} units", dist));
                }
            }
        }

        mk("Camera Tricks", std::min(pct, 80.f), reasons);
    }

    // ===== 5. TOGGLE REVEALS =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_map<int,int> grpOffCount;
        for (auto* o : offscreenDeco) {
            for (int g : o->groups) grpOffCount[g]++;
        }

        for (auto* t : triggers) {
            if (t->id == 1049) {
                int tgt = t->targetGroup;
                if (grpOffCount.count(tgt) && grpOffCount[tgt] > 15) {
                    pct += 40;
                    reasons.push_back(fmt::format("Toggle group {} controls {} off-screen deco", tgt, grpOffCount[tgt]));
                    break;
                }
            }
        }

        mk("Toggle Reveals", pct, reasons);
    }

    // ===== 6. ALPHA FLASHING =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_set<int> offDecoChannels;
        for (auto* o : offscreenDeco) {
            if (o->mainColor>0) offDecoChannels.insert(o->mainColor);
            if (o->detailColor>0) offDecoChannels.insert(o->detailColor);
        }

        std::unordered_map<int,int> alphaOnOffCh;
        for (auto* t : triggers) {
            if (t->id == 1007 && offDecoChannels.count(t->targetColor))
                alphaOnOffCh[t->targetColor]++;
        }

        for (auto& [ch,count] : alphaOnOffCh) {
            if (count >= 3) {
                pct += 40;
                reasons.push_back(fmt::format("{} alpha triggers on off-screen channel {}", count, ch));
                break;
            }
        }

        mk("Alpha Flashing", pct, reasons);
    }

    // ===== 7. TRIGGER DENSITY =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_map<int,int> bins;
        for (auto* t : triggers) bins[(int)(t->x/60.f)]++;

        int denseZones = 0;
        for (auto& [b,c] : bins) if (c >= 10) denseZones++;

        if (denseZones > 0) {
            pct += std::min(20.f, denseZones * 5.f);
            reasons.push_back(fmt::format("{} zones with 10+ triggers in 60 units", denseZones));
        }

        mk("Trigger Density", pct, reasons);
    }

    result.scanTimeMs = std::chrono::duration<float,std::milli>(clk::now()-t0).count();
    return result;
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    ScanResult fail;
    if (!level) { fail.error = "No level"; return fail; }
    log::info("Scanning '{}' (ID:{})", level->m_levelName, level->m_levelID.value());
    auto data = decodeLevelData(level);
    if (data.empty()) { fail.error = "Could not load level data.\nMake sure it's downloaded."; return fail; }

    // Save decoded data for Python analysis
    try {
        auto path = Mod::get()->getSaveDir() / fmt::format("level_{}.txt", level->m_levelID.value());
        std::ofstream file(path.string());
        if (file.is_open()) { file << data; file.close(); }
    } catch (...) {}

    auto objs = parseObjects(data);
    if (objs.empty()) { fail.error = "Parsed 0 objects."; return fail; }
    log::info("Parsed {} objects", objs.size());
    return analyze(objs);
}
