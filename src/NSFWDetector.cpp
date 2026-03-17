#include "NSFWDetector.hpp"
#include <chrono>
#include <algorithm>
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

static bool isTriggerID(int id) {
    switch (id) {
        case 899:   // color
        case 901:   // move
        case 1006:  // pulse
        case 1007:  // alpha
        case 1049:  // toggle
        case 1268:  // spawn
        case 1346:  // rotate
        case 1347:  // follow
        case 1520:  // shake
        case 1585:  // animate
        case 1595:  // touch
        case 1611:  // count
        case 1811:  // instant count
        case 1815:  // collision
        case 1817:  // pickup
        case 1916:  // camera offset
        case 2062:  // camera static
        case 3600:  // edit object
            return true;
        default:
            return false;
    }
}

ScanResult NSFWDetector::scanPlayLayer(PlayLayer* playLayer) {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    ScanResult result;
    if (!playLayer) return result;

    auto objects = playLayer->m_objects;
    if (!objects) return result;

    int totalObjects = 0;
    int totalTriggers = 0;

    int colorTriggers = 0;
    int pulseTriggers = 0;
    int alphaTriggers = 0;
    int moveTriggers = 0;
    int toggleTriggers = 0;
    int spawnTriggers = 0;
    int cameraOffset = 0;
    int cameraStatic = 0;
    int editObject = 0;

    float minY = 999999.f;
    float maxY = -999999.f;
    int highObjects = 0;
    int lowObjects = 0;

    std::unordered_map<int, int> triggerBins;

    for (unsigned i = 0; i < objects->count(); i++) {
        auto obj = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        totalObjects++;

        int id = obj->m_objectID;
        float x = obj->getPositionX();
        float y = obj->getPositionY();

        minY = std::min(minY, y);
        maxY = std::max(maxY, y);

        if (y > 1500.f) highObjects++;
        if (y < -500.f) lowObjects++;

        if (isTriggerID(id)) {
            totalTriggers++;
            triggerBins[(int)(x / 60.f)]++;

            switch (id) {
                case 899: colorTriggers++; break;
                case 901: moveTriggers++; break;
                case 1006: pulseTriggers++; break;
                case 1007: alphaTriggers++; break;
                case 1049: toggleTriggers++; break;
                case 1268: spawnTriggers++; break;
                case 1916: cameraOffset++; break;
                case 2062: cameraStatic++; break;
                case 3600: editObject++; break;
            }
        }
    }

    result.objectsScanned = totalObjects;
    float sens = m_sensitivity / 50.f;

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
    std::vector<std::string> artReasons;
    if (totalObjects > 1500) {
        artPct += 10.f;
        artReasons.push_back("Large object count");
    }
    if (totalObjects > 5000) {
        artPct += 15.f;
        artReasons.push_back("Very large object count");
    }
    if (highObjects > 20) {
        artPct += 20.f;
        artReasons.push_back(fmt::format("{} objects placed very high", highObjects));
    }
    if (lowObjects > 20) {
        artPct += 20.f;
        artReasons.push_back(fmt::format("{} objects placed very low", lowObjects));
    }
    if (editObject > 10) {
        artPct += 10.f;
        artReasons.push_back("Uses edit object triggers");
    }
    makeCategory("art", "Art Detection", artPct, artReasons);

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

    int denseBins = 0;
    for (auto const& [bin, count] : triggerBins) {
        if (count >= 40) denseBins++;
    }
    if (denseBins > 0) {
        lagPct += std::min(25.f, denseBins * 8.f);
        lagReasons.push_back(fmt::format("{} dense trigger zones", denseBins));
    }
    makeCategory("lag", "Lag Machine", lagPct, lagReasons);

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
    if (highObjects > 20 || lowObjects > 20) {
        camPct += 10.f;
        camReasons.push_back("Off-screen object placement");
    }
    makeCategory("camera", "Camera Tricks", camPct, camReasons);

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
