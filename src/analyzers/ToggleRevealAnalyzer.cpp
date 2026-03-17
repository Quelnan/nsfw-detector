#include "ToggleRevealAnalyzer.hpp"
#include <unordered_map>

std::vector<SuspiciousRegion> ToggleRevealAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    // Find toggle triggers and the groups they control
    std::unordered_map<int, std::vector<const ParsedObject*>> toggleByTarget;

    for (auto& o : objects)
        if (o.objectID == GDID::TOGGLE_TRIGGER)
            toggleByTarget[o.targetGroupID].push_back(&o);

    for (auto& [grp, trigs] : toggleByTarget) {
        auto grpObjs = ObjectUtils::filterByGroup(objects, grp);
        int vis = 0;
        float x0=FLT_MAX,y0=FLT_MAX,x1=-FLT_MAX,y1=-FLT_MAX;
        for (auto& g : grpObjs) {
            if (g.isTrigger) continue;
            vis++;
            x0=std::min(x0,g.x); y0=std::min(y0,g.y);
            x1=std::max(x1,g.x); y1=std::max(y1,g.y);
        }
        if (vis < 25) continue;

        float h = y1-y0, w = x1-x0;
        if (h < 80 || w < 40) continue;

        SuspiciousRegion r;
        r.score    = std::min(0.3f + vis * 0.004f, 0.75f);
        r.reason   = fmt::format("Toggle grp {} controls {} visual objects ({:.0f}x{:.0f})", grp, vis, w, h);
        r.category = "toggle_reveal";
        r.bounds   = {x0, y0, w, h};
        results.push_back(r);
    }

    return results;
}
