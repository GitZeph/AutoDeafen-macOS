#pragma once

// ---------------------------------------------------------------------------
// AutoDeafen - Popup impostazioni PER-LIVELLO (menu di pausa)
// ---------------------------------------------------------------------------
// Solo impostazioni per-livello: abilitato, soglia di deafen, soglia opzionale
// di undeafen (cutscene). NESSUNA credenziale qui.
//
// Layout verticale con slider in stile GD (come le impostazioni globali).
// Lo stepper "StartPos" sposta lo slider sulla percentuale della start position.
// ---------------------------------------------------------------------------

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <functional>
#include <cmath>

#include "LevelConfig.hpp"

using namespace geode::prelude;

class ADConfigPopup : public geode::Popup {
protected:
    autodeafen::LevelCfg m_cfg;
    std::vector<float> m_sp;
    std::function<void(autodeafen::LevelCfg const&)> m_onApply;

    CCMenu* m_menu = nullptr;
    Slider* m_onSlider = nullptr;
    Slider* m_offSlider = nullptr;
    CCLabelBMFont* m_onValLabel = nullptr;
    CCLabelBMFont* m_offValLabel = nullptr;
    CCLabelBMFont* m_onSpLabel = nullptr;
    CCLabelBMFont* m_offSpLabel = nullptr;
    CCMenuItemToggler* m_enabledToggle = nullptr;
    CCMenuItemToggler* m_offToggle = nullptr;
    int m_onSpIdx = -1;
    int m_offSpIdx = -1;

    static float clampPct(float v) { return std::clamp(v, 0.f, 100.f); }

    void updateLabels() {
        if (m_onValLabel)
            m_onValLabel->setString(
                fmt::format("Deafen at: {}%", (int)std::round(m_cfg.onPercent)).c_str());
        if (m_offValLabel)
            m_offValLabel->setString(
                fmt::format("Stop deafen at: {}%", (int)std::round(m_cfg.offPercent)).c_str());
    }

    void onSlider(CCObject*) {
        if (m_onSlider)  m_cfg.onPercent  = clampPct(m_onSlider->getValue() * 100.f);
        if (m_offSlider) m_cfg.offPercent = clampPct(m_offSlider->getValue() * 100.f);
        updateLabels();
    }

    void refreshSpLabel(bool isOff) {
        int idx = isOff ? m_offSpIdx : m_onSpIdx;
        auto lbl = isOff ? m_offSpLabel : m_onSpLabel;
        if (!lbl) return;
        if (m_sp.empty()) { lbl->setString("no startpos"); return; }
        if (idx < 0) lbl->setString("StartPos");
        else lbl->setString(fmt::format("SP {}/{}", idx + 1, (int)m_sp.size()).c_str());
    }

    void step(bool isOff, int delta) {
        if (m_sp.empty()) return;
        int& idx = isOff ? m_offSpIdx : m_onSpIdx;
        idx += delta;
        if (idx < 0) idx = (int)m_sp.size() - 1;
        if (idx >= (int)m_sp.size()) idx = 0;
        float pct = m_sp[idx];
        auto slider = isOff ? m_offSlider : m_onSlider;
        if (slider) slider->setValue(pct / 100.f);
        if (isOff) m_cfg.offPercent = pct; else m_cfg.onPercent = pct;
        updateLabels();
        refreshSpLabel(isOff);
    }

    void onStepLeft(CCObject* s)  { step(static_cast<CCNode*>(s)->getTag() == 1, -1); }
    void onStepRight(CCObject* s) { step(static_cast<CCNode*>(s)->getTag() == 1, +1); }
    void onToggleEnabled(CCObject*) {}
    void onToggleOff(CCObject*) {}

    void onSave(CCObject*) {
        m_cfg.enabled    = m_enabledToggle ? m_enabledToggle->isToggled() : true;
        m_cfg.offEnabled = m_offToggle ? m_offToggle->isToggled() : false;
        if (m_onApply) m_onApply(m_cfg);
        this->onClose(nullptr);
    }

