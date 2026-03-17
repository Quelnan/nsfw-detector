#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "NSFWDetector.hpp"
#include "ui/ScanPopup.hpp"

using namespace geode::prelude;

class $modify(NSFWLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "gj_findBtn_001.png",
            1.f,
            CircleBaseColor::Green,
            CircleBaseSize::MediumAlt
        );

        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(NSFWLevelInfoLayer::onScan)
        );
        btn->setID("nsfw-scan-btn"_spr);

        if (auto leftSide = this->getChildByID("left-side-menu")) {
            static_cast<CCMenu*>(leftSide)->addChild(btn);
            leftSide->updateLayout();
        }
        else {
            auto menu = CCMenu::create();
            menu->setPosition({35.f, 130.f});
            btn->setPosition({0.f, 0.f});
            menu->addChild(btn);
            this->addChild(menu, 10);
        }

        return true;
    }

    void onScan(CCObject*) {
        if (!m_level) return;
        if (auto popup = ScanPopup::create(m_level)) {
            popup->show();
        }
    }
};

class $modify(NSFWPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!Mod::get()->getSettingValue<bool>("auto-scan")) return true;

        int threshold = Mod::get()->getSettingValue<int64_t>("warn-threshold");

        Loader::get()->queueInMainThread([level, threshold]() {
            auto result = NSFWDetector::get()->scanLevel(level);

            float maxPct = 0.f;
            std::string maxName = "None";
            for (auto const& c : result.categories) {
                if (c.percent > maxPct) {
                    maxPct = c.percent;
                    maxName = c.name;
                }
            }

            if (maxPct >= threshold) {
                FLAlertLayer::create(
                    "NSFW Warning",
                    fmt::format(
                        "<cr>Suspicious level detected</c>\n\nHighest flag: <cy>{}</c> at <cr>{:.0f}%</c>\nTotal: <co>{:.0f}%</c>",
                        maxName, maxPct, result.totalPercent
                    ),
                    "OK"
                )->show();
            }
        });

        return true;
    }
};

$on_mod(Loaded) {
    log::info("NSFW Detector loaded");
}
