#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "NSFWDetector.hpp"
#include "ui/ScanPopup.hpp"

using namespace geode::prelude;

// ═════════════════════════════════════════════════════════════════
//  Add "Scan" button to the level info page
// ═════════════════════════════════════════════════════════════════
class $modify(ScanLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        // Build the scan button
        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "gj_findBtn_001.png", 1.f,
            CircleBaseColor::Green, CircleBaseSize::MediumAlt);

        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ScanLevelInfo::onNSFWScan));
        btn->setID("nsfw-scan-btn"_spr);

        // Try adding to the left-side menu, otherwise make our own
        if (auto menu = this->getChildByID("left-side-menu")) {
            static_cast<CCMenu*>(menu)->addChild(btn);
            menu->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition({35, 130});
            btn->setPosition({0, 0});
            menu->addChild(btn);
            addChild(menu, 10);
        }

        return true;
    }

    void onNSFWScan(CCObject*) {
        if (m_level) {
            if (auto popup = ScanPopup::create(m_level))
                popup->show();
        }
    }
};

// ═════════════════════════════════════════════════════════════════
//  Optional auto-scan when playing a level
// ═════════════════════════════════════════════════════════════════
class $modify(ScanPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        if (!Mod::get()->getSettingValue<bool>("auto-scan"))
            return true;

        int threshold = Mod::get()->getSettingValue<int64_t>("warn-threshold");

        Loader::get()->queueInMainThread([this, level, threshold]() {
            auto result = NSFWDetector::get()->scanLevel(level);

            // Find max category
            float maxPct = 0;
            std::string maxCat;
            for (auto& c : result.categories) {
                if (c.percent > maxPct) { maxPct = c.percent; maxCat = c.name; }
            }

            if (maxPct >= threshold) {
                FLAlertLayer::create(
                    "NSFW Warning",
                    fmt::format(
                        "<cr>Suspicious level detected!</c>\n\n"
                        "Highest flag: <cy>{}</c> at <cr>{:.0f}%</c>\n"
                        "Total: <co>{:.0f}%</c>\n\n"
                        "Consider checking before streaming.",
                        maxCat, maxPct, result.totalPercent),
                    "OK"
                )->show();
            } else if (maxPct > 20) {
                Notification::create(
                    fmt::format("Scan: {} {:.0f}%", maxCat, maxPct),
                    NotificationIcon::Warning, 3.f)->show();
            } else {
                Notification::create("Level scan: Clean",
                    NotificationIcon::Success, 2.f)->show();
            }
        });

        return true;
    }
};

$on_mod(Loaded) {
    log::info("NSFW Level Detector loaded");
}