    CCLabelBMFont* addLabel(const char* text, float x, float y, float scale, float ax) {
        auto l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(scale);
        l->setAnchorPoint({ ax, 0.5f });
        l->setPosition({ x, y });
        m_mainLayer->addChild(l);
        return l;
    }

    // Slider GD centrato in (cx,y) + stepper StartPos sotto.
    Slider* buildSliderRow(float y, bool isOff, float initialPct,
                           CCLabelBMFont*& spLabelOut) {
        auto slider = Slider::create(this, menu_selector(ADConfigPopup::onSlider), 0.65f);
        slider->setPosition({ 170.f, y });
        slider->setValue(clampPct(initialPct) / 100.f);
        m_mainLayer->addChild(slider);

        // Stepper StartPos sotto lo slider
        float sy = y - 24.f;
        spLabelOut = CCLabelBMFont::create("", "chatFont.fnt");
        spLabelOut->setScale(0.5f);
        spLabelOut->setAnchorPoint({ 0.5f, 0.5f });
        spLabelOut->setPosition({ 170.f, sy });
        spLabelOut->setColor({ 180, 180, 180 });
        m_mainLayer->addChild(spLabelOut);

        if (!m_sp.empty()) {
            auto lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
            lSpr->setScale(0.5f);
            auto l = CCMenuItemSpriteExtra::create(lSpr, this,
                menu_selector(ADConfigPopup::onStepLeft));
            l->setPosition({ 120.f, sy });
            l->setTag(isOff ? 1 : 0);
            m_menu->addChild(l);

            auto rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
            rSpr->setFlipX(true);
            rSpr->setScale(0.5f);
            auto r = CCMenuItemSpriteExtra::create(rSpr, this,
                menu_selector(ADConfigPopup::onStepRight));
            r->setPosition({ 220.f, sy });
            r->setTag(isOff ? 1 : 0);
            m_menu->addChild(r);
        }
        return slider;
    }

    bool init(autodeafen::LevelCfg cfg, std::vector<float> sp,
              std::function<void(autodeafen::LevelCfg const&)> onApply) {
        if (!geode::Popup::init(330.f, 300.f)) return false;

        m_cfg = cfg;
        m_sp = std::move(sp);
        m_onApply = std::move(onApply);

        this->setTitle("AutoDeafen - This Level");

        m_menu = CCMenu::create();
        m_menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_menu);

        // --- Enabled su questo livello -------------------------------------
        addLabel("Enabled on this level", 30.f, 262.f, 0.55f, 0.f);
        m_enabledToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(ADConfigPopup::onToggleEnabled), 0.8f);
        m_enabledToggle->setPosition({ 300.f, 262.f });
        m_enabledToggle->toggle(m_cfg.enabled);
        m_menu->addChild(m_enabledToggle);

        // --- DEAFEN AT (slider) --------------------------------------------
        m_onValLabel = addLabel("Deafen at: 0%", 30.f, 222.f, 0.55f, 0.f);
        m_onSlider = buildSliderRow(196.f, false, m_cfg.onPercent, m_onSpLabel);
        refreshSpLabel(false);

        // --- STOP DEAFEN AT (slider, per cutscene) -------------------------
        m_offToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(ADConfigPopup::onToggleOff), 0.7f);
        m_offToggle->setPosition({ 40.f, 138.f });
        m_offToggle->toggle(m_cfg.offEnabled);
        m_menu->addChild(m_offToggle);
        m_offValLabel = addLabel("Stop deafen at: 0%", 58.f, 138.f, 0.5f, 0.f);
        m_offSlider = buildSliderRow(112.f, true, m_cfg.offPercent, m_offSpLabel);
        refreshSpLabel(true);

        updateLabels();

        // --- Save ----------------------------------------------------------
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", 0.9f),
            this, menu_selector(ADConfigPopup::onSave));
        saveBtn->setPosition({ 165.f, 40.f });
        m_menu->addChild(saveBtn);

        return true;
    }

public:
    static ADConfigPopup* create(autodeafen::LevelCfg cfg, std::vector<float> sp,
                                 std::function<void(autodeafen::LevelCfg const&)> onApply) {
        auto ret = new ADConfigPopup();
        if (ret && ret->init(cfg, std::move(sp), std::move(onApply))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};
