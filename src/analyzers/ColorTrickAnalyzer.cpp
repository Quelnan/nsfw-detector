#include "ColorTrickAnalyzer.hpp"
#include <unordered_map>
#include <set>
#include <algorithm>

std::vector<SuspiciousRegion> ColorTrickAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    // ── 1. Channel behaviour analysis ───────────────────────────
    // Map: channel -> color triggers targeting it, sorted by X
    std::unordered_map<int, std::vector<const ParsedObject*>> chanTriggers;
    std::unordered_map<int, int> chanObjCount; // non-trigger objects per channel

    for (auto& o : objects) {
        if (GDID::isColorTrigger(o.objectID))
            chanTriggers[o.targetColorChannel].push_back(&o);
        if (!o.isTrigger) {
            if (o.mainColorChannel > 0)   chanObjCount[o.mainColorChannel]++;
            if (o.detailColorChannel > 0) chanObjCount[o.detailColorChannel]++;
        }
    }

    for (auto& [ch, trigs] : chanTriggers) {
        if (chanObjCount[ch] < 8) continue;

        auto sorted = trigs;
        std::sort(sorted.begin(), sorted.end(),
            [](auto* a, auto* b){ return a->x < b->x; });

        bool startsHidden = false;
        float skinScore = 0;
        cocos2d::ccColor3B revealCol = {0,0,0};

        if (!sorted.empty()) {
            auto* first = sorted[0];
            if (first->opacity < 0.1f ||
                (first->targetColor.r < 5 && first->targetColor.g < 5 && first->targetColor.b < 5))
                startsHidden = true;
        }

        for (size_t i = 1; i < sorted.size(); i++) {
            auto* t = sorted[i];
            if (t->opacity > 0.4f) {
                float sk = SkinColor::similarity(t->targetColor);
                if (sk > skinScore) { skinScore = sk; revealCol = t->targetColor; }
            }
        }

        // Also: single color trigger that adds skin-tone to many objects
        if (!startsHidden && sorted.size() >= 1 && chanObjCount[ch] > 20) {
            float sk = SkinColor::similarity(sorted[0]->targetColor);
            if (sk > 0.5f) { startsHidden = true; skinScore = sk; }
        }

        float score = 0;
        std::string reason;

        if (startsHidden && skinScore > 0.5f) {
            score += 0.45f;
            reason += fmt::format("ch{}: hidden->skin-tone reveal ({:.0f}% skin); ", ch, skinScore*100);
        }
        if (chanObjCount[ch] > 40) {
            score += 0.15f;
            reason += fmt::format("{} objs on ch; ", chanObjCount[ch]);
        }

        if (score > 0.15f) {
            SuspiciousRegion r;
            r.score    = std::min(score, 1.f);
            r.reason   = reason;
            r.category = "color_trick";
            // compute bounds of objects on this channel
            float x0=FLT_MAX,y0=FLT_MAX,x1=-FLT_MAX,y1=-FLT_MAX;
            for (auto& o : objects) {
                if (o.isTrigger) continue;
                if (o.mainColorChannel==ch || o.detailColorChannel==ch) {
                    x0=std::min(x0,o.x); y0=std::min(y0,o.y);
                    x1=std::max(x1,o.x); y1=std::max(y1,o.y);
                }
            }
            r.bounds = {x0,y0,x1-x0,y1-y0};
            results.push_back(r);
        }
    }

    // ── 2. Same-position layered colouring ──────────────────────
    struct PK { int x,y; bool operator==(const PK& o) const { return x==o.x && y==o.y; } };
    struct PKH { size_t operator()(const PK& k) const { return std::hash<int>()(k.x)^(std::hash<int>()(k.y)<<16); } };
    std::unordered_map<PK, std::vector<const ParsedObject*>, PKH> posMap;

    for (auto& o : objects) {
        if (o.isTrigger) continue;
        PK k{(int)(o.x/2.f), (int)(o.y/2.f)};
        posMap[k].push_back(&o);
    }

    int stacked = 0;
    for (auto& [pk, objs] : posMap) {
        if (objs.size() < 2) continue;
        std::set<int> chs;
        for (auto* o : objs) {
            if (o->mainColorChannel>0) chs.insert(o->mainColorChannel);
            if (o->detailColorChannel>0) chs.insert(o->detailColorChannel);
        }
        if (chs.size() >= 2) stacked++;
    }
    if (stacked > 25) {
        SuspiciousRegion r;
        r.score    = std::min(0.3f + stacked * 0.004f, 0.85f);
        r.reason   = fmt::format("{} positions with layered colour channels", stacked);
        r.category = "color_trick";
        r.bounds   = ObjectUtils::getLevelBounds(objects);
        results.push_back(r);
    }

    // ── 3. Overall skin-tone colour trigger density ─────────────
    auto ct = ObjectUtils::filterColorTriggers(objects);
    int skinCount = 0;
    for (auto& t : ct)
        if (SkinColor::similarity(t.targetColor) > 0.6f) skinCount++;
    if (ct.size() > 5 && skinCount > 5) {
        float ratio = (float)skinCount / ct.size();
        if (ratio > 0.25f) {
            SuspiciousRegion r;
            r.score    = std::min(ratio * 0.9f, 0.8f);
            r.reason   = fmt::format("{}/{} colour triggers are skin-tone", skinCount, (int)ct.size());
            r.category = "color_trick";
            r.bounds   = ObjectUtils::getLevelBounds(objects);
            results.push_back(r);
        }
    }

    return results;
}
