#include "AlphaFlashAnalyzer.hpp"
#include <unordered_map>
#include <algorithm>

std::vector<SuspiciousRegion> AlphaFlashAnalyzer::analyze(const std::vector<ParsedObject>& objects) {
    std::vector<SuspiciousRegion> results;

    // Group alpha triggers by target channel
    std::unordered_map<int, std::vector<const ParsedObject*>> alphaByChannel;
    for (auto& o : objects)
        if (o.objectID == GDID::ALPHA_TRIGGER)
            alphaByChannel[o.targetColorChannel].push_back(&o);

    for (auto& [ch, trigs] : alphaByChannel) {
        if (trigs.size() < 2) continue;

        auto sorted = trigs;
        std::sort(sorted.begin(), sorted.end(),
            [](auto* a, auto* b){ return a->x < b->x; });

        // Look for 0→1→0 pattern (brief flash)
        for (size_t i = 0; i + 2 < sorted.size(); i++) {
            bool flash =
                sorted[i]->opacity < 0.1f &&
                sorted[i+1]->opacity > 0.4f &&
                sorted[i+2]->opacity < 0.1f;

            float dur = sorted[i+2]->x - sorted[i+1]->x;
            if (!flash || dur > 600) continue;

            // How many visual objects on this channel?
            auto chObjs = ObjectUtils::filterByColorChannel(objects, ch);
            int vis = 0;
            for (auto& co : chObjs) if (!co.isTrigger) vis++;
            if (vis < 8) continue;

            SuspiciousRegion r;
            r.score    = std::min(0.5f + vis * 0.005f, 0.9f);
            r.reason   = fmt::format("Alpha flash ch{}: {} objs revealed for {:.0f}u", ch, vis, dur);
            r.category = "alpha_flash";
            r.bounds   = {sorted[i]->x, sorted[i]->y - 200, dur + 200, 400};
            results.push_back(r);
            break; // one per channel
        }
    }

    return results;
}
