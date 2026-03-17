#include "LagMachineAnalyzer.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>

std::vector<SuspiciousRegion> LagMachineAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    // ── 1. Spawn trigger loops ──────────────────────────────────
    // group -> spawn triggers IN that group
    std::unordered_map<int, std::vector<const ParsedObject*>> spawnByGroup;

    for (auto& o : objects) {
        if (o.objectID != GDID::SPAWN_TRIGGER) continue;
        for (int g : o.groups) spawnByGroup[g].push_back(&o);
    }

    // Follow spawn chains and detect cycles
    for (auto& [startGroup, triggers] : spawnByGroup) {
        std::unordered_set<int> visited;
        int cur = startGroup;
        int chainLen = 0;
        bool loop = false;

        for (int step = 0; step < 20; step++) {
            if (visited.count(cur)) { loop = true; break; }
            visited.insert(cur);
            chainLen++;

            // Find a spawn trigger in group `cur`
            bool found = false;
            for (auto& o : objects) {
                if (o.objectID != GDID::SPAWN_TRIGGER) continue;
                for (int g : o.groups) {
                    if (g == cur) {
                        cur = o.targetGroupID;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) break;
        }

        if (loop && chainLen <= 6) {
            SuspiciousRegion r;
            r.score    = 0.55f + (6 - chainLen) * 0.08f;
            r.reason   = fmt::format("Spawn loop (chain length {})", chainLen);
            r.category = "lag_machine";
            // bounds from involved triggers
            float x0=FLT_MAX,y0=FLT_MAX,x1=-FLT_MAX,y1=-FLT_MAX;
            for (auto* t : triggers) {
                x0=std::min(x0,t->x); y0=std::min(y0,t->y);
                x1=std::max(x1,t->x); y1=std::max(y1,t->y);
            }
            r.bounds = {x0,y0,std::max(x1-x0,30.f),std::max(y1-y0,30.f)};
            results.push_back(r);
        }
    }

    // ── 2. Trigger density spikes ───────────────────────────────
    constexpr float BIN = 60.f;
    std::unordered_map<int, int> bins;
    for (auto& o : objects)
        if (o.isTrigger) bins[(int)(o.x / BIN)]++;

    for (auto& [b, cnt] : bins) {
        if (cnt < 50) continue;
        SuspiciousRegion r;
        r.score = (cnt > 200) ? 0.8f : (cnt > 100) ? 0.55f : 0.3f;
        r.reason   = fmt::format("{} triggers in ~60-unit span", cnt);
        r.category = "lag_machine";
        r.bounds   = {b*BIN, -100, BIN, 700};
        results.push_back(r);
    }

    // ── 3. Pulse spam ───────────────────────────────────────────
    int pulseCount = 0;
    float pMinX=FLT_MAX, pMaxX=-FLT_MAX;
    for (auto& o : objects) {
        if (o.objectID == GDID::PULSE_TRIGGER) {
            pulseCount++;
            pMinX = std::min(pMinX, o.x);
            pMaxX = std::max(pMaxX, o.x);
        }
    }
    if (pulseCount > 30 && pMaxX > pMinX) {
        float density = pulseCount / ((pMaxX - pMinX) / 100.f);
        if (density > 5.f) {
            SuspiciousRegion r;
            r.score    = std::min(0.3f + density * 0.04f, 0.75f);
            r.reason   = fmt::format("{} pulse triggers (density {:.1f}/100u)", pulseCount, density);
            r.category = "lag_machine";
            r.bounds   = {pMinX, -100, pMaxX-pMinX, 700};
            results.push_back(r);
        }
    }

    // ── 4. Correlate with nearby visual content ─────────────────
    for (auto& region : results) {
        cocos2d::CCRect search = {
            region.bounds.origin.x - 500, region.bounds.origin.y - 500,
            region.bounds.size.width + 1000, region.bounds.size.height + 1000
        };
        auto nearby = ObjectUtils::filterByRect(objects, search);
        int deco = 0, skinTrigs = 0;
        for (auto& o : nearby) {
            if (!o.isTrigger) deco++;
            if (GDID::isColorTrigger(o.objectID) && SkinColor::similarity(o.targetColor) > 0.5f)
                skinTrigs++;
        }
        float boost = 0;
        if (deco > 50) boost += 0.15f;
        if (skinTrigs > 2) boost += 0.2f;
        if (boost > 0) {
            region.score = std::min(region.score + boost, 1.f);
            region.reason += fmt::format(" [near {} deco, {} skin-trigs]", deco, skinTrigs);
        }
    }

    return results;
}
