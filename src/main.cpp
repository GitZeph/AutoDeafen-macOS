// AutoDeafen — Geode hooks, OAuth flow and Discord IPC wiring.
//
// Rather than simulating Discord's deafen hotkey (macOS ignores synthetic
// keystrokes for global keybinds), the mod talks to the Discord client over
// its local IPC socket and toggles deafen with SET_VOICE_SETTINGS.
//
// That requires an OAuth access token with the "rpc rpc.voice.write" scopes.
// Those scopes are whitelist-only except for the application owner, so each
// user registers their own Discord application and pastes its Client ID and
// Client Secret into the mod settings.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <thread>

#include "DiscordIPC.hpp"
#include "OAuthServer.hpp"
#include "LevelConfig.hpp"
#include "ConfigPopup.hpp"

using namespace geode::prelude;

namespace ad = autodeafen;

namespace {

    constexpr const char* kRedirectUri = "http://localhost:8000";

    bool isEnabled()           { return Mod::get()->getSettingValue<bool>("enabled"); }
    bool undeafenOnDeath()      { return Mod::get()->getSettingValue<bool>("undeafen-on-death"); }
    std::string clientId()      { return Mod::get()->getSettingValue<std::string>("client-id"); }
    std::string clientSecret()  { return Mod::get()->getSettingValue<std::string>("client-secret"); }

    long long nowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Show a notification on the main thread (safe to call from worker threads).
    void notify(std::string text, bool success) {
        Loader::get()->queueInMainThread([text = std::move(text), success] {
            Notification::create(text,
                success ? NotificationIcon::Success : NotificationIcon::Error)->show();
        });
    }

    // Connect and authenticate the IPC client using the stored token.
    void connectIPC() {
        auto token = Mod::get()->getSavedValue<std::string>("access-token");
        auto id = clientId();
        if (token.empty() || id.empty()) return;

        std::thread([id, token] {
            std::string err;
            if (ad::ipc::connect(id, token, err)) {
                log::info("Connected to Discord over IPC");
                notify("AutoDeafen: connected to Discord", true);
            } else {
                log::error("IPC connection failed: {}", err);
                notify("AutoDeafen: " + err, false);
            }
        }).detach();
    }

    // POST to Discord's token endpoint (worker thread). Handles both the initial
    // authorization_code exchange and refresh_token, depending on `body`.
    void requestToken(std::string body) {
        std::thread([body = std::move(body)] {
            auto req = web::WebRequest();
            req.header("Content-Type", "application/x-www-form-urlencoded");
            req.bodyString(body);

            auto res = req.postSync("https://discord.com/api/oauth2/token");
            if (!res.ok()) {
                log::error("Token request failed (HTTP {}): {}",
                    res.code(), res.string().unwrapOr(""));
                notify("AutoDeafen: authentication failed (HTTP "
                    + std::to_string(res.code()) + ")", false);
                return;
            }

            auto jsonRes = res.json();
            if (!jsonRes) {
                notify("AutoDeafen: invalid response from Discord", false);
                return;
            }
            auto json = jsonRes.unwrap();

            std::string access  = json["access_token"].asString().unwrapOr("");
            std::string refresh = json["refresh_token"].asString().unwrapOr("");
            long long expiresIn = json["expires_in"].asInt().unwrapOr(0);

            if (access.empty()) {
                notify("AutoDeafen: no token received", false);
                return;
            }

            Mod::get()->setSavedValue<std::string>("access-token", access);
            Mod::get()->setSavedValue<std::string>("refresh-token", refresh);
            Mod::get()->setSavedValue<int64_t>("token-expiry", nowSeconds() + expiresIn);

            connectIPC();
        }).detach();
    }

    void exchangeCodeForToken(std::string const& code) {
        requestToken(
            "client_id=" + clientId() +
            "&client_secret=" + clientSecret() +
            "&grant_type=authorization_code"
            "&code=" + code +
            "&redirect_uri=" + kRedirectUri);
    }

    void refreshToken() {
        auto refresh = Mod::get()->getSavedValue<std::string>("refresh-token");
        if (refresh.empty()) return;
        requestToken(
            "client_id=" + clientId() +
            "&client_secret=" + clientSecret() +
            "&grant_type=refresh_token"
            "&refresh_token=" + refresh);
    }

    // Start the redirect server and open Discord's consent page in the browser.
    void startAuthFlow() {
        if (clientId().empty() || clientSecret().empty()) {
            notify("AutoDeafen: set Client ID and Client Secret first", false);
            return;
        }

        ad::oauth::startRedirectServer([](std::string code, std::string error) {
            if (code.empty()) {
                log::error("OAuth failed: {}", error);
                notify("AutoDeafen: authorization failed (" + error + ")", false);
                return;
            }
            exchangeCodeForToken(code);
        });

        web::openLinkInBrowser(
            "https://discord.com/oauth2/authorize?client_id=" + clientId() +
            "&response_type=code"
            "&redirect_uri=http%3A%2F%2Flocalhost%3A8000"
            "&scope=rpc.voice.write+rpc");
        notify("AutoDeafen: authorize in your browser, then return to the game", true);
    }

