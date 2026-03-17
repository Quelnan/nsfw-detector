#include "ArtClusterAnalyzer.hpp"
#include <unordered_map>
#include <set>
#include <cmath>
#include <algorithm>

std::vector<SuspiciousRegion> ArtClusterAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    // ── Grid-based clustering ───────────────────────────────────
    constexpr float GRID = 200.f;
    struct GK { int x,y; bool operator==(const GK& o) const { return x==o.x&&y==o.y; } };
    struct GKH { size_t operator()(const GK& k) const { return std::hash<int>()(k.x)^(std::hash<int>()(k.y)<<16); } };
    std::unordered_map<GK, std::vector<const ParsedObject*>, GKH> grid;

    for (auto& o : objects)
        if (!o.isTrigger)
            grid[{(int)(o.x/GRID), (int)(o.y/GRID)}].push_back(&o);

    std::set<int64_t> visited; // packed grid keys already processed

    for (auto& [key, cell] : grid) {
        if (cell.size() < 15) continue;
        int64_t pk = ((int64_t)key.x << 32) | (uint32_t)key.y;
        if (visited.count(pk)) continue;

        // Merge adjacent dense cells
        std::vector<const ParsedObject*> cluster = cell;
        visited.insert(pk);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (!dx && !dy) continue;
            GK adj{key.x+dx, key.y+dy};
            int64_t apk = ((int64_t)adj.x << 32) | (uint32_t)adj.y;
            if (grid.count(adj) && grid[adj].size() > 8 && !visited.count(apk)) {
                cluster.insert(cluster.end(), grid[adj].begin(), grid[adj].end());
                visited.insert(apk);
            }
        }

        if (cluster.size() < 30) continue;

        // Bounding box
        float x0=FLT_MAX,y0=FLT_MAX,x1=-FLT_MAX,y1=-FLT_MAX;
        for (auto* o : cluster) {
            x0=std::min(x0,o->x); y0=std::min(y0,o->y);
            x1=std::max(x1,o->x); y1=std::max(y1,o->y);
        }
        float w = x1-x0, h = y1-y0;
        if (w <= 0 || h <= 0) continue;

        float score = 0;
        std::string reason;

        // Aspect ratio checks (human figure proportions)
        float ar = h / w;
        if ((ar > 1.2f && ar < 3.f) || (ar > 0.3f && ar < 0.85f)) {
            score += 0.12f;
            reason += "figure-like AR; ";
        }

        // Fill ratio
        float fill = cluster.size() * 900.f / (w * h);
        if (fill > 0.08f && fill < 0.8f) { score += 0.1f; reason += "art-like fill; "; }

        // Symmetry check (sample)
        float cx = (x0+x1)/2;
        int symPairs = 0, checked = 0;
        for (auto* o : cluster) {
            float mx = 2*cx - o->x;
            for (auto* o2 : cluster) {
                if (std::abs(o2->x - mx) < 5.f && std::abs(o2->y - o->y) < 5.f) {
                    symPairs++; break;
                }
            }
            if (++checked > 80) break;
        }
        float symRatio = checked > 0 ? (float)symPairs/checked : 0;
        if (symRatio > 0.3f) { score += 0.12f; reason += fmt::format("symmetry {:.0f}%; ", symRatio*100); }

        // Object type variety
        std::set<int> types;
        for (auto* o : cluster) types.insert(o->objectID);
        float variety = (float)types.size() / std::min((int)cluster.size(), 100);
        if (variety > 0.03f && variety < 0.5f) { score += 0.08f; reason += "varied obj types; "; }

        // Colour channels → skin tones
        std::set<int> chs;
        for (auto* o : cluster) {
            if (o->mainColorChannel>0) chs.insert(o->mainColorChannel);
            if (o->detailColorChannel>0) chs.insert(o->detailColorChannel);
        }
        int skinCh = 0;
        for (auto& o : objects) {
            if (!GDID::isColorTrigger(o.objectID)) continue;
            if (!chs.count(o.targetColorChannel)) continue;
            if (SkinColor::similarity(o.targetColor) > 0.5f) skinCh++;
        }
        if (skinCh > 2) { score += 0.25f; reason += fmt::format("{} skin-tone channels; ", skinCh); }
        if (chs.size() > 10) { score += 0.1f; reason += fmt::format("{} colour channels; ", chs.size()); }

        if (score > 0.15f) {
            SuspiciousRegion r;
            r.score    = std::min(score, 0.9f);
            r.reason   = fmt::format("{} objects: {}", cluster.size(), reason);
            r.category = "art";
            r.bounds   = {x0, y0, w, h};
            results.push_back(r);
        }
    }

    return results;
}
