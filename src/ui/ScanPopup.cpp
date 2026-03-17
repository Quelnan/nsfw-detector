#include "ScanPopup.hpp"
#include <algorithm>

ScanPopup* ScanPopup::create(GJGameLevel* level) {
    auto ret = new ScanPopup();
    if (ret && ret->initAnchored(400.f, 310.f, level)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ScanPopup::setup(GJGameLevel* level) {
    m_level = level;
    setTitle("NSFW Scanner");

    auto sz = m_mainLayer->getContentSize();

    // Level name
    auto name = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
    name->setScale(0.45f);
    name->setPosition({sz.width/2, sz.height - 48});
    m_mainLayer->addChild(name);

    // Level ID
    auto idStr = fmt::format("ID: {}", level->m_levelID.value());
    auto idLbl = CCLabelBMFont::create(idStr.c_str(), "chatFont.fnt");
    idLbl->setScale(0.6f);
    idLbl->setPosition({sz.width/2, sz.height - 64});
    idLbl->setColor({160,160,160});
    m_mainLayer->addChild(idLbl);

    // Status
    m_statusLabel = CCLabelBMFont::create("Press Scan to analyze", "chatFont.fnt");
    m_statusLabel->setScale(0.55f);
    m_statusLabel->setPosition({sz.width/2, sz.height/2});
    m_mainLayer->addChild(m_statusLabel);

    // Result container (hidden until scan)
    m_resultBox = CCNode::create();
    m_resultBox->setPosition({0, 0});
    m_resultBox->setVisible(false);
    m_mainLayer->addChild(m_resultBox);

    // Scan button
    auto scanSpr = ButtonSprite::create("Scan", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    m_scanBtn = CCMenuItemSpriteExtra::create(
        scanSpr, this, menu_selector(ScanPopup::onScan));
    m_scanBtn->setPosition({sz.width/2, 40});
    m_buttonMenu->addChild(m_scanBtn);

    return true;
}

void ScanPopup::onScan(CCObject*) {
    m_statusLabel->setString("Scanning...");
    m_statusLabel->setColor({255,255,100});
    m_resultBox->setVisible(false);
    m_resultBox->removeAllChildren();

    auto lvl = m_level;
    Loader::get()->queueInMainThread([this, lvl]() {
        m_result  = NSFWDetector::get()->scanLevel(lvl);
        m_scanned = true;
        buildResultUI();
    });
}

// ── Helpers ─────────────────────────────────────────────────────

CCNode* ScanPopup::createCategoryRow(const CategoryScore& cat, float width, float rowH) {
    auto row = CCNode::create();
    row->setContentSize({width, rowH});
    row->setAnchorPoint({0, 0.5f});

    // Category name
    auto label = CCLabelBMFont::create(cat.name.c_str(), "bigFont.fnt");
    label->setScale(0.3f);
    label->setAnchorPoint({0, 0.5f});
    label->setPosition({4, rowH/2});
    row->addChild(label);

    // Background bar
    float barX     = 150.f;
    float barW     = width - barX - 60.f;
    float barH     = rowH - 6.f;

    auto barBg = CCLayerColor::create({40, 40, 40, 180}, barW, barH);
    barBg->setPosition({barX, (rowH - barH) / 2});
    row->addChild(barBg);

    // Filled portion
    float fillW = barW * std::clamp(cat.percent / 100.f, 0.f, 1.f);
    if (fillW > 1.f) {
        auto barFill = CCLayerColor::create(
            {cat.color.r, cat.color.g, cat.color.b, 200}, fillW, barH);
        barFill->setPosition({barX, (rowH - barH) / 2});
        row->addChild(barFill);
    }

    // Percentage text
    auto pctStr = fmt::format("{:.0f}%", cat.percent);
    auto pctLbl = CCLabelBMFont::create(pctStr.c_str(), "bigFont.fnt");
    pctLbl->setScale(0.3f);
    pctLbl->setAnchorPoint({1, 0.5f});
    pctLbl->setPosition({width - 4, rowH/2});
    pctLbl->setColor(cat.color);
    row->addChild(pctLbl);

    return row;
}

void ScanPopup::buildResultUI() {
    auto sz = m_mainLayer->getContentSize();

    // Update status
    m_statusLabel->setString(
        fmt::format("{} objects | {:.1f}ms",
            m_result.objectsScanned, m_result.scanTimeMs).c_str());
    m_statusLabel->setColor({180,180,180});
    m_statusLabel->setPosition({sz.width/2, sz.height - 78});
    m_statusLabel->setScale(0.45f);

    m_resultBox->setVisible(true);
    m_resultBox->removeAllChildren();

    float contentW = sz.width - 40;
    float rowH     = 26.f;
    float startY   = sz.height - 95;
    float y        = startY;

    // ── Category rows (sorted by score descending) ──────────────
    auto cats = m_result.categories;
    std::sort(cats.begin(), cats.end(),
        [](auto& a, auto& b){ return a.percent > b.percent; });

    for (auto& cat : cats) {
        auto row = createCategoryRow(cat, contentW, rowH);
        row->setPosition({20, y - rowH/2 - rowH});
        m_resultBox->addChild(row);
        y -= rowH + 3;
    }

    // ── Separator line ──────────────────────────────────────────
    y -= 6;
    auto sep = CCLayerColor::create({255,255,255,80}, contentW, 1);
    sep->setPosition({20, y});
    m_resultBox->addChild(sep);
    y -= 8;

    // ── Total ───────────────────────────────────────────────────
    auto totalColor = NSFWDetector::colorForPercent(m_result.totalPercent / (float)cats.size());
    auto totalStr = fmt::format("Total: {:.0f}%", m_result.totalPercent);
    auto totalLbl = CCLabelBMFont::create(totalStr.c_str(), "bigFont.fnt");
    totalLbl->setScale(0.45f);
    totalLbl->setPosition({sz.width/2, y - 5});
    totalLbl->setColor(totalColor);
    m_resultBox->addChild(totalLbl);
    y -= 22;

    // ── Verdict ─────────────────────────────────────────────────
    std::string verdict;
    cocos2d::ccColor3B verdictCol;

    float avgPct = m_result.totalPercent / std::max((int)cats.size(), 1);
    float maxPct = 0;
    for (auto& c : cats) maxPct = std::max(maxPct, c.percent);

    if (maxPct < 15) {
        verdict = "Looks clean!";
        verdictCol = {0, 255, 80};
    } else if (maxPct < 40) {
        verdict = "Probably fine, minor flags";
        verdictCol = {200, 255, 0};
    } else if (maxPct < 65) {
        verdict = "Suspicious - check before streaming";
        verdictCol = {255, 200, 0};
    } else {
        verdict = "Likely hidden NSFW content!";
        verdictCol = {255, 50, 50};
    }

    auto verdictLbl = CCLabelBMFont::create(verdict.c_str(), "goldFont.fnt");
    verdictLbl->setScale(0.5f);
    verdictLbl->setPosition({sz.width/2, y - 5});
    verdictLbl->setColor(verdictCol);
    m_resultBox->addChild(verdictLbl);

    // ── Change button text ──────────────────────────────────────
    if (auto spr = static_cast<ButtonSprite*>(m_scanBtn->getChildren()->objectAtIndex(0)))
        spr->setString("Re-scan");
}
