#pragma once

#include <Geode/Geode.hpp>
#include "utils/ObjectUtils.hpp"
#include <vector>
#include <unordered_map>

using namespace geode::prelude;

// ── One suspicious finding ──────────────────────────────────────
struct SuspiciousRegion {
    cocos2d::CCRect bounds;
    float score = 0;           // 0-1
    std::string reason;
    std::string category;      // "art", "color_trick", "lag_machine", "camera_trick", "toggle_reveal", "alpha_flash"
};

// ── Per-category score (what the UI displays) ───────────────────
struct CategoryScore {
    std::string name;          // display name
    std::string id;            // internal id
    float percent = 0;         // 0-100
    cocos2d::ccColor3B color;
    std::vector<std::string> reasons; // individual findings
};

// ── Full scan result ────────────────────────────────────────────
struct ScanResult {
    std::vector<CategoryScore> categories;
    float totalPercent = 0;       // sum of all categories (can exceed 100)
    int   objectsScanned = 0;
    float scanTimeMs = 0;
    std::vector<SuspiciousRegion> allRegions;
};

// ── Main detector singleton ─────────────────────────────────────
class NSFWDetector {
public:
    static NSFWDetector* get();

    ScanResult scanLevel(GJGameLevel* level);
    ScanResult scanFromObjects(const std::vector<ParsedObject>& objects);

    void setSensitivity(int s) { m_sens = std::clamp(s, 0, 100); }
    int  sensitivity() const   { return m_sens; }

    static cocos2d::ccColor3B colorForPercent(float p);

private:
    NSFWDetector() = default;
    int m_sens = 50;
    static NSFWDetector* s_inst;
};
