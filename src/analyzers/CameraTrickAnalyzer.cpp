#include "CameraTrickAnalyzer.hpp"
#include <cmath>

std::vector<SuspiciousRegion> CameraTrickAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    constexpr float SUS_OFFSET = 1000.f;

    // ── 1. Camera / static triggers to extreme positions ────────
    for (auto& o : objects) {
        if (o.objectID != GDID::CAMERA_STATIC && o.objectID != GDID::CAMERA_OFFSET)
            continue;

        float destY = o.y + o.moveY;
        bool extreme = std::abs(destY) > SUS_OFFSET || destY > 800 || destY < -300;
        if (!extreme) continue;

        // Check content at destination
        cocos2d::CCRect search = {o.x + o.moveX - 600, destY - 400, 1200, 800};
        auto nearby = ObjectUtils::filterByRect(objects, search);
        int deco = 0, skinTrigs = 0;
        for (auto& n : nearby) {
            if (!n.isTrigger) deco++;
            if (GDID::isColorTrigger(n.objectID) && SkinColor::similarity(n.targetColor)>0.5f)
                skinTrigs++;
        }

        if (deco > 10) {
            SuspiciousRegion r;
            r.score = std::min(0.4f + deco*0.003f + skinTrigs*0.1f, 0.95f);
            r.reason = fmt::format("Camera to Y={:.0f} ({} objects, {} skin-trigs)", destY, deco, skinTrigs);
            r.category = "camera_trick";
            r.bounds = {o.x-50, o.y-50, 100, 100};
            results.push_back(r);
        }
    }

    // ── 2. Move triggers with extreme distances ─────────────────
    for (auto& o : objects) {
        if (o.objectID != GDID::MOVE_TRIGGER) continue;
        float dist = std::sqrt(o.moveX*o.moveX + o.moveY*o.moveY);
        if (dist < 2000.f) continue;

        auto grp = ObjectUtils::filterByGroup(objects, o.targetGroupID);
        int vis = 0;
        for (auto& g : grp) if (!g.isTrigger) vis++;
        if (vis > 10) {
            SuspiciousRegion r;
            r.score    = 0.5f;
            r.reason   = fmt::format("Move trig teleports {} objs by {:.0f}u (grp {})", vis, dist, o.targetGroupID);
            r.category = "camera_trick";
            r.bounds   = {o.x-30, o.y-30, 60, 60};
            results.push_back(r);
        }
    }

    // ── 3. Off-screen art clusters ──────────────────────────────
    constexpr float HIGH = 1500.f, LOW = -500.f;
    std::vector<const ParsedObject*> highObjs, lowObjs;
    for (auto& o : objects) {
        if (o.isTrigger) continue;
        if (o.y > HIGH)  highObjs.push_back(&o);
        if (o.y < LOW)   lowObjs.push_back(&o);
    }

    auto checkCluster = [&](const std::vector<const ParsedObject*>& cl, const char* label) {
        if (cl.size() < 25) return;
        float x0=FLT_MAX,y0=FLT_MAX,x1=-FLT_MAX,y1=-FLT_MAX;
        for (auto* o : cl) { x0=std::min(x0,o->x); y0=std::min(y0,o->y);
                              x1=std::max(x1,o->x); y1=std::max(y1,o->y); }
        float area = (x1-x0)*(y1-y0);
        if (area <= 0) return;
        float density = cl.size() / (area / 10000.f);
        if (density > 0.5f) {
            SuspiciousRegion r;
            r.score    = std::min(0.35f + cl.size()*0.002f, 0.8f);
            r.reason   = fmt::format("{} objects at {} (Y {:.0f}-{:.0f})", cl.size(), label, y0, y1);
            r.category = "camera_trick";
            r.bounds   = {x0, y0, x1-x0, y1-y0};
            results.push_back(r);
        }
    };
    checkCluster(highObjs, "extreme high");
    checkCluster(lowObjs,  "extreme low");

    return results;
}
