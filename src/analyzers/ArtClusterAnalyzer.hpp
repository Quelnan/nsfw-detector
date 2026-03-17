#pragma once
#include "../NSFWDetector.hpp"
#include "../utils/ObjectUtils.hpp"

class ArtClusterAnalyzer {
public:
    static std::vector<SuspiciousRegion> analyze(const std::vector<ParsedObject>& objects);
};