    // Shown when we are not connected. Contains actions only — credentials are
    // entered in the Geode mod settings, never here.
    void openConnectFlow() {
        if (clientId().empty() || clientSecret().empty()) {
            createQuickPopup(
                "AutoDeafen",
                "AutoDeafen needs your own Discord app.\n"
                "Open the <cy>mod settings</c>, paste <cg>Client ID</c> and "
                "<cg>Client Secret</c>, then come back and press <cb>Authorize</c>.",
                "Cancel", "Settings",
                [](auto, bool settings) { if (settings) openSettingsPopup(Mod::get()); });
            return;
        }
        createQuickPopup(
            "AutoDeafen",
            "Ready to connect to <cg>Discord</c>.\n"
            "<cb>Authorize</c> opens your browser: accept, then return here. "
            "<cy>Discord must be running.</c>",
            "Cancel", "Authorize",
            [](auto, bool ok) { if (ok) startAuthFlow(); });
    }

} // namespace

// On load: if we have a stored token, connect (refreshing it first if expired).
$on_mod(Loaded) {
    if (!Mod::get()->hasSavedValue("access-token")) return;

    long long expiry = Mod::get()->getSavedValue<int64_t>("token-expiry");
    if (nowSeconds() >= expiry && Mod::get()->hasSavedValue("refresh-token")) {
        refreshToken();
    } else {
        connectIPC();
    }
}

// PlayLayer: drives the deafen state. Each frame we compute whether we should be
// deafened (desired state) and forward it to the IPC client, which only sends a
// command to Discord when the state actually changes.
class $modify(ADPlayLayer, PlayLayer) {
    struct Fields {
        autodeafen::LevelCfg m_cfg;     // this level's settings
        bool m_ready = false;           // true once the config has been loaded
        bool m_deadSuppress = false;    // keep undeafened after death until restart
        int  m_lastState = -1;          // last applied deaf state (for change logs)
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        m_fields->m_cfg = autodeafen::loadCfg(level);
        m_fields->m_lastState = -1;
        m_fields->m_deadSuppress = false;
        m_fields->m_ready = true;
        return true;
    }

    bool computeDesired() {
        // Deafen only once the config is loaded; PlayLayer::init can trigger an
        // early postUpdate with default settings, which would flicker at 0%.
        if (!m_fields->m_ready) return false;
        if (!isEnabled()) return false;
        if (!m_fields->m_cfg.enabled) return false;
        if (this->m_hasCompletedLevel) return false;
        if (m_fields->m_deadSuppress) return false;
        if (!ad::ipc::isConnected()) return false;

        // getCurrentPercentInt() is 0-100; getCurrentPercent() would be 0.0-1.0.
        float p = static_cast<float>(this->getCurrentPercentInt());
        auto const& c = m_fields->m_cfg;
        if (p < c.onPercent) return false;
        if (c.offEnabled && p >= c.offPercent) return false;
        return true;
    }

    void applyDesired() {
        bool desired = computeDesired();
        ad::ipc::setDeafen(desired);
        int state = desired ? 1 : 0;
        if (state != m_fields->m_lastState) {
            m_fields->m_lastState = state;
            log::debug("deaf={} at {}%", desired, this->getCurrentPercentInt());
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // Undeafen-on-death: once dead, stay undeafened until the run restarts
        // (percent drops back to/under the threshold). This avoids a rapid
        // mute/unmute while the dead player sits past the threshold.
        if (undeafenOnDeath()) {
            if (this->m_player1 && this->m_player1->m_isDead) {
                m_fields->m_deadSuppress = true;
            } else if (this->getCurrentPercentInt() <= static_cast<int>(m_fields->m_cfg.onPercent)) {
                m_fields->m_deadSuppress = false;
            }
        } else {
            m_fields->m_deadSuppress = false;
        }

        applyDesired();
    }

    void onQuit() {
        ad::ipc::setDeafen(false);
        PlayLayer::onQuit();
    }
};

// PauseLayer: undeafen while paused, and add the per-level settings button.
class $modify(ADPauseLayer, PauseLayer) {

    void customSetup() {
        PauseLayer::customSetup();

        // Always undeafen while paused so the user can talk; postUpdate re-applies
        // the desired state on resume.
        ad::ipc::setDeafen(false);

        // Green when connected to Discord, gray when setup is still needed.
        auto circle = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_optionsBtn_001.png", 0.8f,
            ad::ipc::isConnected() ? CircleBaseColor::Green : CircleBaseColor::Gray,
            CircleBaseSize::Small);

        auto btn = CCMenuItemSpriteExtra::create(
            circle, this, menu_selector(ADPauseLayer::onAutoDeafenSettings));
        btn->setID("autodeafen-settings-button"_spr);

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("autodeafen-menu"_spr);
        menu->setContentSize(winSize);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ 0.f, 0.f });
        menu->setPosition({ 0.f, 0.f });
        menu->addChild(btn);
        btn->setPosition({ 35.f, winSize.height - 45.f });
        this->addChild(menu);
    }

    void onAutoDeafenSettings(CCObject*) {
        if (!ad::ipc::isConnected()) {
            openConnectFlow();
            return;
        }
        auto pl = PlayLayer::get();
        if (!pl) { openConnectFlow(); return; }

        auto cfg = static_cast<ADPlayLayer*>(pl)->m_fields->m_cfg;
        auto startPositions = autodeafen::startPosPercents(pl);

        ADConfigPopup::create(cfg, startPositions, [](autodeafen::LevelCfg const& c) {
            if (auto p = PlayLayer::get()) {
                autodeafen::saveCfg(p->m_level, c);
                auto adpl = static_cast<ADPlayLayer*>(p);
                adpl->m_fields->m_cfg = c;
                adpl->m_fields->m_lastState = -1;
            }
        })->show();
    }

    void onQuit(CCObject* sender) {
        ad::ipc::setDeafen(false);
        PauseLayer::onQuit(sender);
    }
};
