#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <set>
#include <numeric>

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

// ---- base64 ----
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
    if (raw.size() > 10 && raw.find(';') != std::string::npos && raw.find(',') < 50)
        return raw;

    // URL-safe base64
    std::string b64 = raw;
    for (char& c : b64) { if (c=='-') c='+'; else if (c=='_') c='/'; }

    auto decoded = b64decode(b64);
    if (decoded.empty()) return "";

    // gzip
    try {
        unsigned char* inflated = nullptr;
        auto len = ZipUtils::ccInflateMemory(decoded.data(), (unsigned int)decoded.size(), &inflated);
        if (inflated && len > 0) {
            std::string r((char*)inflated, len);
            free(inflated);
            return r;
        }
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
            try {
                switch (key) {
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
                        std::istringstream gs(v);
                        std::string g;
                        while (std::getline(gs, g, '.'))
                            if (!g.empty()) try { obj.groups.push_back(std::stoi(g)); } catch (...) {}
                        break;
                    }
                }
            } catch (...) {}
        }
        if (obj.id > 0) objects.push_back(obj);
    }
    return objects;
}

// ---- The actual pattern-based analysis ----

ScanResult NSFWDetector::analyze(std::vector<LevelObject> const& objects) {
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    ScanResult result;
    result.objectsScanned = (int)objects.size();
    if (objects.empty()) {
        result.error = "No objects parsed";
        return result;
    }

    float sens = m_sens / 50.f;

    // ===== Classify objects =====
    // Normal play area: Y roughly -200 to 800
    constexpr float OFFSCREEN_HIGH = 1200.f;
    constexpr float OFFSCREEN_LOW = -600.f;

    std::vector<LevelObject const*> offscreenObjects;
    std::vector<LevelObject const*> normalObjects;
    std::vector<LevelObject const*> allTriggers;

    // Trigger IDs
    auto isTrigger = [](int id) {
        return id==899||id==901||id==1006||id==1007||id==1049||id==1268||
               id==1346||id==1347||id==1520||id==1585||id==1595||id==1611||
               id==1811||id==1815||id==1817||id==1916||id==2062||id==3600;
    };

    for (auto const& o : objects) {
        if (isTrigger(o.id)) {
            allTriggers.push_back(&o);
        }
        if (!isTrigger(o.id)) {
            if (o.y > OFFSCREEN_HIGH || o.y < OFFSCREEN_LOW) {
                offscreenObjects.push_back(&o);
            } else {
                normalObjects.push_back(&o);
            }
        }
    }

    auto mk = [&](std::string nm, float p, std::vector<std::string> r) {
        CategoryScore c;
        c.name = nm;
        c.percent = std::clamp(p * sens, 0.f, 100.f);
        c.color = colorForPercent(c.percent);
        c.reasons = std::move(r);
        result.totalPercent += c.percent;
        result.categories.push_back(c);
    };

    // ===== 1. OFF-SCREEN ART DETECTION =====
    // Key insight: normal levels rarely have 20+ non-trigger objects far off-screen
    // NSFW levels place entire art pieces above Y=1200 or below Y=-600
    {
        float pct = 0;
        std::vector<std::string> reasons;

        int offCount = (int)offscreenObjects.size();
        float offRatio = (float)offCount / std::max(1, result.objectsScanned);

        // Check if off-screen objects form a dense cluster (art-like)
        if (offCount > 50) {
            // Find bounding box of off-screen cluster
            float minX=1e9, maxX=-1e9, minY=1e9, maxY=-1e9;
            for (auto* o : offscreenObjects) {
                minX = std::min(minX, o->x); maxX = std::max(maxX, o->x);
                minY = std::min(minY, o->y); maxY = std::max(maxY, o->y);
            }
            float w = maxX - minX, h = maxY - minY;

            // Art has notable height AND width (not just scattered objects)
            if (w > 100 && h > 100) {
                pct += 40;
                reasons.push_back(fmt::format("{} objects off-screen in {:.0f}x{:.0f} area", offCount, w, h));
            }

            // Very dense cluster = almost certainly art
            float area = std::max(w * h, 1.f);
            float density = offCount / (area / 10000.f);
            if (density > 2.f) {
                pct += 30;
                reasons.push_back(fmt::format("Dense cluster (density: {:.1f})", density));
            }
        } else if (offCount > 20) {
            pct += 15;
            reasons.push_back(fmt::format("{} objects placed off-screen", offCount));
        }

        mk("Off-Screen Art", pct, reasons);
    }

    // ===== 2. COLOR CHANNEL ANALYSIS =====
    // Key insight: NSFW levels color off-screen objects via color triggers
    // Normal levels don't have color triggers targeting channels used only by off-screen objects
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Find color channels used by off-screen objects
        std::unordered_set<int> offscreenChannels;
        for (auto* o : offscreenObjects) {
            if (o->mainColor > 0) offscreenChannels.insert(o->mainColor);
            if (o->detailColor > 0) offscreenChannels.insert(o->detailColor);
        }

        // Find color channels used by normal objects
        std::unordered_set<int> normalChannels;
        for (auto* o : normalObjects) {
            if (o->mainColor > 0) normalChannels.insert(o->mainColor);
            if (o->detailColor > 0) normalChannels.insert(o->detailColor);
        }

        // Channels ONLY used by off-screen objects = suspicious
        std::unordered_set<int> exclusiveOffscreenChannels;
        for (int ch : offscreenChannels) {
            if (!normalChannels.count(ch)) {
                exclusiveOffscreenChannels.insert(ch);
            }
        }

        // Color triggers targeting those exclusive channels
        int suspiciousColorTriggers = 0;
        for (auto* t : allTriggers) {
            if (t->id == 899 || t->id == 1006) { // color or pulse
                if (exclusiveOffscreenChannels.count(t->targetColor)) {
                    suspiciousColorTriggers++;
                }
            }
        }

        if (suspiciousColorTriggers > 3) {
            pct += 50;
            reasons.push_back(fmt::format("{} color triggers target off-screen-only channels", suspiciousColorTriggers));
        }
        if (exclusiveOffscreenChannels.size() > 5) {
            pct += 20;
            reasons.push_back(fmt::format("{} color channels used only by off-screen objects", exclusiveOffscreenChannels.size()));
        }

        mk("Hidden Coloring", pct, reasons);
    }

    // ===== 3. LAG MACHINE DETECTION =====
    // Key insight: lag machines pack tons of triggers in a TINY area (not spread across level)
    // Normal levels spread triggers across the full length
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Bin triggers by X position (60-unit bins ~ 2 blocks)
        std::unordered_map<int, int> bins;
        for (auto* t : allTriggers) bins[(int)(t->x / 60.f)]++;

        int extremeBins = 0;
        int maxBinCount = 0;
        for (auto& [bin, count] : bins) {
            maxBinCount = std::max(maxBinCount, count);
            if (count >= 80) extremeBins++;  // 80+ triggers in 2 blocks = insane
        }

        if (extremeBins > 0) {
            pct += std::min(60.f, extremeBins * 25.f);
            reasons.push_back(fmt::format("{} zones with 80+ triggers packed together", extremeBins));
        }

        // Spawn trigger loops: spawn triggers whose target group contains another spawn trigger
        std::unordered_map<int, std::vector<int>> spawnGroupToTarget; // group -> target groups
        for (auto* t : allTriggers) {
            if (t->id == 1268) { // spawn
                for (int g : t->groups) {
                    spawnGroupToTarget[g].push_back(t->targetGroup);
                }
            }
        }
        // Simple loop check: A->B->A
        for (auto& [grpA, targets] : spawnGroupToTarget) {
            for (int grpB : targets) {
                if (spawnGroupToTarget.count(grpB)) {
                    for (int grpC : spawnGroupToTarget[grpB]) {
                        if (grpC == grpA) {
                            pct += 40;
                            reasons.push_back(fmt::format("Spawn trigger loop: group {} <-> {}", grpA, grpB));
                            goto lagDone;
                        }
                    }
                }
            }
        }
        lagDone:

        mk("Lag Machine", pct, reasons);
    }

    // ===== 4. CAMERA TRICKS =====
    // Key insight: camera triggers with extreme Y offset pointing to where off-screen objects exist
    {
        float pct = 0;
        std::vector<std::string> reasons;

        for (auto* t : allTriggers) {
            if (t->id == 1916 || t->id == 2062) { // camera offset / static
                float absY = std::abs(t->moveY);
                if (absY > 800.f) {
                    // Check if there are objects near where the camera would point
                    float destY = t->y + t->moveY;
                    int nearbyOffscreen = 0;
                    for (auto* o : offscreenObjects) {
                        if (std::abs(o->y - destY) < 500.f) nearbyOffscreen++;
                    }
                    if (nearbyOffscreen > 10) {
                        pct += 50;
                        reasons.push_back(fmt::format("Camera trigger jumps to Y={:.0f} where {} objects exist", destY, nearbyOffscreen));
                        break;
                    } else if (absY > 1500.f) {
                        pct += 20;
                        reasons.push_back(fmt::format("Camera trigger with extreme offset Y={:.0f}", t->moveY));
                    }
                }
            }

            // Move triggers with extreme distance
            if (t->id == 901) {
                float dist = std::sqrt(t->moveX*t->moveX + t->moveY*t->moveY);
                if (dist > 3000.f) {
                    pct += 15;
                    reasons.push_back(fmt::format("Move trigger teleports {:.0f} units", dist));
                }
            }
        }

        mk("Camera Tricks", pct, reasons);
    }

    // ===== 5. TOGGLE REVEALS =====
    // Key insight: toggle triggers controlling groups with many OFF-SCREEN objects
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Map groups to their off-screen object count
        std::unordered_map<int, int> groupOffscreenCount;
        for (auto* o : offscreenObjects) {
            for (int g : o->groups) groupOffscreenCount[g]++;
        }

        // Check toggle triggers
        for (auto* t : allTriggers) {
            if (t->id == 1049) { // toggle
                int target = t->targetGroup;
                if (groupOffscreenCount.count(target) && groupOffscreenCount[target] > 20) {
                    pct += 40;
                    reasons.push_back(fmt::format("Toggle controls group {} with {} off-screen objects",
                        target, groupOffscreenCount[target]));
                    break;
                }
            }
        }

        mk("Toggle Reveals", pct, reasons);
    }

    // ===== 6. ALPHA FLASHING =====
    // Key insight: rapid alpha 0->1->0 on channels with off-screen objects
    {
        float pct = 0;
        std::vector<std::string> reasons;

        // Find alpha triggers and check for rapid on/off patterns
        std::unordered_map<int, std::vector<LevelObject const*>> alphaByChannel;
        for (auto* t : allTriggers) {
            if (t->id == 1007) alphaByChannel[t->targetColor].push_back(t);
        }

        // Get off-screen channels
        std::unordered_set<int> offCh;
        for (auto* o : offscreenObjects) {
            if (o->mainColor > 0) offCh.insert(o->mainColor);
            if (o->detailColor > 0) offCh.insert(o->detailColor);
        }

        for (auto& [ch, triggers] : alphaByChannel) {
            if (!offCh.count(ch)) continue; // only care about off-screen channels
            if (triggers.size() >= 3) {
                pct += 40;
                reasons.push_back(fmt::format("Alpha trigger pattern on off-screen channel {} ({} triggers)", ch, triggers.size()));
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
    if (data.empty()) {
        fail.error = "Could not load level data.\nMake sure the level is downloaded.";
        return fail;
    }

    auto objects = parseObjects(data);
    log::info("Parsed {} objects", objects.size());

    if (objects.empty()) {
        fail.error = "Parsed 0 objects from level data.";
        return fail;
    }

    return analyze(objects);
}
