#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "NSFWDetector.hpp"

using namespace geode::prelude;

static std::string buildText(ScanResult const& r) {
    if (!r.error.empty()) return r.error;
    if (r.objectsScanned == 0) return "No objects found.";

    std::string t;
    for (auto& c : r.categories) {
        t += fmt::format("{}: <cy>{:.0f}%</c>", c.name, c.percent);
        if (!c.reasons.empty()) {
            t += " (";
            for (size_t i = 0; i < c.reasons.size(); i++) {
                if (i > 0) t += ", ";
                t += c.reasons[i];
            }
            t += ")";
        }
        t += "\n";
    }

    t += fmt::format("\nTotal: <co>{:.0f}%</c>", r.totalPercent);

    float mx = 0;
    for (auto& c : r.categories) mx = std::max(mx, c.percent);

    if (mx >= 50.f) t += "\n\n<cr>Likely hidden NSFW content!</c>";
    else if (mx >= 30.f) t += "\n\n<cy>Suspicious - check before streaming</c>";
    else if (mx >= 15.f) t += "\n\n<cg>Minor flags - probably fine</c>";
    else t += "\n\n<cg>Looks clean!</c>";

    t += fmt::format("\n\n<cs>{} objects | {:.1f}ms</c>", r.objectsScanned, r.scanTimeMs);
    return t;
}

class $modify(NSFWLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "gj_findBtn_001.png", 1.f, CircleBaseColor::Green, CircleBaseSize::MediumAlt);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(NSFWLevelInfo::onScan));
        btn->setID("nsfw-scan-btn"_spr);

        if (auto m = this->getChildByID("left-side-menu")) {
            static_cast<CCMenu*>(m)->addChild(btn);
            m->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition({35.f, 130.f});
            btn->setPosition({0, 0});
            menu->addChild(btn);
            addChild(menu, 10);
        }
        return true;
    }

    void onScan(CCObject*) {
        if (!m_level) return;
        auto r = NSFWDetector::get()->scanLevel(m_level);
        FLAlertLayer::create("NSFW Scanner", buildText(r), "OK")->show();
    }
};

class $modify(NSFWPlay, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreate) {
        if (!PlayLayer::init(level, useReplay, dontCreate)) return false;
        if (!Mod::get()->getSettingValue<bool>("auto-scan")) return true;

        int thresh = Mod::get()->getSettingValue<int64_t>("warn-threshold");
        Loader::get()->queueInMainThread([level, thresh]() {
            auto r = NSFWDetector::get()->scanLevel(level);
            float mx = 0;
            for (auto& c : r.categories) mx = std::max(mx, c.percent);
            if (mx >= thresh)
                FLAlertLayer::create("NSFW Warning", buildText(r), "OK")->show();
        });
        return true;
    }
};

$on_mod(Loaded) { log::info("NSFW Detector v1.3 loaded"); }
