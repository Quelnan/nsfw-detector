#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <cmath>

NSFWDetector* NSFWDetector::s_instance = nullptr;

NSFWDetector* NSFWDetector::get() {
    if (!s_instance) {
        s_instance = new NSFWDetector();
        s_instance->m_sensitivity = Mod::get()->getSettingValue<int64_t>("sensitivity");
    }
    return s_instance;
}

cocos2d::ccColor3B NSFWDetector::colorForPercent(float p) {
    if (p < 20.f) return {0, 255, 80};
    if (p < 50.f) return {255, 220, 0};
    if (p < 75.f) return {255, 140, 0};
    return {255, 60, 60};
}

// ---- manual base64 decode (no cocos dependency) ----
static const std::string B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<unsigned char> decodeBase64(std::string const& input) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[B64[i]] = i;

    int val = 0, bits = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

std::string NSFWDetector::getDecodedLevelData(GJGameLevel* level) {
    if (!level) return "";

    std::string raw = level->m_levelString;
    if (raw.empty()) {
        log::warn("Level string empty for '{}'", level->m_levelName);
        return "";
    }

    log::info("Raw level string size: {} for '{}'", raw.size(), level->m_levelName);

    // Already decompressed?
    if (raw.size() > 10 && raw.find(';') != std::string::npos && raw.find(',') < 50) {
        log::info("Data already decompressed: {} bytes", raw.size());
        return raw;
    }

    // URL-safe base64 fixup
    std::string b64 = raw;
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }

    // Decode base64
    auto decoded = decodeBase64(b64);
    if (decoded.empty()) {
        log::warn("Base64 decode produced 0 bytes");
        return "";
    }
    log::info("Base64 decoded: {} bytes", decoded.size());

    // gzip inflate
    try {
        unsigned char* inflated = nullptr;
        auto len = ZipUtils::ccInflateMemory(
            decoded.data(), (unsigned int)decoded.size(), &inflated
        );
        if (inflated && len > 0) {
            std::string result((char*)inflated, len);
            free(inflated);
            log::info("Inflated: {} bytes", result.size());
            return result;
        }
        if (inflated) free(inflated);
    } catch (...) {
        log::warn("Inflate failed");
    }

    // Maybe not gzipped
    std::string asStr(decoded.begin(), decoded.end());
    if (asStr.find(';') != std::string::npos) {
        log::info("Decoded has semicolons, using as-is");
        return asStr;
    }

    log::warn("Could not decode level data");
    return "";
}

struct ParsedObj {
    int id = 0;
    float x = 0, y = 0;
};

static std::vector<ParsedObj> parseObjects(std::string const& data) {
    std::vector<ParsedObj> objects;
    std::istringstream stream(data);
    std::string objStr;
    bool isHeader = true;

    while (std::getline(stream, objStr, ';')) {
        if (isHeader) { isHeader = false; continue; }
        if (objStr.empty()) continue;

        ParsedObj obj;
        std::istringstream os(objStr);
        std::string tok;
        std::vector<std::string> toks;
        while (std::getline(os, tok, ',')) toks.push_back(tok);

        for (size_t i = 0; i + 1 < toks.size(); i += 2) {
            int key = 0;
            try { key = std::stoi(toks[i]); } catch (...) { continue; }
            try {
                switch (key) {
                    case 1: obj.id = std::stoi(toks[i+1]); break;
                    case 2: obj.x = std::stof(toks[i+1]); break;
                    case 3: obj.y = std::stof(toks[i+1]); break;
                }
            } catch (...) {}
        }
        if (obj.id > 0) objects.push_back(obj);
    }
    return objects;
}

