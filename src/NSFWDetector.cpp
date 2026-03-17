#include "NSFWDetector.hpp"
#include "analyzers/ColorTrickAnalyzer.hpp"
#include "analyzers/LagMachineAnalyzer.hpp"
#include "analyzers/CameraTrickAnalyzer.hpp"
#include "analyzers/ArtClusterAnalyzer.hpp"
#include "analyzers/ToggleRevealAnalyzer.hpp"
#include "analyzers/AlphaFlashAnalyzer.hpp"
#include <chrono>

NSFWDetector* NSFWDetector::s_inst = nullptr;

NSFWDetector* NSFWDetector::get() {
    if (!s_inst) {
        s_inst = new NSFWDetector();
        s_inst->m_sens = Mod::get()->getSettingValue<int64_t>("sensitivity");
    }
    return s_inst;
}

cocos2d::ccColor3B NSFWDetector::colorForPercent(float p) {
    if (p < 20)  return {0, 255, 0};
    if (p < 50)  return {255, 255, 0};
    if (p < 75)  return {255, 140, 0};
    return {255, 40, 40};
}

ScanResult NSFWDetector::scanLevel(GJGameLevel* level) {
    if (!level || level->m_levelString.empty()) {
        ScanResult r;
        r.totalPercent = 0;
        return r;
    }
    auto objects = ObjectUtils::parseLevelString(level->m_levelString);
    return scanFromObjects(objects);
}

ScanResult NSFWDetector::scanFromObjects(const std::vector<ParsedObject>& objects) {
    auto t0 = std::chrono::high_resolution_clock::now();

    float sensMul = m_sens / 50.f;

    // ── Run each analyzer ───────────────────────────────────────
    auto artRegions    = ArtClusterAnalyzer::analyze(objects);
    auto colorRegions  = ColorTrickAnalyzer::analyze(objects);
    auto lagRegions    = LagMachineAnalyzer::analyze(objects);
    auto camRegions    = CameraTrickAnalyzer::analyze(objects);
    auto toggleRegions = ToggleRevealAnalyzer::analyze(objects);
    auto alphaRegions  = AlphaFlashAnalyzer::analyze(objects);

    // ── Build per-category scores ───────────────────────────────
    auto buildCategory = [&](const std::string& name, const std::string& id,
                              std::vector<SuspiciousRegion>& regions) -> CategoryScore {
        CategoryScore cat;
        cat.name = name;
        cat.id   = id;

        if (regions.empty()) {
            cat.percent = 0;
            cat.color   = colorForPercent(0);
            return cat;
        }

        float maxScore = 0;
        float sum = 0;
        for (auto& r : regions) {
            r.score = std::clamp(r.score * sensMul, 0.f, 1.f);
            maxScore = std::max(maxScore, r.score);
            sum += r.score;
            cat.reasons.push_back(r.reason);
        }
        float avg = sum / regions.size();

        // Weighted: 70% max finding, 30% average
        cat.percent = std::clamp((maxScore * 0.7f + avg * 0.3f) * 100.f, 0.f, 100.f);
        cat.color   = colorForPercent(cat.percent);
        return cat;
    };

    ScanResult result;
    result.objectsScanned = (int)objects.size();

    result.categories.push_back(buildCategory("Art Detection",       "art",            artRegions));
    result.categories.push_back(buildCategory("Color Tricks",        "color_trick",    colorRegions));
    result.categories.push_back(buildCategory("Lag Machine",         "lag_machine",    lagRegions));
    result.categories.push_back(buildCategory("Camera Tricks",       "camera_trick",   camRegions));
    result.categories.push_back(buildCategory("Toggle Reveals",      "toggle_reveal",  toggleRegions));
    result.categories.push_back(buildCategory("Alpha Flashing",      "alpha_flash",    alphaRegions));

    // Total = sum (can exceed 100)
    result.totalPercent = 0;
    for (auto& c : result.categories)
        result.totalPercent += c.percent;

    // Collect all regions
    auto addAll = [&](std::vector<SuspiciousRegion>& v) {
        result.allRegions.insert(result.allRegions.end(), v.begin(), v.end());
    };
    addAll(artRegions); addAll(colorRegions); addAll(lagRegions);
    addAll(camRegions); addAll(toggleRegions); addAll(alphaRegions);

    auto t1 = std::chrono::high_resolution_clock::now();
    result.scanTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    return result;
}
