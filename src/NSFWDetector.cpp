#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
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
    if (raw.size() > 10 && raw.find(';') != std::string::npos && raw.find(',') < 50)
        return raw;
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
                case 28: obj.moveX = std::stof(v); break;
                case 29: obj.moveY = std::stof(v); break;
                case 35: obj.opacity = std::stof(v); break;
                case 51: obj.targetGroup = std::stoi(v); break;
                case 57: {
                    std::istringstream gs(v); std::string g;
                    while (std::getline(gs, g, '.'))
                        if (!g.empty()) try { obj.groups.push_back(std::stoi(g)); } catch (...) {}
                    break;
                }
            }} catch (...) {}
        }
        if (obj.id > 0) objects.push_back(obj);
    }
    return objects;
}

// Decoration/art object IDs - lines, pixels, slopes, glow, etc.
// These are what NSFW artists use to draw. Gameplay blocks are NOT art.
static bool isDecoObject(int id) {
    // Lines and outlines
    if (id >= 580 && id <= 590) return true;   // thin lines
    if (id >= 394 && id <= 400) return true;   // outlines
    // Slopes and wedges (used for shapes)
    if (id >= 289 && id <= 310) return true;   // slopes
    if (id >= 325 && id <= 340) return true;   // more slopes
    // Pixel-like small blocks
    if (id == 917) return true;                // pixel
    // Glow objects
    if (id >= 501 && id <= 520) return true;   // glow
    if (id >= 1700 && id <= 1800) return true; // glow v2
    // Circle/dot decorations
    if (id >= 725 && id <= 740) return true;
    // Custom deco objects
    if (id >= 1764 && id <= 1830) return true;
    // Half blocks / quarter blocks (used in art)
    if (id >= 173 && id <= 190) return true;
    // Various small deco
    if (id >= 207 && id <= 215) return true;
    // Arrow/triangle deco
    if (id >= 467 && id <= 480) return true;
    return false;
}

static bool isTrigger(int id) {
    return id==899||id==901||id==1006||id==1007||id==1049||id==1268||
           id==1346||id==1347||id==1520||id==1585||id==1595||id==1611||
           id==1811||id==1815||id==1817||id==1916||id==2062||id==3600||
           id==2899||id==2903||id==2067||id==3016||id==3660;
}

