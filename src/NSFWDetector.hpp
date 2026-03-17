#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

using namespace geode::prelude;

struct CategoryScore {
    std::string id;
    std::string name;
    float percent = 0.f;
    cocos2d::ccColor3B color = {255,255,255};
    std::vector<std::string> reasons;
};

struct ScanResult {
    std::vector<CategoryScore> categories;
    float totalPercent = 0.f;
    int objectsScanned = 0;
    float scanTimeMs = 0.f;
    bool dataAvailable = false;
    std::string error;
};

class NSFWDetector {
public:
    static NSFWDetector* get();

    ScanResult scanLevel(GJGameLevel* level);

    static cocos2d::ccColor3B colorForPercent(float p);

private:
    int m_sensitivity = 50;
    static NSFWDetector* s_instance;

    std::string getDecodedLevelData(GJGameLevel* level);
    ScanResult analyzeDecodedData(std::string const& data);
};
