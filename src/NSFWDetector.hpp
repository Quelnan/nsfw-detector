#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

struct CategoryScore {
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
    std::string error;
};

struct LevelObject {
    int id = 0;
    float x = 0, y = 0;
    float scale = 1.f;
    int mainColor = 0, detailColor = 0;
    int targetColor = 0, targetGroup = 0;
    int staticGroup = 0;
    int zLayer = 0, zOrder = 0;
    float moveX = 0, moveY = 0;
    float opacity = 1.f, duration = 0.f;
    std::vector<int> groups;
};

class NSFWDetector {
public:
    static NSFWDetector* get();
    ScanResult scanLevel(GJGameLevel* level);
    static cocos2d::ccColor3B colorForPercent(float p);
private:
    int m_sens = 50;
    static NSFWDetector* s_inst;
    std::string decodeLevelData(GJGameLevel* level);
    std::vector<LevelObject> parseObjects(std::string const& data);
    ScanResult analyze(std::vector<LevelObject> const& objects);
};
