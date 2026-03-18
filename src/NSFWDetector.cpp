#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <set>
#include <tuple>

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
                case 7: obj.trigR = std::stoi(v); break;
                case 8: obj.trigG = std::stoi(v); break;
                case 9: obj.trigB = std::stoi(v); break;
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
                case 71: obj.staticGroup = std::stoi(v); break;
            }} catch (...) {}
        }
        if (obj.id > 0) objects.push_back(obj);
    }
    return objects;
}

std::unordered_map<int, std::tuple<int,int,int>> NSFWDetector::parseHeaderColors(std::string const& data) {
    std::unordered_map<int, std::tuple<int,int,int>> colors;
    auto ks38 = data.find("kS38,");
    if (ks38 == std::string::npos) return colors;
    size_t valStart = ks38 + 5;
    auto valEnd = data.find(",kA", valStart);
    if (valEnd == std::string::npos) valEnd = data.find(';', valStart);
    if (valEnd == std::string::npos) valEnd = data.size();
    std::string section = data.substr(valStart, valEnd - valStart);

    std::istringstream colorStream(section);
    std::string colorDef;
    while (std::getline(colorStream, colorDef, '|')) {
        std::istringstream cs(colorDef);
        std::string part;
        std::vector<std::string> parts;
        while (std::getline(cs, part, '_')) parts.push_back(part);

        int r=255, g=255, b=255, chId=0;
        for (size_t i = 0; i+1 < parts.size(); i += 2) {
            try {
                int k = std::stoi(parts[i]);
                if (k == 1) r = std::stoi(parts[i+1]);
                else if (k == 2) g = std::stoi(parts[i+1]);
                else if (k == 3) b = std::stoi(parts[i+1]);
                else if (k == 6) chId = std::stoi(parts[i+1]);
            } catch (...) {}
        }
        if (chId > 0) colors[chId] = {r, g, b};
    }
    return colors;
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

static bool isSkinTone(int r, int g, int b) {
    if (r==0 && g==0 && b==0) return false;
    if (r==255 && g==255 && b==255) return false;

    float rf=r/255.f, gf=g/255.f, bf=b/255.f;
    float mx=std::max({rf,gf,bf}), mn=std::min({rf,gf,bf});
    float diff=mx-mn;

    float h=0;
    if (diff > 0) {
        if (mx==rf) h = std::fmod((60.f*((gf-bf)/diff)+360.f), 360.f);
        else if (mx==gf) h = std::fmod((60.f*((bf-rf)/diff)+120.f), 360.f);
        else h = std::fmod((60.f*((rf-gf)/diff)+240.f), 360.f);
    }
    float s = (mx > 0) ? diff/mx : 0;
    float v = mx;

    bool inHue = (h >= 0 && h <= 55) || (h >= 335 && h <= 360);
    bool inSat = s >= 0.10f && s <= 0.78f;
    bool inVal = v >= 0.20f && v <= 1.0f;
    bool rDom = r >= g;

    return inHue && inSat && inVal && rDom;
}

ScanResult NSFWDetector::analyze(std::vector<LevelObject> const& objects, std::string const& levelData) {
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    ScanResult result;
    result.objectsScanned = (int)objects.size();
    if (objects.empty()) { result.error = "No objects"; return result; }

    float sens = m_sens / 50.f;
    constexpr float OFF_HIGH = 500.f;
    constexpr float OFF_LOW = -800.f;

    std::vector<LevelObject const*> offscreenDeco, normalDeco, triggers, offscreenAll;

    for (auto const& o : objects) {
        bool offscreen = o.y > OFF_HIGH || o.y < OFF_LOW;
        if (isTrigger(o.id)) { triggers.push_back(&o); }
        else if (isDecoObject(o.id)) {
            if (offscreen) offscreenDeco.push_back(&o);
            else normalDeco.push_back(&o);
        }
        if (offscreen && !isTrigger(o.id)) offscreenAll.push_back(&o);
    }

    auto mk = [&](std::string nm, float p, std::vector<std::string> r) {
        CategoryScore c; c.name = nm;
        c.percent = std::clamp(p * sens, 0.f, 100.f);
        c.color = colorForPercent(c.percent);
        c.reasons = std::move(r);
        result.totalPercent += c.percent;
        result.categories.push_back(c);
    };

    // Get channel sets
    std::unordered_set<int> offCh, normCh;
    for (auto* o : offscreenDeco) {
        if (o->mainColor>0) offCh.insert(o->mainColor);
        if (o->detailColor>0) offCh.insert(o->detailColor);
    }
    for (auto* o : normalDeco) {
        if (o->mainColor>0) normCh.insert(o->mainColor);
        if (o->detailColor>0) normCh.insert(o->detailColor);
    }
    std::unordered_set<int> exclusiveCh;
    for (int ch : offCh) if (!normCh.count(ch)) exclusiveCh.insert(ch);

    // ===== 1. OFF-SCREEN ART =====
    {
        float pct = 0; std::vector<std::string> reasons;
        int n = (int)offscreenDeco.size();
        if (n > 30) {
            float x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;
            for (auto* o : offscreenDeco) {
                x0=std::min(x0,o->x); x1=std::max(x1,o->x);
                y0=std::min(y0,o->y); y1=std::max(y1,o->y);
            }
            float w=x1-x0, h=y1-y0, area=std::max(w*h,1.f);
            float density=n/(area/10000.f);
            if (w>100&&h>100&&density>1.5f) { pct+=40; reasons.push_back(fmt::format("{} deco in dense cluster ({:.0f}x{:.0f})",n,w,h)); }
            else if (n>80) { pct+=30; reasons.push_back(fmt::format("{} deco off-screen",n)); }
            else { pct+=15; reasons.push_back(fmt::format("{} deco off-screen",n)); }
        }
        mk("Off-Screen Art", pct, reasons);
    }

    // ===== 2. HIDDEN COLORING =====
    {
        float pct = 0; std::vector<std::string> reasons;
        struct PK { int x,y; bool operator==(PK const& o) const { return x==o.x&&y==o.y; } };
        struct PH { size_t operator()(PK const& k) const { return std::hash<int>()(k.x)^(std::hash<int>()(k.y)<<16); } };
        std::unordered_map<PK,int,PH> stacks;
        for (auto* t : triggers)
            if (t->id==899||t->id==1006) stacks[{(int)(t->x/3.f),(int)(t->y/3.f)}]++;
        int heavy=0, mx=0;
        for (auto&[p,c]:stacks) { mx=std::max(mx,c); if(c>=8) heavy++; }
        if (heavy>0) { pct+=std::min(50.f,heavy*10.f); reasons.push_back(fmt::format("{} positions with 8+ stacked color triggers",heavy)); }
        int exCnt=0;
        for (int ch:offCh) if (!normCh.count(ch)) exCnt++;
        if (exCnt>3) { pct+=25; reasons.push_back(fmt::format("{} exclusive off-screen color channels",exCnt)); }
        mk("Hidden Coloring", pct, reasons);
    }

    // ===== 3. LAG MACHINE =====
    {
        float pct = 0; std::vector<std::string> reasons;
        std::unordered_map<int,int> bins;
        for (auto* t:triggers) bins[(int)(t->x/60.f)]++;
        int db=0; for (auto&[b,c]:bins) if(c>=80) db++;
        if (db>0) { pct+=std::min(60.f,db*25.f); reasons.push_back(fmt::format("{} zones with 80+ triggers",db)); }
        std::unordered_map<int,std::vector<int>> sm;
        for (auto* t:triggers) if(t->id==1268) for(int g:t->groups) sm[g].push_back(t->targetGroup);
        bool fl=false;
        for (auto&[a,ts]:sm) { for(int b:ts) if(sm.count(b)) for(int c:sm[b]) if(c==a) {
            pct+=40; reasons.push_back(fmt::format("Spawn loop: {} <-> {}",a,b)); fl=true; break;
        } if(fl) break; }
        mk("Lag Machine", pct, reasons);
    }

    // ===== 4. CAMERA TRICKS =====
    {
        float pct = 0; std::vector<std::string> reasons;
        for (auto* t:triggers) {
            if (t->id==1914 && t->staticGroup>0) {
                for (auto const& o:objects) {
                    if (isTrigger(o.id)) continue;
                    for (int g:o.groups) if (g==t->staticGroup && o.y>500.f) {
                        pct+=25; reasons.push_back(fmt::format("Static cam group {} -> object at Y={:.0f}",t->staticGroup,o.y));
                        goto camDone;
                    }
                }
            }
        }
        camDone:
        for (auto* t:triggers) {
            if ((t->id==1916||t->id==2062) && std::abs(t->moveY)>500.f) {
                float dy=(t->id==1916)?t->moveY:t->y+t->moveY;
                int nearby=0; for(auto*o:offscreenAll) if(std::abs(o->y-dy)<1000.f) nearby++;
                if (nearby>15) { pct+=30; reasons.push_back(fmt::format("Camera to Y={:.0f}, {} objects there",dy,nearby)); break; }
                else if (std::abs(t->moveY)>1200.f) { pct+=15; reasons.push_back(fmt::format("Extreme camera Y={:.0f}",t->moveY)); }
            }
        }
        mk("Camera Tricks", std::min(pct,80.f), reasons);
    }

    // ===== 5. SKIN TONE ANALYSIS =====
    {
        float pct = 0; std::vector<std::string> reasons;

        auto headerColors = parseHeaderColors(levelData);

        // Get all channels
        std::unordered_set<int> allCh;
        for (auto const& o:objects) {
            if (o.mainColor>0) allCh.insert(o.mainColor);
            if (o.detailColor>0) allCh.insert(o.detailColor);
        }

        // Get trigger colors per channel
        std::unordered_map<int, std::vector<std::tuple<int,int,int>>> trigColors;
        for (auto* t:triggers) {
            if (t->id==899) trigColors[t->targetColor].push_back({t->trigR, t->trigG, t->trigB});
        }

        int skinOffscreen = 0;
        int skinHiddenReveal = 0;
        int skinGeneral = 0;

        for (int ch : allCh) {
            bool isExclusive = exclusiveCh.count(ch) > 0;
            bool isOff = offCh.count(ch) > 0;
            bool hasSkin = false;
            bool startsBlack = false;
            bool skinFromTrigger = false;

            // Check initial color
            if (headerColors.count(ch)) {
                auto [r,g,b] = headerColors[ch];
                if (r==0 && g==0 && b==0) startsBlack = true;
                if (isSkinTone(r,g,b)) hasSkin = true;
            }

            // Check trigger colors
            if (trigColors.count(ch)) {
                for (auto& [r,g,b] : trigColors[ch]) {
                    if (isSkinTone(r,g,b)) {
                        hasSkin = true;
                        skinFromTrigger = true;
                    }
                }
            }

            if (hasSkin) {
                skinGeneral++;
                if (isExclusive) skinOffscreen++;
                if (startsBlack && skinFromTrigger) skinHiddenReveal++;
            }
        }

        // Scoring - skin tone is the MAIN signal
        if (skinOffscreen >= 1) {
            pct = std::max(pct, 60.f);
            reasons.push_back(fmt::format("{} off-screen-only channel(s) use skin-tone colors", skinOffscreen));
        }
        if (skinOffscreen >= 2) {
            pct = std::max(pct, 75.f);
            reasons.push_back("Multiple off-screen skin-tone channels");
        }
        if (skinOffscreen >= 3) {
            pct = std::max(pct, 90.f);
            reasons.push_back("Heavy off-screen skin-tone usage");
        }

        if (skinHiddenReveal >= 1) {
            pct = std::max(pct, 75.f);
            reasons.push_back(fmt::format("{} hidden channel(s) turn skin-tone via trigger", skinHiddenReveal));
        }
        if (skinHiddenReveal >= 2) {
            pct = std::max(pct, 90.f);
            reasons.push_back("Multiple hidden->skin reveal channels");
        }

        // General fallback
        if (skinGeneral >= 1 && skinOffscreen == 0 && skinHiddenReveal == 0) {
            pct = std::max(pct, 25.f);
            reasons.push_back(fmt::format("{} skin-tone channel(s) detected", skinGeneral));
        }
        if (skinGeneral >= 2 && skinOffscreen == 0 && skinHiddenReveal == 0) {
            pct = std::max(pct, 40.f);
            reasons.push_back("Multiple skin-tone channels");
        }
        if (skinGeneral >= 3 && skinOffscreen == 0 && skinHiddenReveal == 0) {
            pct = std::max(pct, 55.f);
            reasons.push_back("Strong skin-tone presence");
        }

        mk("Skin Tone", pct, reasons);
    }

    // ===== 6. TOGGLE REVEALS =====
    {
        float pct = 0; std::vector<std::string> reasons;
        std::unordered_map<int,int> gc;
        for (auto* o:offscreenDeco) for (int g:o->groups) gc[g]++;
        for (auto* t:triggers) if(t->id==1049) {
            int tgt=t->targetGroup;
            if (gc.count(tgt)&&gc[tgt]>15) { pct+=40; reasons.push_back(fmt::format("Toggle grp {} controls {} off-screen deco",tgt,gc[tgt])); break; }
        }
        mk("Toggle Reveals", pct, reasons);
    }

    // ===== 7. ALPHA FLASHING =====
    {
        float pct = 0; std::vector<std::string> reasons;
        std::unordered_set<int> oc;
        for (auto* o:offscreenDeco) { if(o->mainColor>0)oc.insert(o->mainColor); if(o->detailColor>0)oc.insert(o->detailColor); }
        std::unordered_map<int,int> ac;
        for (auto* t:triggers) if(t->id==1007&&oc.count(t->targetColor)) ac[t->targetColor]++;
        for (auto&[ch,cnt]:ac) if(cnt>=3) { pct+=40; reasons.push_back(fmt::format("{} alpha triggers on off-screen ch {}",cnt,ch)); break; }
        mk("Alpha Flashing", pct, reasons);
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

    try {
        auto path = Mod::get()->getSaveDir() / fmt::format("level_{}.txt", level->m_levelID.value());
        std::ofstream file(path.string());
        if (file.is_open()) { file << data; file.close(); }
    } catch (...) {}

    auto objs = parseObjects(data);
    if (objs.empty()) { fail.error = "Parsed 0 objects."; return fail; }
    log::info("Parsed {} objects", objs.size());
    return analyze(objs, data);
}
