#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "../NSFWDetector.hpp"

using namespace geode::prelude;

class ScanPopup : public geode::Popup<GJGameLevel*> {
protected:
    GJGameLevel* m_level = nullptr;
    ScanResult m_result;

    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_resultBox = nullptr;
    CCMenuItemSpriteExtra* m_scanBtn = nullptr;

    bool setup(GJGameLevel* level) override;
    void onScan(CCObject*);
    void buildResultUI();

    cocos2d::CCNode* createCategoryRow(const CategoryScore& cat, float width, float rowHeight);

public:
    static ScanPopup* create(GJGameLevel* level);
};
