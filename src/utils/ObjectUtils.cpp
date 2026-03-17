#include "ObjectUtils.hpp"
#include <sstream>

// ── Decompress ──────────────────────────────────────────────────
std::string ObjectUtils::decompressLevelString(const std::string& compressed) {
    if (compressed.empty()) return {};

    // Already decompressed?
    if (compressed[0] == 'k' || compressed.find(',') < 20)
        return compressed;

    try {
        // Try base64url then base64 then raw
        std::string decoded;
        unsigned char* buf = nullptr;
        ssize_t sz = 0;

        // Geode / cocos helpers
        decoded.resize(cocos2d::base64Decode(
            (const unsigned char*)compressed.data(),
            (unsigned int)compressed.size(),
            &buf));
        if (buf) {
            decoded.assign((char*)buf, sz);
            free(buf); buf = nullptr;
        } else {
            decoded = compressed;
        }

        unsigned char* inflated = nullptr;
        sz = ZipUtils::ccInflateMemory(
            (unsigned char*)decoded.data(),
            (ssize_t)decoded.size(), &inflated);

        if (sz > 0 && inflated) {
            std::string result((char*)inflated, sz);
            free(inflated);
            return result;
        }
    } catch (...) {}

    return compressed;
}

// ── Parse a single object string  (key,val,key,val,...) ─────────
ParsedObject ObjectUtils::parseObjectString(const std::string& s) {
    ParsedObject o;
    std::istringstream ss(s);
    std::string tok;
    std::vector<std::string> tokens;
    while (std::getline(ss, tok, ',')) tokens.push_back(tok);

    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        int key = std::atoi(tokens[i].c_str());
        const auto& val = tokens[i+1];
        switch (key) {
            case 1:  o.objectID = std::stoi(val); break;
            case 2:  o.x = std::stof(val); break;
            case 3:  o.y = std::stof(val); break;
            case 4:  o.flipX = val == "1"; break;
            case 5:  o.flipY = val == "1"; break;
            case 6:  o.rotation = std::stof(val); break;
            case 7:  o.targetColor.r = (uint8_t)std::stoi(val); break;
            case 8:  o.targetColor.g = (uint8_t)std::stoi(val); break;
            case 9:  o.targetColor.b = (uint8_t)std::stoi(val); break;
            case 10: o.duration = std::stof(val); break;
            case 20: o.editorLayer = std::stoi(val); break;
            case 21: o.mainColorChannel = std::stoi(val); break;
            case 22: o.detailColorChannel = std::stoi(val); break;
            case 23: o.targetColorChannel = std::stoi(val); break;
            case 24: o.zLayer = std::stoi(val); break;
            case 25: o.zOrder = std::stoi(val); break;
            case 28: o.moveX = std::stof(val); break;
            case 29: o.moveY = std::stof(val); break;
            case 32: o.scaleX = o.scaleY = std::stof(val); break;
            case 35: o.opacity = std::stof(val); break;
            case 51: o.targetGroupID = std::stoi(val); break;
            case 56: o.activateGroup = val == "1"; break;
            case 57: {
                std::istringstream gs(val);
                std::string g;
                while (std::getline(gs, g, '.'))
                    if (!g.empty()) o.groups.push_back(std::stoi(g));
                o.groupCount = (int)o.groups.size();
                break;
            }
            case 61: o.editorLayer2 = std::stoi(val); break;
        }
    }
    o.isTrigger = GDID::isTrigger(o.objectID);
    return o;
}

// ── Parse full level string ─────────────────────────────────────
std::vector<ParsedObject> ObjectUtils::parseLevelString(const std::string& raw) {
    std::vector<ParsedObject> out;
    std::string data = decompressLevelString(raw);
    if (data.empty()) return out;

    std::istringstream ss(data);
    std::string tok;
    bool header = true;
    while (std::getline(ss, tok, ';')) {
        if (header) { header = false; continue; }
        if (tok.empty()) continue;
        out.push_back(parseObjectString(tok));
    }
    return out;
}

// ── From editor objects ─────────────────────────────────────────
std::vector<ParsedObject> ObjectUtils::fromEditorObjects(cocos2d::CCArray* arr) {
    std::vector<ParsedObject> out;
    if (!arr) return out;
    for (unsigned i = 0; i < arr->count(); i++) {
        auto* go = static_cast<GameObject*>(arr->objectAtIndex(i));
        if (!go) continue;
        ParsedObject o;
        o.objectID = go->m_objectID;
        o.x = go->getPositionX();
        o.y = go->getPositionY();
        o.rotation = go->getRotation();
        o.scaleX = go->getScaleX();
        o.scaleY = go->getScaleY();
        o.isTrigger = GDID::isTrigger(o.objectID);
        o.editorLayer  = go->m_editorLayer;
        o.editorLayer2 = go->m_editorLayer2;
        o.mainColorChannel   = go->m_mainColorChannel;
        o.detailColorChannel = go->m_detailColorChannel;
        if (auto* eg = dynamic_cast<EffectGameObject*>(go)) {
            o.targetColorChannel = eg->m_targetColorID;
            o.duration       = eg->m_duration;
            o.targetGroupID  = eg->m_targetGroupID;
            o.opacity        = eg->m_opacity;
            auto tc = eg->m_triggerTargetColor;
            o.targetColor = {tc.r, tc.g, tc.b};
        }
        if (go->m_groups) {
            for (int g = 0; g < go->m_groupCount; g++)
                o.groups.push_back(go->m_groups[g]);
            o.groupCount = go->m_groupCount;
        }
        out.push_back(o);
    }
    return out;
}

// ── Filters ─────────────────────────────────────────────────────
std::vector<ParsedObject> ObjectUtils::filterColorTriggers(const std::vector<ParsedObject>& v) {
    std::vector<ParsedObject> r;
    for (auto& o : v) if (GDID::isColorTrigger(o.objectID)) r.push_back(o);
    return r;
}
std::vector<ParsedObject> ObjectUtils::filterByGroup(const std::vector<ParsedObject>& v, int g) {
    std::vector<ParsedObject> r;
    for (auto& o : v)
        if (std::find(o.groups.begin(), o.groups.end(), g) != o.groups.end())
            r.push_back(o);
    return r;
}
std::vector<ParsedObject> ObjectUtils::filterByColorChannel(const std::vector<ParsedObject>& v, int ch) {
    std::vector<ParsedObject> r;
    for (auto& o : v)
        if (o.mainColorChannel == ch || o.detailColorChannel == ch)
            r.push_back(o);
    return r;
}
std::vector<ParsedObject> ObjectUtils::filterByRect(const std::vector<ParsedObject>& v, const cocos2d::CCRect& rect) {
    std::vector<ParsedObject> r;
    for (auto& o : v) if (rect.containsPoint({o.x, o.y})) r.push_back(o);
    return r;
}
cocos2d::CCRect ObjectUtils::getLevelBounds(const std::vector<ParsedObject>& v) {
    if (v.empty()) return {0,0,0,0};
    float x0=FLT_MAX, y0=FLT_MAX, x1=-FLT_MAX, y1=-FLT_MAX;
    for (auto& o : v) { x0=std::min(x0,o.x); y0=std::min(y0,o.y);
                         x1=std::max(x1,o.x); y1=std::max(y1,o.y); }
    return {x0, y0, x1-x0, y1-y0};
}
