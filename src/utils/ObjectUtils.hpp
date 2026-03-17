#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

// ── Lightweight parsed object for analysis ──────────────────────
struct ParsedObject {
    int objectID = 0;
    float x = 0, y = 0;
    float rotation = 0;
    float scaleX = 1, scaleY = 1;
    bool flipX = false, flipY = false;
    int editorLayer = 0;
    int editorLayer2 = 0;
    int zLayer = 0;
    int zOrder = 0;
    std::vector<int> groups;
    int groupCount = 0;

    // Color
    int mainColorChannel = 0;
    int detailColorChannel = 0;

    // Trigger fields
    bool isTrigger = false;
    int targetColorChannel = 0;
    cocos2d::ccColor3B targetColor = {0,0,0};
    float duration = 0;
    float opacity = 1;
    bool blending = false;

    float moveX = 0, moveY = 0;
    int targetGroupID = 0;

    int toggleGroupID = 0;
    bool activateGroup = false;

    cocos2d::CCRect getBounds() const {
        float w = 30.f * std::abs(scaleX);
        float h = 30.f * std::abs(scaleY);
        return { x - w/2, y - h/2, w, h };
    }
};

// ── Well-known GD object IDs ────────────────────────────────────
namespace GDID {
    constexpr int COLOR_TRIGGER   = 899;
    constexpr int MOVE_TRIGGER    = 901;
    constexpr int STOP_TRIGGER    = 1616;
    constexpr int TOGGLE_TRIGGER  = 1049;
    constexpr int SPAWN_TRIGGER   = 1268;
    constexpr int ROTATE_TRIGGER  = 1346;
    constexpr int FOLLOW_TRIGGER  = 1347;
    constexpr int SHAKE_TRIGGER   = 1520;
    constexpr int ANIMATE_TRIGGER = 1585;
    constexpr int TOUCH_TRIGGER   = 1595;
    constexpr int COUNT_TRIGGER   = 1611;
    constexpr int INSTANT_COUNT   = 1811;
    constexpr int PICKUP_TRIGGER  = 1817;
    constexpr int COLLISION_TRIGGER = 1815;
    constexpr int ON_DEATH_TRIGGER  = 1812;
    constexpr int PULSE_TRIGGER   = 1006;
    constexpr int ALPHA_TRIGGER   = 1007;
    constexpr int CAMERA_STATIC   = 2062;
    constexpr int CAMERA_OFFSET   = 1916;
    constexpr int EDIT_OBJ_TRIGGER = 3600;
    constexpr int AREA_TINT       = 3602;

    inline bool isTrigger(int id) {
        switch (id) {
            case COLOR_TRIGGER: case MOVE_TRIGGER: case STOP_TRIGGER:
            case TOGGLE_TRIGGER: case SPAWN_TRIGGER: case ROTATE_TRIGGER:
            case FOLLOW_TRIGGER: case SHAKE_TRIGGER: case ANIMATE_TRIGGER:
            case TOUCH_TRIGGER: case COUNT_TRIGGER: case INSTANT_COUNT:
            case PICKUP_TRIGGER: case COLLISION_TRIGGER: case ON_DEATH_TRIGGER:
            case PULSE_TRIGGER: case ALPHA_TRIGGER: case CAMERA_STATIC:
            case CAMERA_OFFSET: case EDIT_OBJ_TRIGGER: case AREA_TINT:
                return true;
            default: return false;
        }
    }

    inline bool isColorTrigger(int id) {
        return id == COLOR_TRIGGER || id == PULSE_TRIGGER
            || id == ALPHA_TRIGGER || id == AREA_TINT;
    }

    inline bool isMoveTrigger(int id) {
        return id == MOVE_TRIGGER || id == ROTATE_TRIGGER || id == FOLLOW_TRIGGER;
    }
}

// ── Skin-tone colour helpers ────────────────────────────────────
namespace SkinColor {
    inline float similarity(const cocos2d::ccColor3B& c) {
        struct Range { int rMin,rMax,gMin,gMax,bMin,bMax; };
        static const Range ranges[] = {
            {180,255, 130,210, 100,180},
            {150,210, 100,160,  70,130},
            {120,180,  80,130,  50,100},
            { 80,140,  50,100,  30, 80},
            {200,255, 160,220, 130,190},
            {220,255, 170,210, 140,180},
        };
        float best = 0;
        for (auto& r : ranges) {
            auto chan = [](int v, int lo, int hi) -> float {
                if (v >= lo && v <= hi) return 1.f;
                float d = std::min(std::abs(v-lo), std::abs(v-hi));
                return std::max(0.f, 1.f - d/50.f);
            };
            float rs = chan(c.r, r.rMin, r.rMax);
            float gs = chan(c.g, r.gMin, r.gMax);
            float bs = chan(c.b, r.bMin, r.bMax);
            float order = (c.r > c.g && c.g > c.b) ? 1.f : 0.5f;
            best = std::max(best, (rs+gs+bs)/3.f * 0.7f + order * 0.3f);
        }
        return best;
    }
    inline bool isSkinTone(const cocos2d::ccColor3B& c) { return similarity(c) > 0.7f; }
}

// ── Object utilities ────────────────────────────────────────────
class ObjectUtils {
public:
    static std::vector<ParsedObject> parseLevelString(const std::string& raw);
    static std::vector<ParsedObject> fromEditorObjects(cocos2d::CCArray* arr);
    static ParsedObject              parseObjectString(const std::string& s);
    static std::string               decompressLevelString(const std::string& compressed);

    // Filters
    static std::vector<ParsedObject> filterColorTriggers(const std::vector<ParsedObject>& v);
    static std::vector<ParsedObject> filterByGroup(const std::vector<ParsedObject>& v, int g);
    static std::vector<ParsedObject> filterByColorChannel(const std::vector<ParsedObject>& v, int ch);
    static std::vector<ParsedObject> filterByRect(const std::vector<ParsedObject>& v, const cocos2d::CCRect& r);
    static cocos2d::CCRect           getLevelBounds(const std::vector<ParsedObject>& v);
};
