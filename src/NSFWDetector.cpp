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

std::string NSFWDetector::getDecodedLevelData(GJGameLevel* level) {
    if (!level) return "";

    std::string raw = level->m_levelString;

    if (raw.empty()) {
        log::warn("Level string is empty for '{}'", level->m_levelName);
        return "";
    }

    log::info("Raw level string size: {}", raw.size());

    // Check if already decompressed (contains commas and semicolons = raw object data)
    if (raw.size() > 10 && raw.find(';') != std::string::npos && raw.find(',') < 50) {
        log::info("Level data appears already decompressed, size: {}", raw.size());
        return raw;
    }

    // Try base64 decode + gzip inflate
    try {
        // Use cocos2d's ZipUtils which handles the full GD level decompression
        // GD levels are typically base64 + gzip
        // ZipUtils::decompressString2 handles this exact case

        bool needsFree = false;
        unsigned char* decoded = nullptr;
        int decodedLen = 0;

        // Try the Geode/cocos utility for base64
        std::string b64 = raw;

        // GD uses URL-safe base64, replace chars
        for (auto& c : b64) {
            if (c == '-') c = '+';
            if (c == '_') c = '/';
        }

        // Pad if needed
        while (b64.size() % 4 != 0) b64 += '=';

        // Manual base64 decode using cocos
        decodedLen = cocos2d::base64Decode(
            (const unsigned char*)b64.c_str(),
            (unsigned int)b64.size(),
            &decoded
        );

        if (decoded && decodedLen > 0) {
            // Try gzip decompress
            unsigned char* inflated = nullptr;
            auto inflatedLen = ZipUtils::ccInflateMemory(decoded, decodedLen, &inflated);

            free(decoded);

            if (inflated && inflatedLen > 0) {
                std::string result((char*)inflated, inflatedLen);
                free(inflated);
                log::info("Decompressed via base64+gzip: {} bytes", result.size());
                return result;
            }
            if (inflated) free(inflated);
        }
        if (decoded) free(decoded);
    } catch (...) {
        log::warn("Exception during base64+gzip decompression");
    }

    // Fallback: try ZipUtils directly on the raw data
    try {
        unsigned char* inflated = nullptr;
        auto inflatedLen = ZipUtils::ccInflateMemory(
            (unsigned char*)raw.c_str(),
            (unsigned int)raw.size(),
            &inflated
        );

        if (inflated && inflatedLen > 0) {
            std::string result((char*)inflated, inflatedLen);
            free(inflated);
            log::info("Decompressed via raw inflate: {} bytes", result.size());
            return result;
        }
        if (inflated) free(inflated);
    } catch (...) {
        log::warn("Raw inflate failed too");
    }

    // Last resort: if it has semicolons, maybe it's usable as-is
    if (raw.find(';') != std::string::npos) {
        log::info("Using raw data as-is (has semicolons)");
        return raw;
    }

    log::warn("Could not decode level data at all");
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

        std::istringstream objStream(objStr);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(objStream, token, ',')) {
            tokens.push_back(token);
        }

        for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
            int key = 0;
            try { key = std::stoi(tokens[i]); } catch (...) { continue; }
            auto const& val = tokens[i + 1];

            try {
                switch (key) {
                    case 1: obj.id = std::stoi(val); break;
                    case 2: obj.x = std::stof(val); break;
                    case 3: obj.y = std::stof(val); break;
                }
            } catch (...) {}
        }

        if (obj.id > 0) {
            objects.push_back(obj);
        }
    }

    return objects;
}

