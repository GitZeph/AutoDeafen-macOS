#pragma once

// Per-level configuration.
//
// Each level stores its own deafen threshold (and an optional "undeafen"
// threshold, handy for cutscenes), persisted through Geode's saved values under
// a key derived from the level id and type.
//
// Start positions are not stored: when the user picks a start position in the
// pause popup, its level percentage is computed and written into the threshold,
// so everything is percentage-based internally.

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/StartPosObject.hpp>
#include <Geode/binding/GameObject.hpp>

#include <string>
#include <vector>
#include <algorithm>

namespace autodeafen {

    struct LevelCfg {
        bool  enabled    = true;     // auto-deafen active on this level
        float onPercent  = 0.f;      // percentage at which to deafen
        bool  offEnabled = false;    // is there an "undeafen" threshold?
        float offPercent = 100.f;    // percentage at which to undeafen again
    };

    // Distinguishes levels that share an id (e.g. main level 1 vs online id 1).
    inline int levelTypeTag(GJGameLevel* level) {
        if (!level) return 0;
        if (level->m_levelType != GJLevelType::Saved) return 1; // main/local/editor
        if (level->m_dailyID.value() > 0)             return 2; // daily/weekly
        if (level->m_gauntletLevel)                   return 3; // gauntlet
        return 0;                                               // online saved
    }

    inline std::string levelKey(GJGameLevel* level) {
        if (!level) return "lvl-unknown";
        return "lvl-" + std::to_string(level->m_levelID.value())
             + "-" + std::to_string(levelTypeTag(level));
    }

    // Defaults taken from the global mod settings.
    inline LevelCfg defaultCfg() {
        LevelCfg c;
        c.onPercent = static_cast<float>(
            geode::Mod::get()->getSettingValue<int64_t>("default-percent"));
        return c;
    }

    inline LevelCfg loadCfg(GJGameLevel* level) {
        LevelCfg c = defaultCfg();
        auto mod = geode::Mod::get();
        auto key = levelKey(level);
        if (mod->hasSavedValue(key)) {
            auto v = mod->getSavedValue<matjson::Value>(key);
            c.enabled    = v["enabled"].asBool().unwrapOr(c.enabled);
            c.onPercent  = static_cast<float>(v["on"].asDouble().unwrapOr(c.onPercent));
            c.offEnabled = v["offEn"].asBool().unwrapOr(c.offEnabled);
            c.offPercent = static_cast<float>(v["off"].asDouble().unwrapOr(c.offPercent));
        }
        return c;
    }

    inline void saveCfg(GJGameLevel* level, LevelCfg const& c) {
        matjson::Value v;
        v["enabled"] = c.enabled;
        v["on"]      = c.onPercent;
        v["offEn"]   = c.offEnabled;
        v["off"]     = c.offPercent;
        geode::Mod::get()->setSavedValue(levelKey(level), v);
    }

    // Percentages (0-100) of every start position in the level, sorted ascending.
    // Empty if the level has none or its length is unknown.
    inline std::vector<float> startPosPercents(GJBaseGameLayer* gl) {
        std::vector<float> out;
        if (!gl || !gl->m_objects || gl->m_levelLength <= 0.f) return out;

        auto objs = gl->m_objects;
        for (int i = 0; i < objs->count(); ++i) {
            auto obj = static_cast<cocos2d::CCNode*>(objs->objectAtIndex(i));
            if (geode::cast::typeinfo_cast<StartPosObject*>(obj)) {
                float pct = obj->getPositionX() / gl->m_levelLength * 100.f;
                out.push_back(std::clamp(pct, 0.f, 100.f));
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

} // namespace autodeafen
