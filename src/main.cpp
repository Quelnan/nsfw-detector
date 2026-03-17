#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "NSFWDetector.hpp"

using namespace geode::prelude;

static std::string buildScanText(ScanResult const& result) {
    if (!result.error.empty()) {
        return result.error;
    }

    if (result.objectsScanned == 0) {
        return "No objects found in level data.\n\nThe level might not be downloaded yet.\nTry opening it once first.";
    }

    std::string text;

    for (auto const& c : result.categories) {
        text += fmt::format("{}: <cy>{:.0f}%</c>\n", c.name, c.percent);
    }

    text += fmt::format("\nTotal: <co>{:.0f}%</c>", result.totalPercent);

    float maxPct = 0.f;
    for (auto const& c : result.categories) {
        maxPct = std::max(maxPct, c.percent);
    }

    if (maxPct >= 65.f) {
        text += "\n\n<cr>Likely hidden NSFW content!</c>";
    }
    else if (maxPct >= 40.f) {
        text += "\n\n<cy>Suspicious - check before streaming</c>";
    }
    else if (maxPct >= 20.f) {
        text += "\n\n<cg>Minor flags - probably fine</c>";
    }
    else {
        text += "\n\n<cg>Looks clean!</c>";
    }

    text += fmt::format("\n\n<cs>{} objects | {:.1f}ms</c>", result.objectsScanned, result.scanTimeMs);
    return text;
}

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

        auto result = NSFWDetector::get()->scanLevel(m_level);

        FLAlertLayer::create(
            "NSFW Scanner",
            buildScanText(result),
            "OK"
        )->show();
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
            std::string maxName;
            for (auto const& c : result.categories) {
                if (c.percent > maxPct) {
                    maxPct = c.percent;
                    maxName = c.name;
                }
            }

            if (maxPct >= threshold) {
                FLAlertLayer::create(
                    "NSFW Warning",
                    buildScanText(result),
                    "OK"
                )->show();
            }
        });

        return true;
    }
};

$on_mod(Loaded) {
    log::info("NSFW Detector v1.2 loaded");
}
