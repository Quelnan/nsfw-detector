#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "../NSFWDetector.hpp"

using namespace geode::prelude;

class ScanPopup : public Popup<GJGameLevel*> {
protected:
    GJGameLevel* m_level = nullptr;
    ScanResult   m_result;
    bool m_scanned = false;

    // UI elements
    CCLabelBMFont*        m_titleLabel   = nullptr;
    CCLabelBMFont*        m_statusLabel  = nullptr;
    CCMenuItemSpriteExtra* m_scanBtn     = nullptr;
    CCNode*               m_resultBox    = nullptr;

    bool setup(GJGameLevel* level) override;
    void onScan(CCObject*);
    void buildResultUI();

    // Build a single category bar row
    CCNode* createCategoryRow(const CategoryScore& cat, float width, float rowHeight);

public:
    static ScanPopup* create(GJGameLevel* level);
};
