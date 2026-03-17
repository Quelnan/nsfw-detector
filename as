[1mdiff --git a/src/NSFWDetector.cpp b/src/NSFWDetector.cpp[m
[1mindex b68ffb3..d97f777 100644[m
[1m--- a/src/NSFWDetector.cpp[m
[1m+++ b/src/NSFWDetector.cpp[m
[36m@@ -22,100 +22,85 @@[m [mcocos2d::ccColor3B NSFWDetector::colorForPercent(float p) {[m
     return {255, 60, 60};[m
 }[m
 [m
[32m+[m[32m// ---- manual base64 decode (no cocos dependency) ----[m
[32m+[m[32mstatic const std::string B64 =[m
[32m+[m[32m    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";[m
[32m+[m
[32m+[m[32mstatic std::vector<unsigned char> decodeBase64(std::string const& input) {[m
[32m+[m[32m    std::vector<unsigned char> out;[m
[32m+[m[32m    std::vector<int> T(256, -1);[m
[32m+[m[32m    for (int i = 0; i < 64; i++) T[B64[i]] = i;[m
[32m+[m
[32m+[m[32m    int val = 0, bits = -8;[m
[32m+[m[32m    for (unsigned char c : input) {[m
[32m+[m[32m        if (T[c] == -1) continue;[m
[32m+[m[32m        val = (val << 6) + T[c];[m
[32m+[m[32m        bits += 6;[m
[32m+[m[32m        if (bits >= 0) {[m
[32m+[m[32m            out.push_back((val >> bits) & 0xFF);[m
[32m+[m[32m            bits -= 8;[m
[32m+[m[32m        }[m
[32m+[m[32m    }[m
[32m+[m[32m    return out;[m
[32m+[m[32m}[m
[32m+[m
 std::string NSFWDetector::getDecodedLevelData(GJGameLevel* level) {[m
     if (!level) return "";[m
 [m
     std::string raw = level->m_levelString;[m
[31m-[m
     if (raw.empty()) {[m
[31m-        log::warn("Level string is empty for '{}'", level->m_levelName);[m
[32m+[m[32m        log::warn("Level string empty for '{}'", level->m_levelName);[m
         return "";[m
     }[m
 [m
[31m-    log::info("Raw level string size: {}", raw.size());[m
[32m+[m[32m    log::info("Raw level string size: {} for '{}'", raw.size(), level->m_levelName);[m
 [m
[31m-    // Check if already decompressed (contains commas and semicolons = raw object data)[m
[32m+[m[32m    // Already decompressed?[m
     if (raw.size() > 10 && raw.find(';') != std::string::npos && raw.find(',') < 50) {[m
[31m-        log::info("Level data appears already decompressed, size: {}", raw.size());[m
[32m+[m[32m        log::info("Data already decompressed: {} bytes", raw.size());[m
         return raw;[m
     }[m
 [m
[31m-    // Try base64 decode + gzip inflate[m
[31m-    try {[m
[31m-        // Use cocos2d's ZipUtils which handles the full GD level decompression[m
[31m-        // GD levels are typically base64 + gzip[m
[31m-        // ZipUtils::decompressString2 handles this exact case[m
[31m-[m
[31m-        bool needsFree = false;[m
[31m-        unsigned char* decoded = nullptr;[m
[31m-        int decodedLen = 0;[m
[31m-[m
[31m-        // Try the Geode/cocos utility for base64[m
[31m-        std::string b64 = raw;[m
[31m-[m
[31m-        // GD uses URL-safe base64, replace chars[m
[31m-        for (auto& c : b64) {[m
[31m-            if (c == '-') c = '+';[m
[31m-            if (c == '_') c = '/';[m
[31m-        }[m
[31m-[m
[31m-        // Pad if needed[m
[31m-        while (b64.size() % 4 != 0) b64 += '=';[m
[31m-[m
[31m-        // Manual base64 decode using cocos[m
[31m-        decodedLen = cocos2d::base64Decode([m
[31m-            (const unsigned char*)b64.c_str(),[m
[31m-            (unsigned int)b64.size(),[m
[31m-            &decoded[m
[31m-        );[m
[31m-[m
[31m-        if (decoded && decodedLen > 0) {[m
[31m-            // Try gzip decompress[m
[31m-            unsigned char* inflated = nullptr;[m
[31m-            auto inflatedLen = ZipUtils::ccInflateMemory(decoded, decodedLen, &inflated);[m
[31m-[m
[31m-            free(decoded);[m
[32m+[m[32m    // URL-safe base64 fixup[m
[32m+[m[32m    std::string b64 = raw;[m
[32m+[m[32m    for (char& c : b64) {[m
[32m+[m[32m        if (c == '-') c = '+';[m
[32m+[m[32m        else if (c == '_') c = '/';[m
[32m+[m[32m    }[m
 [m
[31m-            if (inflated && inflatedLen > 0) {[m
[31m-                std::string result((char*)inflated, inflatedLen);[m
[31m-                free(inflated);[m
[31m-                log::info("Decompressed via base64+gzip: {} bytes", result.size());[m
[31m-                return result;[m
[31m-            }[m
[31m-            if (inflated) free(inflated);[m
[31m-        }[m
[31m-        if (decoded) free(decoded);[m
[31m-    } catch (...) {[m
[31m-        log::warn("Exception during base64+gzip decompression");[m
[32m+[m[32m    // Decode base64[m
[32m+[m[32m    auto decoded = decodeBase64(b64);[m
[32m+[m[32m    if (decoded.empty()) {[m
[32m+[m[32m        log::warn("Base64 decode produced 0 bytes");[m
[32m+[m[32m        return "";[m
     }[m
[32m+[m[32m    log::info("Base64 decoded: {} bytes", decoded.size());[m
 [m
[31m-    // Fallback: try ZipUtils directly on the raw data[m
[32m+[m[32m    // gzip inflate[m
     try {[m
         unsigned char* inflated = nullptr;[m
[31m-        auto inflatedLen = ZipUtils::ccInflateMemory([m
[31m-            (unsigned char*)raw.c_str(),[m
[31m-            (unsigned int)raw.size(),[m
[31m-            &inflated[m
[32m+[m[32m        auto len = ZipUtils::ccInflateMemory([m
[32m+[m[32m            decoded.data(), (unsigned int)decoded.size(), &inflated[m
         );[m
[31m-[m
[31m-        if (inflated && inflatedLen > 0) {[m
[31m-            std::string result((char*)inflated, inflatedLen);[m
[32m+[m[32m        if (inflated && len > 0) {[m
[32m+[m[32m            std::string result((char*)inflated, len);[m
             free(inflated);[m
[31m-            log::info("Decompressed via raw inflate: {} bytes", result.size());[m
[32m+[m[32m            log::info("Inflated: {} bytes", result.size());[m
             return result;[m
         }[m
         if (inflated) free(inflated);[m
     } catch (...) {[m
[31m-        log::warn("Raw inflate failed too");[m
[32m+[m[32m        log::warn("Inflate failed");[m
     }[m
 [m
[31m-    // Last resort: if it has semicolons, maybe it's usable as-is[m
[31m-    if (raw.find(';') != std::string::npos) {[m
[31m-        log::info("Using raw data as-is (has semicolons)");[m
[31m-        return raw;[m
[32m+[m[32m    // Maybe not gzipped[m
[32m+[m[32m    std::string asStr(decoded.begin(), decoded.end());[m
[32m+[m[32m    if (asStr.find(';') != std::string::npos) {[m
[32m+[m[32m        log::info("Decoded has semicolons, using as-is");[m
[32m+[m[32m        return asStr;[m
     }[m
 [m
[31m-    log::warn("Could not decode level data at all");[m
[32m+[m[32m    log::warn("Could not decode level data");[m
     return "";[m
 }[m
 [m
[36m@@ -126,7 +111,6 @@[m [mstruct ParsedObj {[m
 [m
 static std::vector<ParsedObj> parseObjects(std::string const& data) {[m
     std::vector<ParsedObj> objects;[m
[31m-[m
     std::istringstream stream(data);[m
     std::string objStr;[m
     bool isHeader = true;[m
[36m@@ -136,40 +120,30 @@[m [mstatic std::vector<ParsedObj> parseObjects(std::string const& data) {[m
         if (objStr.empty()) continue;[m
 [m
         ParsedObj obj;[m
[32m+[m[32m        std::istringstream os(objStr);[m
[32m+[m[32m        std::string tok;[m
[32m+[m[32m        std::vector<std::string> toks;[m
[32m+[m[32m        while (std::getline(os, tok, ',')) toks.push_back(tok);[m
 [m
[31m-        std::istringstream objStream(objStr);[m
[31m-        std::string token;[m
[31m-        std::vector<std::string> tokens;[m
[31m-        while (std::getline(objStream, token, ',')) {[m
[31m-            tokens.push_back(token);[m
[31m-        }[m
[31m-[m
[31m-        for (size_t i = 0; i + 1 < tokens.size(); i += 2) {[m
[32m+[m[32m        for (size_t i = 0; i + 1 < toks.size(); i += 2) {[m
             int key = 0;[m
[31m-            try { key = std::stoi(tokens[i]); } catch (...) { continue; }[m
[31m-            auto const& val = tokens[i + 1];[m
[31m-[m
[32m+[m[32m            try { key = std::stoi(toks[i]); } catch (...) { continue; }[m
             try {[m
                 switch (key) {[m
[31m-                    case 1: obj.id = std::stoi(val); break;[m
[31m-                    case 2: obj.x = std::stof(val); break;[m
[31m-                    case 3: obj.y = std::stof(val); break;[m
[32m+[m[32m                    case 1: obj.id = std::stoi(toks[i+1]); break;[m
[32m+[m[32m                    case 2: obj.x = std::stof(toks[i+1]); break;[m
[32m+[m[32m                    case 3: obj.y = std::stof(toks[i+1]); break;[m
                 }[m
             } catch (...) {}[m
         }[m
[31m-[m
[31m-        if (obj.id > 0) {[m
[31m-            objects.push_back(obj);[m
[31m-        }[m
[32m+[m[32m        if (obj.id > 0) objects.push_back(obj);[m
     }[m
[31m-[m
     return objects;[m
 }[m
 [m
 ScanResult NSFWDetector::analyzeDecodedData(std::string const& data) {[m
[31m-    using namespace std::chrono;[m
[31m-    auto t0 = high_resolution_clock::now();[m
[31m-[m
[32m+[m[32m    using clk = std::chrono::high_resolution_clock;[m
[32m+[m[32m    auto t0 = clk::now();[m
     ScanResult result;[m
     result.dataAvailable = true;[m
 [m
[36m@@ -178,123 +152,89 @@[m [mScanResult NSFWDetector::analyzeDecodedData(std::string const& data) {[m
 [m
     if (objects.empty()) {[m
         result.error = "Parsed 0 objects from level data";[m
[31m-        auto t1 = high_resolution_clock::now();[m
[31m-        result.scanTimeMs = duration<float, std::milli>(t1 - t0).count();[m
[32m+[m[32m        result.scanTimeMs = std::chrono::duration<float, std::milli>(clk::now() - t0).count();[m
         return result;[m
     }[m
 [m
     float sens = m_sensitivity / 50.f;[m
[31m-[m
[31m-    int colorTriggers = 0, pulseTriggers = 0, alphaTriggers = 0;[m
[31m-    int moveTriggers = 0, toggleTriggers = 0, spawnTriggers = 0;[m
[31m-    int cameraOffset = 0, cameraStatic = 0, editObject = 0;[m
[31m-    int totalTriggers = 0;[m
[31m-    int highObjects = 0, lowObjects = 0;[m
[31m-[m
[31m-    std::unordered_map<int, int> triggerBins;[m
[31m-[m
[31m-    for (auto const& obj : objects) {[m
[31m-        if (obj.y > 1500.f) highObjects++;[m
[31m-        if (obj.y < -500.f) lowObjects++;[m
[31m-[m
[31m-        bool isTrigger = false;[m
[31m-        switch (obj.id) {[m
[31m-            case 899: colorTriggers++; isTrigger = true; break;[m
[31m-            case 901: moveTriggers++; isTrigger = true; break;[m
[31m-            case 1006: pulseTriggers++; isTrigger = true; break;[m
[31m-            case 1007: alphaTriggers++; isTrigger = true; break;[m
[31m-            case 1049: toggleTriggers++; isTrigger = true; break;[m
[31m-            case 1268: spawnTriggers++; isTrigger = true; break;[m
[31m-            case 1916: cameraOffset++; isTrigger = true; break;[m
[31m-            case 2062: cameraStatic++; isTrigger = true; break;[m
[31m-            case 3600: editObject++; isTrigger = true; break;[m
[31m-            case 1346: case 1347: case 1520: case 1585:[m
[31m-            case 1595: case 1611: case 1811: case 1815: case 1817:[m
[31m-                isTrigger = true; break;[m
[31m-        }[m
[31m-        if (isTrigger) {[m
[31m-            totalTriggers++;[m
[31m-            triggerBins[(int)(obj.x / 60.f)]++;[m
[32m+[m[32m    int colorT=0,pulseT=0,alphaT=0,moveT=0,toggleT=0,spawnT=0,camOff=0,camStat=0,editObj=0;[m
[32m+[m[32m    int totalT=0,highObj=0,lowObj=0;[m
[32m+[m[32m    std::unordered_map<int,int> bins;[m
[32m+[m
[32m+[m[32m    for (auto& o : objects) {[m
[32m+[m[32m        if (o.y > 1500.f) highObj++;[m
[32m+[m[32m        if (o.y < -500.f) lowObj++;[m
[32m+[m[32m        bool t = false;[m
[32m+[m[32m        switch (o.id) {[m
[32m+[m[32m            case 899:colorT++;t=true;break; case 901:moveT++;t=true;break;[m
[32m+[m[32m            case 1006:pulseT++;t=true;break; case 1007:alphaT++;t=true;break;[m
[32m+[m[32m            case 1049:toggleT++;t=true;break; case 1268:spawnT++;t=true;break;[m
[32m+[m[32m            case 1916:camOff++;t=true;break; case 2062:camStat++;t=true;break;[m
[32m+[m[32m            case 3600:editObj++;t=true;break;[m
[32m+[m[32m            case 1346:case 1347:case 1520:case 1585:case 1595:[m
[32m+[m[32m            case 1611:case 1811:case 1815:case 1817:t=true;break;[m
         }[m
[32m+[m[32m        if (t) { totalT++; bins[(int)(o.x/60.f)]++; }[m
     }[m
 [m
[31m-    auto makeCategory = [&](std::string id, std::string name, float p, std::vector<std::string> reasons) {[m
[31m-        CategoryScore c;[m
[31m-        c.id = id;[m
[31m-        c.name = name;[m
[31m-        c.percent = std::clamp(p * sens, 0.f, 100.f);[m
[31m-        c.color = colorForPercent(c.percent);[m
[31m-        c.reasons = std::move(reasons);[m
[31m-        result.totalPercent += c.percent;[m
[31m-        result.categories.push_back(c);[m
[32m+[m[32m    auto mk = [&](std::string id, std::string nm, float p, std::vector<std::string> r) {[m
[32m+[m[32m        CategoryScore c; c.id=id; c.name=nm;[m
[32m+[m[32m        c.percent=std::clamp(p*sens,0.f,100.f);[m
[32m+[m[32m        c.color=colorForPercent(c.percent); c.reasons=std::move(r);[m
[32m+[m[32m        result.totalPercent+=c.percent; result.categories.push_back(c);[m
     };[m
 [m
[31m-    float artPct = 0.f;[m
[31m-    std::vector<std::string> artR;[m
[31m-    if (result.objectsScanned > 1500) { artPct += 10; artR.push_back("Large object count"); }[m
[31m-    if (result.objectsScanned > 5000) { artPct += 15; artR.push_back("Very large object count"); }[m
[31m-    if (highObjects > 20) { artPct += 20; artR.push_back(fmt::format("{} objects very high", highObjects)); }[m
[31m-    if (lowObjects > 20) { artPct += 20; artR.push_back(fmt::format("{} objects very low", lowObjects)); }[m
[31m-    if (editObject > 10) { artPct += 10; artR.push_back("Edit object triggers"); }[m
[31m-    makeCategory("art", "Art Detection", artPct, artR);[m
[31m-[m
[31m-    float lagPct = 0.f;[m
[31m-    std::vector<std::string> lagR;[m
[31m-    if (spawnTriggers > 30) { lagPct += 30; lagR.push_back(fmt::format("{} spawn triggers", spawnTriggers)); }[m
[31m-    if (pulseTriggers > 40) { lagPct += 20; lagR.push_back(fmt::format("{} pulse triggers", pulseTriggers)); }[m
[31m-    if (totalTriggers > 200) { lagPct += 25; lagR.push_back(fmt::format("{} total triggers", totalTriggers)); }[m
[31m-    int denseBins = 0;[m
[31m-    for (auto const& [b, c] : triggerBins) if (c >= 40) denseBins++;[m
[31m-    if (denseBins > 0) { lagPct += std::min(25.f, denseBins * 8.f); lagR.push_back(fmt::format("{} dense trigger zones", denseBins)); }[m
[31m-    makeCategory("lag", "Lag Machine", lagPct, lagR);[m
[31m-[m
[31m-    float colPct = 0.f;[m
[31m-    std::vector<std::string> colR;[m
[31m-    if (colorTriggers > 20) { colPct += 20; colR.push_back(fmt::format("{} color triggers", colorTriggers)); }[m
[31m-    if (alphaTriggers > 10) { colPct += 20; colR.push_back(fmt::format("{} alpha triggers", alphaTriggers)); }[m
[31m-    if (pulseTriggers > 20) { colPct += 10; colR.push_back("Heavy visual triggers"); }[m
[31m-    makeCategory("color", "Color Tricks", colPct, colR);[m
[31m-[m
[31m-    float camPct = 0.f;[m
[31m-    std::vector<std::string> camR;[m
[31m-    if (cameraOffset > 0) { camPct += std::min(35.f, cameraOffset * 5.f); camR.push_back(fmt::format("{} camera offset", cameraOffset)); }[m
[31m-    if (cameraStatic > 0) { camPct += std::min(35.f, cameraStatic * 6.f); camR.push_back(fmt::format("{} camera static", cameraStatic)); }[m
[31m-    if (moveTriggers > 50) { camPct += 10; camR.push_back("Heavy move triggers"); }[m
[31m-    if (highObjects > 20 || lowObjects > 20) { camPct += 10; camR.push_back("Off-screen objects"); }[m
[31m-    makeCategory("camera", "Camera Tricks", camPct, camR);[m
[31m-[m
[31m-    float togPct = 0.f;[m
[31m-    std::vector<std::string> togR;[m
[31m-    if (toggleTriggers > 10) { togPct += 20; togR.push_back(fmt::format("{} toggle triggers", toggleTriggers)); }[m
[31m-    if (toggleTriggers > 40) { togPct += 20; togR.push_back("Heavy group visibility"); }[m
[31m-    makeCategory("toggle", "Toggle Reveals", togPct, togR);[m
[31m-[m
[31m-    float alpPct = 0.f;[m
[31m-    std::vector<std::string> alpR;[m
[31m-    if (alphaTriggers > 5) { alpPct += 20; alpR.push_back(fmt::format("{} alpha triggers", alphaTriggers)); }[m
[31m-    if (alphaTriggers > 20) { alpPct += 20; alpR.push_back("Possible hidden flashing"); }[m
[31m-    makeCategory("alpha", "Alpha Flashing", alpPct, alpR);[m
[31m-[m
[31m-    auto t1 = high_resolution_clock::now();[m
[31m-    result.scanTimeMs = duration<float, std::milli>(t1 - t0).count();[m
[32m+[m[32m    float a=0; std::vector<std::string> ar;[m
[32m+[m[32m    if(result.objectsScanned>1500){a+=10;ar.push_back("Large object count");}[m
[32m+[m[32m    if(result.objectsScanned>5000){a+=15;ar.push_back("Very large object count");}[m
[32m+[m[32m    if(highObj>20){a+=20;ar.push_back(fmt::format("{} objects very high",highObj));}[m
[32m+[m[32m    if(lowObj>20){a+=20;ar.push_back(fmt::format("{} objects very low",lowObj));}[m
[32m+[m[32m    if(editObj>10){a+=10;ar.push_back("Edit object triggers");}[m
[32m+[m[32m    mk("art","Art Detection",a,ar);[m
[32m+[m
[32m+[m[32m    float l=0; std::vector<std::string> lr;[m
[32m+[m[32m    if(spawnT>30){l+=30;lr.push_back(fmt::format("{} spawn triggers",spawnT));}[m
[32m+[m[32m    if(pulseT>40){l+=20;lr.push_back(fmt::format("{} pulse triggers",pulseT));}[m
[32m+[m[32m    if(totalT>200){l+=25;lr.push_back(fmt::format("{} total triggers",totalT));}[m
[32m+[m[32m    int db=0; for(auto&[b,c]:bins)if(c>=40)db++;[m
[32m+[m[32m    if(db>0){l+=std::min(25.f,db*8.f);lr.push_back(fmt::format("{} dense zones",db));}[m
[32m+[m[32m    mk("lag","Lag Machine",l,lr);[m
[32m+[m
[32m+[m[32m    float co=0; std::vector<std::string> cr;[m
[32m+[m[32m    if(colorT>20){co+=20;cr.push_back(fmt::format("{} color triggers",colorT));}[m
[32m+[m[32m    if(alphaT>10){co+=20;cr.push_back(fmt::format("{} alpha triggers",alphaT));}[m
[32m+[m[32m    if(pulseT>20){co+=10;cr.push_back("Heavy visual triggers");}[m
[32m+[m[32m    mk("color","Color Tricks",co,cr);[m
[32m+[m
[32m+[m[32m    float cm=0; std::vector<std::string> cmr;[m
[32m+[m[32m    if(camOff>0){cm+=std::min(35.f,camOff*5.f);cmr.push_back(fmt::format("{} cam offset",camOff));}[m
[32m+[m[32m    if(camStat>0){cm+=std::min(35.f,camStat*6.f);cmr.push_back(fmt::format("{} cam static",camStat));}[m
[32m+[m[32m    if(moveT>50){cm+=10;cmr.push_back("Heavy move triggers");}[m
[32m+[m[32m    if(highObj>20||lowObj>20){cm+=10;cmr.push_back("Off-screen objects");}[m
[32m+[m[32m    mk("camera","Camera Tricks",cm,cmr);[m
[32m+[m
[32m+[m[32m    float tg=0; std::vector<std::string> tgr;[m
[32m+[m[32m    if(toggleT>10){tg+=20;tgr.push_back(fmt::format("{} toggle triggers",toggleT));}[m
[32m+[m[32m    if(toggleT>40){tg+=20;tgr.push_back("Heavy group visibility");}[m
[32m+[m[32m    mk("toggle","Toggle Reveals",tg,tgr);[m
[32m+[m
[32m+[m[32m    float al=0; std::vector<std::string> alr;[m
[32m+[m[32m    if(alphaT>5){al+=20;alr.push_back(fmt::format("{} alpha triggers",alphaT));}[m
[32m+[m[32m    if(alphaT>20){al+=20;alr.push_back("Possible hidden flashing");}[m
[32m+[m[32m    mk("alpha","Alpha Flashing",al,alr);[m
[32m+[m
[32m+[m[32m    result.scanTimeMs = std::chrono::duration<float,std::milli>(clk::now()-t0).count();[m
     return result;[m
 }[m
 [m
 ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {[m
     ScanResult fail;[m
[31m-    if (!level) {[m
[31m-        fail.error = "No level provided";[m
[31m-        return fail;[m
[31m-    }[m
[31m-[m
[31m-    log::info("Scanning level '{}' (ID: {})", level->m_levelName, level->m_levelID.value());[m
[31m-[m
[31m-    std::string decoded = getDecodedLevelData(level);[m
[31m-[m
[32m+[m[32m    if (!level) { fail.error="No level"; return fail; }[m
[32m+[m[32m    log::info("Scanning '{}' (ID:{})