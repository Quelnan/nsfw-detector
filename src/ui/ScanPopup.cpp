#include "ScanPopup.hpp"
#include <algorithm>

ScanPopup* ScanPopup::create(GJGameLevel* level) {
    auto ret = new ScanPopup();
    if (ret && ret->initAnchored(400.f, 320.f, level)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ScanPopup::setup(GJGameLevel* level) {
    m_level = level;
    this->setTitle("NSFW Scanner");

    auto size = m_mainLayer->getContentSize();

    auto levelName = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
    levelName->setScale(0.45f);
    levelName->setPosition({ size.width / 2, size.height - 45.f });
    m_mainLayer->addChild(levelName);

    auto idLabel = CCLabelBMFont::create(fmt::format("ID: {}", level->m_levelID.value()).c_str(), "chatFont.fnt");
    idLabel->setScale(0.6f);
    idLabel->setPosition({ size.width / 2, size.height - 62.f });
    idLabel->setColor({ 160, 160, 160 });
    m_mainLayer->addChild(idLabel);

    m_statusLabel = CCLabelBMFont::create("Press Scan", "chatFont.fnt");
    m_statusLabel->setScale(0.6f);
    m_statusLabel->setPosition({ size.width / 2, size.height - 80.f });
    m_mainLayer->addChild(m_statusLabel);

    m_resultBox = CCNode::create();
    m_mainLayer->addChild(m_resultBox);

    auto scanSpr = ButtonSprite::create("Scan", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    m_scanBtn = CCMenuItemSpriteExtra::create(scanSpr, this, menu_selector(ScanPopup::onScan));
    m_scanBtn->setPosition({ size.width / 2, 35.f });
    m_buttonMenu->addChild(m_scanBtn);

    return true;
}

void ScanPopup::onScan(CCObject*) {
    m_statusLabel->setString("Scanning...");
    m_resultBox->removeAllChildren();

    auto lvl = m_level;
    Loader::get()->queueInMainThread([this, lvl]() {
        m_result = NSFWDetector::get()->scanLevel(lvl);
        buildResultUI();
    });
}

cocos2d::CCNode* ScanPopup::createCategoryRow(const CategoryScore& cat, float width, float rowHeight) {
    auto row = CCNode::create();
    row->setContentSize({ width, rowHeight });

    auto name = CCLabelBMFont::create(cat.name.c_str(), "bigFont.fnt");
    name->setScale(0.28f);
    name->setAnchorPoint({ 0.f, 0.5f });
    name->setPosition({ 0.f, rowHeight / 2 });
    row->addChild(name);

    float barX = 140.f;
    float barW = width - 200.f;
    float barH = rowHeight - 6.f;

    auto bg = CCLayerColor::create({ 40, 40, 40, 180 }, barW, barH);
    bg->setPosition({ barX, 3.f });
    row->addChild(bg);

    float fillW = barW * std::clamp(cat.percent / 100.f, 0.f, 1.f);
    if (fillW > 0) {
        auto fill = CCLayerColor::create({ cat.color.r, cat.color.g, cat.color.b, 220 }, fillW, barH);
        fill->setPosition({ barX, 3.f });
        row->addChild(fill);
    }

    auto pct = CCLabelBMFont::create(fmt::format("{:.0f}%", cat.percent).c_str(), "bigFont.fnt");
    pct->setScale(0.28f);
    pct->setAnchorPoint({ 1.f, 0.5f });
    pct->setPosition({ width - 5.f, rowHeight / 2 });
    pct->setColor(cat.color);
    row->addChild(pct);

    return row;
}

void ScanPopup::buildResultUI() {
    auto size = m_mainLayer->getContentSize();
    m_resultBox->removeAllChildren();

    m_statusLabel->setString(fmt::format("{} objs | {:.1f}ms", m_result.objectsScanned, m_result.scanTimeMs).c_str());
    m_statusLabel->setColor({ 180, 180, 180 });

    auto cats = m_result.categories;
    std::sort(cats.begin(), cats.end(), [](auto const& a, auto const& b) {
        return a.percent > b.percent;
    });

    float y = size.height - 115.f;
    for (auto const& cat : cats) {
        auto row = createCategoryRow(cat, size.width - 30.f, 24.f);
        row->setPosition({ 15.f, y });
        m_resultBox->addChild(row);
        y -= 28.f;
    }

    auto sep = CCLayerColor::create({255,255,255,80}, size.width - 30.f, 1.f);
    sep->setPosition({15.f, y - 4.f});
    m_resultBox->addChild(sep);
    y -= 20.f;

    auto total = CCLabelBMFont::create(fmt::format("Total: {:.0f}%", m_result.totalPercent).c_str(), "bigFont.fnt");
    total->setScale(0.42f);
    total->setPosition({ size.width / 2, y });
    total->setColor(NSFWDetector::colorForPercent(m_result.totalPercent / 6.f));
    m_resultBox->addChild(total);
    y -= 24.f;

    float maxPct = 0.f;
    for (auto const& c : cats) maxPct = std::max(maxPct, c.percent);

    std::string verdict = "Looks clean";
    cocos2d::ccColor3B verdictColor = {0,255,80};
    if (maxPct >= 65.f) {
        verdict = "Likely hidden NSFW content";
        verdictColor = {255,60,60};
    } else if (maxPct >= 40.f) {
        verdict = "Suspicious - check first";
        verdictColor = {255,200,0};
    } else if (maxPct >= 20.f) {
        verdict = "Minor flags found";
        verdictColor = {220,255,0};
    }

    auto verdictLbl = CCLabelBMFont::create(verdict.c_str(), "goldFont.fnt");
    verdictLbl->setScale(0.48f);
    verdictLbl->setPosition({ size.width / 2, y });
    verdictLbl->setColor(verdictColor);
    m_resultBox->addChild(verdictLbl);

    if (auto spr = typeinfo_cast<ButtonSprite*>(m_scanBtn->getNormalImage())) {
        spr->setString("Re-scan");
    }
}
