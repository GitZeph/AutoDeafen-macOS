#pragma once

// ---------------------------------------------------------------------------
// AutoDeafen - Configurazione per-livello
// ---------------------------------------------------------------------------
// Ogni livello ha la sua soglia di deafen (e una soglia opzionale di "undeafen",
// utile per le cutscene). I valori sono salvati per-livello tramite i saved
// values di Geode, con chiave derivata da id + tipo livello.
//
// Le StartPos NON vengono salvate: nel popup di pausa, quando l'utente sceglie
// una startpos, calcoliamo la sua percentuale nel livello e la scriviamo nella
// soglia. Internamente quindi è tutto a percentuale.
// ---------------------------------------------------------------------------

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
        bool  enabled     = true;    // AutoDeafen attivo su questo livello
        float onPercent   = 0.f;     // % a cui ci si deafenna
        bool  offEnabled  = false;   // esiste una soglia di "smetti di deafennare"?
        float offPercent  = 100.f;   // % a cui ci si ri-smuta
    };

    // Tag del tipo di livello, per non confondere livelli diversi con lo stesso id.
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

    // Default globali presi dalle impostazioni Geode della mod.
    inline LevelCfg defaultCfg() {
        LevelCfg c;
        c.enabled    = true;
        c.onPercent  = static_cast<float>(
            geode::Mod::get()->getSettingValue<int64_t>("default-percent"));
        c.offEnabled = false;
        c.offPercent = 100.f;
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

    // Percentuali (0-100) di tutte le StartPos del livello, ordinate crescenti.
    // Vuoto se il livello non ha start positions o la lunghezza è ignota.
    inline std::vector<float> startPosPercents(GJBaseGameLayer* gl) {
        std::vector<float> out;
        if (!gl || !gl->m_objects || gl->m_levelLength <= 0.f) return out;

        auto objs = gl->m_objects;
        for (int i = 0; i < objs->count(); ++i) {
            auto obj = static_cast<cocos2d::CCNode*>(objs->objectAtIndex(i));
            if (geode::cast::typeinfo_cast<StartPosObject*>(obj)) {
                float pct = obj->getPositionX() / gl->m_levelLength * 100.f;
                pct = std::clamp(pct, 0.f, 100.f);
                out.push_back(pct);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

} // namespace autodeafen
