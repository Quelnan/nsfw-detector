#pragma once
#include "../NSFWDetector.hpp"
#include "../utils/ObjectUtils.hpp"

class ColorTrickAnalyzer {
public:
    static std::vector<SuspiciousRegion> analyze(const std::vector<ParsedObject>& objects);
};
