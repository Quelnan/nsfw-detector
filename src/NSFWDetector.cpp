#include "NSFWDetector.hpp"
#include <chrono>
#include <sstream>
#include <algorithm>

NSFWDetector* NSFWDetector::s_instance = nullptr;

NSFWDetector* NSFWDetector::get() {
    if (!s_instance) {
        s_instance = new NSFWDetector();
        s_instance->m_sensitivity = Mod::get()->getSettingValue<int64_t>("sensitivity");
    }
    return s_instance;
}

void NSFWDetector::setSensitivity(int s) {
    m_sensitivity = std::clamp(s, 0, 100);
}

int NSFWDetector::getSensitivity() const {
    return m_sensitivity;
}

cocos2d::ccColor3B NSFWDetector::colorForPercent(float p) {
    if (p < 20.f) return {0, 255, 80};
    if (p < 50.f) return {255, 220, 0};
    if (p < 75.f) return {255, 140, 0};
    return {255, 60, 60};
}

static int countSubstr(std::string const& s, std::string const& sub) {
    if (sub.empty()) return 0;
    int c = 0;
    size_t pos = 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) {
        ++c;
        pos += sub.size();
    }
    return c;
}

ScanResult NSFWDetector::scanLevelString(std::string const& levelString) {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    ScanResult result;
    result.objectsScanned = std::max(0, countSubstr(levelString, ";") - 1);

    float sens = m_sensitivity / 50.f;

    // These are intentionally heuristic / lightweight for now.
    int colorTriggers = countSubstr(levelString, "1,899,");
    int pulseTriggers = countSubstr(levelString, "1,1006,");
    int alphaTriggers = countSubstr(levelString, "1,1007,");
    int moveTriggers = countSubstr(levelString, "1,901,");
    int toggleTriggers = countSubstr(levelString, "1,1049,");
    int spawnTriggers = countSubstr(levelString, "1,1268,");
    int cameraOffset = countSubstr(levelString, "1,1916,");
    int cameraStatic = countSubstr(levelString, "1,2062,");
    int editObject = countSubstr(levelString, "1,3600,");

    int totalTriggers = colorTriggers + pulseTriggers + alphaTriggers + moveTriggers +
                        toggleTriggers + spawnTriggers + cameraOffset + cameraStatic + editObject;

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

    // Art detection
    float artPct = 0.f;
    std::vector<std::string> artReasons;
    if (result.objectsScanned > 1500) {
        artPct += 10.f;
        artReasons.push_back("Large object count");
    }
    if (result.objectsScanned > 5000) {
        artPct += 15.f;
        artReasons.push_back("Very large object count");
    }
    if (editObject > 10) {
        artPct += 10.f;
        artReasons.push_back("Uses edit object triggers");
    }
    makeCategory("art", "Art Detection", artPct, artReasons);

    // Lag machine
    float lagPct = 0.f;
    std::vector<std::string> lagReasons;
    if (spawnTriggers > 30) {
        lagPct += 30.f;
        lagReasons.push_back(fmt::format("{} spawn triggers", spawnTriggers));
    }
    if (pulseTriggers > 40) {
        lagPct += 20.f;
        lagReasons.push_back(fmt::format("{} pulse triggers", pulseTriggers));
    }
    if (totalTriggers > 200) {
        lagPct += 25.f;
        lagReasons.push_back(fmt::format("{} total triggers", totalTriggers));
    }
    if (totalTriggers > 500) {
        lagPct += 25.f;
        lagReasons.push_back("Extreme trigger density");
    }
    makeCategory("lag", "Lag Machine", lagPct, lagReasons);

    // Color tricks
    float colorPct = 0.f;
    std::vector<std::string> colorReasons;
    if (colorTriggers > 20) {
        colorPct += 20.f;
        colorReasons.push_back(fmt::format("{} color triggers", colorTriggers));
    }
    if (alphaTriggers > 10) {
        colorPct += 20.f;
        colorReasons.push_back(fmt::format("{} alpha triggers", alphaTriggers));
    }
    if (pulseTriggers > 20) {
        colorPct += 10.f;
        colorReasons.push_back("Heavy visual trigger usage");
    }
    makeCategory("color", "Color Tricks", colorPct, colorReasons);

    // Camera tricks
    float camPct = 0.f;
    std::vector<std::string> camReasons;
    if (cameraOffset > 0) {
        camPct += std::min(35.f, cameraOffset * 5.f);
        camReasons.push_back(fmt::format("{} camera offset triggers", cameraOffset));
    }
    if (cameraStatic > 0) {
        camPct += std::min(35.f, cameraStatic * 6.f);
        camReasons.push_back(fmt::format("{} camera static triggers", cameraStatic));
    }
    if (moveTriggers > 50) {
        camPct += 10.f;
        camReasons.push_back("Heavy move trigger usage");
    }
    makeCategory("camera", "Camera Tricks", camPct, camReasons);

    // Toggle reveals
    float togglePct = 0.f;
    std::vector<std::string> toggleReasons;
    if (toggleTriggers > 10) {
        togglePct += 20.f;
        toggleReasons.push_back(fmt::format("{} toggle triggers", toggleTriggers));
    }
    if (toggleTriggers > 40) {
        togglePct += 20.f;
        toggleReasons.push_back("Heavy group visibility control");
    }
    makeCategory("toggle", "Toggle Reveals", togglePct, toggleReasons);

    // Alpha flashing
    float alphaPct = 0.f;
    std::vector<std::string> alphaReasons;
    if (alphaTriggers > 5) {
        alphaPct += 20.f;
        alphaReasons.push_back(fmt::format("{} alpha triggers", alphaTriggers));
    }
    if (alphaTriggers > 20) {
        alphaPct += 20.f;
        alphaReasons.push_back("Possible flashing / hidden reveals");
    }
    makeCategory("alpha", "Alpha Flashing", alphaPct, alphaReasons);

    auto t1 = high_resolution_clock::now();
    result.scanTimeMs = duration<float, std::milli>(t1 - t0).count();
    return result;
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    if (!level) return {};
    return scanLevelString(level->m_levelString);
}