ScanResult NSFWDetector::analyzeDecodedData(std::string const& data) {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    ScanResult result;
    result.dataAvailable = true;

    auto objects = parseObjects(data);
    result.objectsScanned = (int)objects.size();

    if (objects.empty()) {
        result.error = "Parsed 0 objects from level data";
        auto t1 = high_resolution_clock::now();
        result.scanTimeMs = duration<float, std::milli>(t1 - t0).count();
        return result;
    }

    float sens = m_sensitivity / 50.f;

    int colorTriggers = 0, pulseTriggers = 0, alphaTriggers = 0;
    int moveTriggers = 0, toggleTriggers = 0, spawnTriggers = 0;
    int cameraOffset = 0, cameraStatic = 0, editObject = 0;
    int totalTriggers = 0;
    int highObjects = 0, lowObjects = 0;

    std::unordered_map<int, int> triggerBins;

    for (auto const& obj : objects) {
        if (obj.y > 1500.f) highObjects++;
        if (obj.y < -500.f) lowObjects++;

        bool isTrigger = false;
        switch (obj.id) {
            case 899: colorTriggers++; isTrigger = true; break;
            case 901: moveTriggers++; isTrigger = true; break;
            case 1006: pulseTriggers++; isTrigger = true; break;
            case 1007: alphaTriggers++; isTrigger = true; break;
            case 1049: toggleTriggers++; isTrigger = true; break;
            case 1268: spawnTriggers++; isTrigger = true; break;
            case 1916: cameraOffset++; isTrigger = true; break;
            case 2062: cameraStatic++; isTrigger = true; break;
            case 3600: editObject++; isTrigger = true; break;
            case 1346: case 1347: case 1520: case 1585:
            case 1595: case 1611: case 1811: case 1815: case 1817:
                isTrigger = true; break;
        }
        if (isTrigger) {
            totalTriggers++;
            triggerBins[(int)(obj.x / 60.f)]++;
        }
    }

    auto makeCategory = [&](std::string id, std::string name, float p, std::vector<std::string> reasons) {
        CategoryScore c;
        c.id = id;
        c.name = name;
        c.percent = std::clamp(p * sens, 0.f, 100.f);
        c.color = colorForPercent(c.percent);
        c.reasons = std::move(reasons);
        result.totalPercent += c.percent;
        result.categories.push_back(c);
    };

    float artPct = 0.f;
    std::vector<std::string> artR;
    if (result.objectsScanned > 1500) { artPct += 10; artR.push_back("Large object count"); }
    if (result.objectsScanned > 5000) { artPct += 15; artR.push_back("Very large object count"); }
    if (highObjects > 20) { artPct += 20; artR.push_back(fmt::format("{} objects very high", highObjects)); }
    if (lowObjects > 20) { artPct += 20; artR.push_back(fmt::format("{} objects very low", lowObjects)); }
    if (editObject > 10) { artPct += 10; artR.push_back("Edit object triggers"); }
    makeCategory("art", "Art Detection", artPct, artR);

    float lagPct = 0.f;
    std::vector<std::string> lagR;
    if (spawnTriggers > 30) { lagPct += 30; lagR.push_back(fmt::format("{} spawn triggers", spawnTriggers)); }
    if (pulseTriggers > 40) { lagPct += 20; lagR.push_back(fmt::format("{} pulse triggers", pulseTriggers)); }
    if (totalTriggers > 200) { lagPct += 25; lagR.push_back(fmt::format("{} total triggers", totalTriggers)); }
    int denseBins = 0;
    for (auto const& [b, c] : triggerBins) if (c >= 40) denseBins++;
    if (denseBins > 0) { lagPct += std::min(25.f, denseBins * 8.f); lagR.push_back(fmt::format("{} dense trigger zones", denseBins)); }
    makeCategory("lag", "Lag Machine", lagPct, lagR);

    float colPct = 0.f;
    std::vector<std::string> colR;
    if (colorTriggers > 20) { colPct += 20; colR.push_back(fmt::format("{} color triggers", colorTriggers)); }
    if (alphaTriggers > 10) { colPct += 20; colR.push_back(fmt::format("{} alpha triggers", alphaTriggers)); }
    if (pulseTriggers > 20) { colPct += 10; colR.push_back("Heavy visual triggers"); }
    makeCategory("color", "Color Tricks", colPct, colR);

    float camPct = 0.f;
    std::vector<std::string> camR;
    if (cameraOffset > 0) { camPct += std::min(35.f, cameraOffset * 5.f); camR.push_back(fmt::format("{} camera offset", cameraOffset)); }
    if (cameraStatic > 0) { camPct += std::min(35.f, cameraStatic * 6.f); camR.push_back(fmt::format("{} camera static", cameraStatic)); }
    if (moveTriggers > 50) { camPct += 10; camR.push_back("Heavy move triggers"); }
    if (highObjects > 20 || lowObjects > 20) { camPct += 10; camR.push_back("Off-screen objects"); }
    makeCategory("camera", "Camera Tricks", camPct, camR);

    float togPct = 0.f;
    std::vector<std::string> togR;
    if (toggleTriggers > 10) { togPct += 20; togR.push_back(fmt::format("{} toggle triggers", toggleTriggers)); }
    if (toggleTriggers > 40) { togPct += 20; togR.push_back("Heavy group visibility"); }
    makeCategory("toggle", "Toggle Reveals", togPct, togR);

    float alpPct = 0.f;
    std::vector<std::string> alpR;
    if (alphaTriggers > 5) { alpPct += 20; alpR.push_back(fmt::format("{} alpha triggers", alphaTriggers)); }
    if (alphaTriggers > 20) { alpPct += 20; alpR.push_back("Possible hidden flashing"); }
    makeCategory("alpha", "Alpha Flashing", alpPct, alpR);

    auto t1 = high_resolution_clock::now();
    result.scanTimeMs = duration<float, std::milli>(t1 - t0).count();
    return result;
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    ScanResult fail;
    if (!level) {
        fail.error = "No level provided";
        return fail;
    }

    log::info("Scanning level '{}' (ID: {})", level->m_levelName, level->m_levelID.value());

    std::string decoded = getDecodedLevelData(level);

    if (decoded.empty()) {
        fail.error = "Could not load level data.\n\nMake sure the level is downloaded.\nTry opening the level once, then go back and scan again.";
        return fail;
    }

    return analyzeDecodedData(decoded);
}