ScanResult NSFWDetector::analyzeDecodedData(std::string const& data) {
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    ScanResult result;
    result.dataAvailable = true;

    auto objects = parseObjects(data);
    result.objectsScanned = (int)objects.size();

    if (objects.empty()) {
        result.error = "Parsed 0 objects from level data";
        result.scanTimeMs = std::chrono::duration<float, std::milli>(clk::now() - t0).count();
        return result;
    }

    float sens = m_sensitivity / 50.f;
    int colorT=0,pulseT=0,alphaT=0,moveT=0,toggleT=0,spawnT=0,camOff=0,camStat=0,editObj=0;
    int totalT=0,highObj=0,lowObj=0;
    std::unordered_map<int,int> bins;

    for (auto& o : objects) {
        if (o.y > 1500.f) highObj++;
        if (o.y < -500.f) lowObj++;
        bool t = false;
        switch (o.id) {
            case 899:colorT++;t=true;break; case 901:moveT++;t=true;break;
            case 1006:pulseT++;t=true;break; case 1007:alphaT++;t=true;break;
            case 1049:toggleT++;t=true;break; case 1268:spawnT++;t=true;break;
            case 1916:camOff++;t=true;break; case 2062:camStat++;t=true;break;
            case 3600:editObj++;t=true;break;
            case 1346:case 1347:case 1520:case 1585:case 1595:
            case 1611:case 1811:case 1815:case 1817:t=true;break;
        }
        if (t) { totalT++; bins[(int)(o.x/60.f)]++; }
    }

    auto mk = [&](std::string id, std::string nm, float p, std::vector<std::string> r) {
        CategoryScore c; c.id=id; c.name=nm;
        c.percent=std::clamp(p*sens,0.f,100.f);
        c.color=colorForPercent(c.percent); c.reasons=std::move(r);
        result.totalPercent+=c.percent; result.categories.push_back(c);
    };

    float a=0; std::vector<std::string> ar;
    if(result.objectsScanned>1500){a+=10;ar.push_back("Large object count");}
    if(result.objectsScanned>5000){a+=15;ar.push_back("Very large object count");}
    if(highObj>20){a+=20;ar.push_back(fmt::format("{} objects very high",highObj));}
    if(lowObj>20){a+=20;ar.push_back(fmt::format("{} objects very low",lowObj));}
    if(editObj>10){a+=10;ar.push_back("Edit object triggers");}
    mk("art","Art Detection",a,ar);

    float l=0; std::vector<std::string> lr;
    if(spawnT>30){l+=30;lr.push_back(fmt::format("{} spawn triggers",spawnT));}
    if(pulseT>40){l+=20;lr.push_back(fmt::format("{} pulse triggers",pulseT));}
    if(totalT>200){l+=25;lr.push_back(fmt::format("{} total triggers",totalT));}
    int db=0; for(auto&[b,c]:bins)if(c>=40)db++;
    if(db>0){l+=std::min(25.f,db*8.f);lr.push_back(fmt::format("{} dense zones",db));}
    mk("lag","Lag Machine",l,lr);

    float co=0; std::vector<std::string> cr;
    if(colorT>20){co+=20;cr.push_back(fmt::format("{} color triggers",colorT));}
    if(alphaT>10){co+=20;cr.push_back(fmt::format("{} alpha triggers",alphaT));}
    if(pulseT>20){co+=10;cr.push_back("Heavy visual triggers");}
    mk("color","Color Tricks",co,cr);

    float cm=0; std::vector<std::string> cmr;
    if(camOff>0){cm+=std::min(35.f,camOff*5.f);cmr.push_back(fmt::format("{} cam offset",camOff));}
    if(camStat>0){cm+=std::min(35.f,camStat*6.f);cmr.push_back(fmt::format("{} cam static",camStat));}
    if(moveT>50){cm+=10;cmr.push_back("Heavy move triggers");}
    if(highObj>20||lowObj>20){cm+=10;cmr.push_back("Off-screen objects");}
    mk("camera","Camera Tricks",cm,cmr);

    float tg=0; std::vector<std::string> tgr;
    if(toggleT>10){tg+=20;tgr.push_back(fmt::format("{} toggle triggers",toggleT));}
    if(toggleT>40){tg+=20;tgr.push_back("Heavy group visibility");}
    mk("toggle","Toggle Reveals",tg,tgr);

    float al=0; std::vector<std::string> alr;
    if(alphaT>5){al+=20;alr.push_back(fmt::format("{} alpha triggers",alphaT));}
    if(alphaT>20){al+=20;alr.push_back("Possible hidden flashing");}
    mk("alpha","Alpha Flashing",al,alr);

    result.scanTimeMs = std::chrono::duration<float,std::milli>(clk::now()-t0).count();
    return result;
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    ScanResult fail;
    if (!level) { fail.error="No level"; return fail; }
    log::info("Scanning '{}' (ID:{})", level->m_levelName, level->m_levelID.value());
    auto decoded = getDecodedLevelData(level);
    if (decoded.empty()) {
        fail.error="Could not load level data.\n\nMake sure the level is downloaded.";
        return fail;
    }
    return analyzeDecodedData(decoded);
}