ScanResult NSFWDetector::analyze(std::vector<LevelObject> const& objects) {
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    ScanResult result;
    result.objectsScanned = (int)objects.size();
    if (objects.empty()) { result.error = "No objects"; return result; }

    float sens = m_sens / 50.f;

    constexpr float OFF_HIGH = 1500.f;
    constexpr float OFF_LOW = -800.f;

    // Separate objects by type and location
    std::vector<LevelObject const*> offscreenDeco;   // deco objects off-screen
    std::vector<LevelObject const*> normalDeco;      // deco objects in play area
    std::vector<LevelObject const*> triggers;
    std::vector<LevelObject const*> offscreenAny;    // any object off-screen

    for (auto const& o : objects) {
        bool offscreen = o.y > OFF_HIGH || o.y < OFF_LOW;
        if (isTrigger(o.id)) {
            triggers.push_back(&o);
        } else if (isDecoObject(o.id)) {
            if (offscreen) offscreenDeco.push_back(&o);
            else normalDeco.push_back(&o);
        }
        if (offscreen && !isTrigger(o.id)) {
            offscreenAny.push_back(&o);
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

    // ===== 1. OFF-SCREEN ART (deco objects only) =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        int decoOff = (int)offscreenDeco.size();

        if (decoOff > 30) {
            // Check cluster density
            float minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;
            for (auto* o : offscreenDeco) {
                minX=std::min(minX,o->x); maxX=std::max(maxX,o->x);
                minY=std::min(minY,o->y); maxY=std::max(maxY,o->y);
            }
            float w = maxX-minX, h = maxY-minY;
            float area = std::max(w*h, 1.f);
            float density = decoOff / (area / 10000.f);

            if (w > 100 && h > 100 && density > 1.5f) {
                pct += 50;
                reasons.push_back(fmt::format("{} deco objects in dense off-screen cluster ({:.0f}x{:.0f})", decoOff, w, h));
            } else if (decoOff > 80) {
                pct += 30;
                reasons.push_back(fmt::format("{} deco objects off-screen", decoOff));
            }
        }

        mk("Off-Screen Art", pct, reasons);
    }

    // ===== 2. HIDDEN COLORING (stacked color triggers) =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Detect color triggers stacked at the same position
        // This is the "don't color when making, color with triggers" technique
        struct PosKey {
            int x, y;
            bool operator==(PosKey const& o) const { return x==o.x && y==o.y; }
        };
        struct PosHash {
            size_t operator()(PosKey const& k) const {
                return std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 16);
            }
        };

        std::unordered_map<PosKey, int, PosHash> colorTriggerStacks;
        int totalColorTriggers = 0;

        for (auto* t : triggers) {
            if (t->id == 899 || t->id == 1006) { // color or pulse trigger
                PosKey pk{(int)(t->x / 3.f), (int)(t->y / 3.f)}; // quantize to ~3 units
                colorTriggerStacks[pk]++;
                totalColorTriggers++;
            }
        }

        // Count positions with many stacked color triggers
        int heavyStacks = 0;
        int maxStack = 0;
        for (auto& [pos, count] : colorTriggerStacks) {
            maxStack = std::max(maxStack, count);
            if (count >= 8) heavyStacks++; // 8+ color triggers at same spot
        }

        if (heavyStacks > 0) {
            pct += std::min(60.f, heavyStacks * 12.f);
            reasons.push_back(fmt::format("{} positions with 8+ stacked color triggers (max: {})", heavyStacks, maxStack));
        }

        // Also check: color triggers targeting channels used ONLY by off-screen objects
        std::unordered_set<int> offCh, normCh;
        for (auto* o : offscreenDeco) {
            if (o->mainColor > 0) offCh.insert(o->mainColor);
            if (o->detailColor > 0) offCh.insert(o->detailColor);
        }
        for (auto* o : normalDeco) {
            if (o->mainColor > 0) normCh.insert(o->mainColor);
            if (o->detailColor > 0) normCh.insert(o->detailColor);
        }

        int exclusiveCount = 0;
        for (int ch : offCh) {
            if (!normCh.count(ch)) exclusiveCount++;
        }

        int susColorTrigs = 0;
        for (auto* t : triggers) {
            if ((t->id == 899 || t->id == 1006) && offCh.count(t->targetColor) && !normCh.count(t->targetColor))
                susColorTrigs++;
        }

        if (susColorTrigs > 5 && exclusiveCount > 3) {
            pct += 30;
            reasons.push_back(fmt::format("{} color triggers target {} off-screen-only channels", susColorTrigs, exclusiveCount));
        }

        mk("Hidden Coloring", pct, reasons);
    }

    // ===== 3. LAG MACHINE =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_map<int, int> bins;
        for (auto* t : triggers) bins[(int)(t->x / 60.f)]++;

        int extremeBins = 0;
        for (auto& [b, c] : bins) if (c >= 80) extremeBins++;

        if (extremeBins > 0) {
            pct += std::min(60.f, extremeBins * 25.f);
            reasons.push_back(fmt::format("{} zones with 80+ triggers in ~60 units", extremeBins));
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

        for (auto* t : triggers) {
            // Static camera trigger (2062) or camera offset (1916)
            // The key sign: moveY pointing to extreme positions
            if (t->id == 2062 || t->id == 1916) {
                float destY = t->moveY; // for offset this is the offset amount
                if (t->id == 2062) destY = t->y + t->moveY; // static = absolute target

                // Check if pointing way above or below play area
                if (std::abs(destY) > 1200.f || destY > OFF_HIGH || destY < OFF_LOW) {
                    // Is there actually content at the destination?
                    int contentNearDest = 0;
                    for (auto* o : offscreenAny) {
                        if (std::abs(o->y - destY) < 600.f) contentNearDest++;
                    }

                    if (contentNearDest > 15) {
                        pct += 60;
                        reasons.push_back(fmt::format("Camera/static trigger to Y={:.0f} where {} objects exist", destY, contentNearDest));
                        break;
                    } else {
                        pct += 20;
                        reasons.push_back(fmt::format("Camera trigger to extreme Y={:.0f}", destY));
                    }
                }
            }

            // Move triggers with extreme teleport distance
            if (t->id == 901) {
                float dist = std::sqrt(t->moveX*t->moveX + t->moveY*t->moveY);
                if (dist > 5000.f) {
                    pct += 20;
                    reasons.push_back(fmt::format("Move trigger teleport: {:.0f} units", dist));
                }
            }
        }

        mk("Camera Tricks", std::min(pct, 80.f), reasons);
    }

    // ===== 5. TOGGLE REVEALS =====
    {
        float pct = 0;
        std::vector<std::string> reasons;

        std::unordered_map<int, int> grpOffDecoCount;
        for (auto* o : offscreenDeco) {
            for (int g : o->groups) grpOffDecoCount[g]++;
        }

        for (auto* t : triggers) {
            if (t->id == 1049) {
                int tgt = t->targetGroup;
                if (grpOffDecoCount.count(tgt) && grpOffDecoCount[tgt] > 15) {
                    pct += 50;
                    reasons.push_back(fmt::format("Toggle controls group {} with {} off-screen deco objects", tgt, grpOffDecoCount[tgt]));
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
            if (o->mainColor > 0) offDecoChannels.insert(o->mainColor);
            if (o->detailColor > 0) offDecoChannels.insert(o->detailColor);
        }

        std::unordered_map<int, int> alphaOnOffCh;
        for (auto* t : triggers) {
            if (t->id == 1007 && offDecoChannels.count(t->targetColor)) {
                alphaOnOffCh[t->targetColor]++;
            }
        }

        for (auto& [ch, count] : alphaOnOffCh) {
            if (count >= 3) {
                pct += 50;
                reasons.push_back(fmt::format("{} alpha triggers on off-screen deco channel {}", count, ch));
                break;
            }
        }

        mk("Alpha Flashing", pct, reasons);
    }

    result.scanTimeMs = std::chrono::duration<float, std::milli>(clk::now() - t0).count();
    return result;
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    ScanResult fail;
    if (!level) { fail.error = "No level"; return fail; }
    log::info("Scanning '{}' (ID:{})", level->m_levelName, level->m_levelID.value());
    auto data = decodeLevelData(level);
    if (data.empty()) { fail.error = "Could not load level data.\nMake sure it's downloaded."; return fail; }
    auto objs = parseObjects(data);
    if (objs.empty()) { fail.error = "Parsed 0 objects."; return fail; }
    log::info("Parsed {} objects", objs.size());
    return analyze(objs);
}
