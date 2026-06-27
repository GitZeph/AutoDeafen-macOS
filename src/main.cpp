// ---------------------------------------------------------------------------
// AutoDeafen - Hook di Geode + flusso OAuth + collegamento a Discord IPC
// ---------------------------------------------------------------------------
// Approccio: NON simuliamo più la hotkey di Discord (Discord ignora gli eventi
// tastiera sintetici). Parliamo direttamente al client Discord via IPC e gli
// diciamo di mutare/smutare con SET_VOICE_SETTINGS.
//
// Per poterlo fare serve un access_token OAuth con scope "rpc rpc.voice.write".
// Dato che gli scope RPC sono "whitelist-only" TRANNE che per il proprietario
// dell'app, ogni utente deve creare la PROPRIA applicazione Discord (gratis) e
// incollare Client ID + Client Secret nelle impostazioni della mod.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Impostazioni + gestione token
// ---------------------------------------------------------------------------
namespace {

    constexpr const char* kRedirectUri = "http://localhost:8000";

    bool isEnabled()        { return Mod::get()->getSettingValue<bool>("enabled"); }
    bool undeafenOnDeath()  { return Mod::get()->getSettingValue<bool>("undeafen-on-death"); }
    std::string clientId()  { return Mod::get()->getSettingValue<std::string>("client-id"); }
    std::string clientSecret() { return Mod::get()->getSettingValue<std::string>("client-secret"); }

    long long nowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Feedback all'utente, sempre sul main thread.
    void notify(std::string text, bool success) {
        Loader::get()->queueInMainThread([text = std::move(text), success] {
            Notification::create(text,
                success ? NotificationIcon::Success : NotificationIcon::Error)
                ->show();
        });
    }

    // Connette l'IPC usando il token salvato (su thread secondario).
    void connectIPC() {
        auto token = Mod::get()->getSavedValue<std::string>("access-token");
        auto id = clientId();
        if (token.empty() || id.empty()) return;

        std::thread([id, token] {
            std::string err;
            if (ad::ipc::connect(id, token, err)) {
                log::info("AutoDeafen: connesso a Discord via IPC");
                notify("AutoDeafen: connesso a Discord", true);
            } else {
                log::error("AutoDeafen: connessione IPC fallita: {}", err);
                notify("AutoDeafen: " + err, false);
            }
        }).detach();
    }

    // Esegue una richiesta al token endpoint di Discord (su thread secondario).
    // `isRefresh` distingue il primo scambio (authorization_code) dal refresh.
    void requestToken(std::string body) {
        std::thread([body = std::move(body)] {
            auto req = web::WebRequest();
            req.header("Content-Type", "application/x-www-form-urlencoded");
            req.bodyString(body);

            auto res = req.postSync("https://discord.com/api/oauth2/token");
            if (!res.ok()) {
                std::string detail = res.string().unwrapOr("");
                log::error("AutoDeafen: richiesta token fallita (HTTP {}): {}",
                    res.code(), detail);
                notify("AutoDeafen: autenticazione fallita (HTTP "
                    + std::to_string(res.code()) + ")", false);
                return;
            }

            auto jsonRes = res.json();
            if (!jsonRes) {
                log::error("AutoDeafen: risposta token non è JSON valido");
                notify("AutoDeafen: risposta di Discord non valida", false);
                return;
            }
            auto json = jsonRes.unwrap();

            std::string access  = json["access_token"].asString().unwrapOr("");
            std::string refresh = json["refresh_token"].asString().unwrapOr("");
            long long expiresIn = json["expires_in"].asInt().unwrapOr(0);

            if (access.empty()) {
                log::error("AutoDeafen: nessun access_token nella risposta");
                notify("AutoDeafen: token non ricevuto", false);
                return;
            }

            Mod::get()->setSavedValue<std::string>("access-token", access);
            Mod::get()->setSavedValue<std::string>("refresh-token", refresh);
            Mod::get()->setSavedValue<int64_t>("token-expiry", nowSeconds() + expiresIn);

            log::info("AutoDeafen: token ottenuto (scade tra {}s)", expiresIn);
            connectIPC();
        }).detach();
    }

    void exchangeCodeForToken(std::string const& code) {
        std::string body =
            "client_id=" + clientId() +
            "&client_secret=" + clientSecret() +
            "&grant_type=authorization_code"
            "&code=" + code +
            "&redirect_uri=" + kRedirectUri;
        requestToken(body);
    }

    void refreshToken() {
        auto refresh = Mod::get()->getSavedValue<std::string>("refresh-token");
        if (refresh.empty()) return;
        std::string body =
            "client_id=" + clientId() +
            "&client_secret=" + clientSecret() +
            "&grant_type=refresh_token"
            "&refresh_token=" + refresh;
        requestToken(body);
    }

    // Avvia l'intero flusso: server di redirect + apertura browser su Discord.
    void startAuthFlow() {
        if (clientId().empty() || clientSecret().empty()) {
            notify("AutoDeafen: imposta prima Client ID e Client Secret", false);
            return;
        }

        ad::oauth::startRedirectServer([](std::string code, std::string error) {
            if (code.empty()) {
                log::error("AutoDeafen: OAuth fallito: {}", error);
                notify("AutoDeafen: autorizzazione fallita (" + error + ")", false);
                return;
            }
            log::info("AutoDeafen: ricevuto code OAuth, scambio in corso");
            exchangeCodeForToken(code);
        });

        std::string url =
            "https://discord.com/oauth2/authorize?client_id=" + clientId() +
            "&response_type=code"
            "&redirect_uri=http%3A%2F%2Flocalhost%3A8000"
            "&scope=rpc.voice.write+rpc";

        web::openLinkInBrowser(url);
        notify("AutoDeafen: autorizza nel browser, poi torna al gioco", true);
    }

    // Flusso di onboarding/connessione, mostrato quando NON siamo connessi.
    // NB: nessun campo Client ID/Secret qui — si impostano nelle impostazioni
    // Geode della mod. Qui ci sono solo azioni.
    void openConnectFlow() {
        if (clientId().empty() || clientSecret().empty()) {
            createQuickPopup(
                "AutoDeafen",
                "To work, AutoDeafen needs your own Discord app.\n"
                "Open the <cy>mod settings</c>, paste <cg>Client ID</c> and "
                "<cg>Client Secret</c>, then come back and press <cb>Authorize</c>.",
                "Cancel", "Settings",
                [](auto, bool settings) {
                    if (settings) openSettingsPopup(Mod::get());
                });
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// All'avvio: se abbiamo un token salvato, connettiamo (rinfrescando se scaduto).
// ---------------------------------------------------------------------------
$on_mod(Loaded) {
    if (!Mod::get()->hasSavedValue("access-token")) {
        log::info("AutoDeafen caricata: nessun token salvato, serve setup.");
        return;
    }

    long long expiry = Mod::get()->getSavedValue<int64_t>("token-expiry");
    if (nowSeconds() >= expiry && Mod::get()->hasSavedValue("refresh-token")) {
        log::info("AutoDeafen: token scaduto, refresh in corso");
        refreshToken();
    } else {
        connectIPC();
    }
}

// ---------------------------------------------------------------------------
// Hook su PlayLayer: soglia per-livello, cutscene (off), reset, morte, uscita.
// Modello "stato desiderato": ad ogni frame calcoliamo se DOVREMMO essere
// deafened e inviamo a Discord solo quando lo stato cambia.
// ---------------------------------------------------------------------------
class $modify(ADPlayLayer, PlayLayer) {
    struct Fields {
        autodeafen::LevelCfg m_cfg;     // config di questo livello
        bool m_ready = false;           // true solo dopo aver caricato la config
        bool m_deadSuppress = false;    // smutato dopo la morte finché non si ricomincia
        int  m_lastLogged = -1;         // per loggare solo i cambi di stato
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        m_fields->m_cfg = autodeafen::loadCfg(level);
        m_fields->m_lastLogged = -1;
        m_fields->m_deadSuppress = false;
        m_fields->m_ready = true;       // da qui in poi possiamo deafennare
        auto const& c = m_fields->m_cfg;
        log::info("AutoDeafen: cfg loaded key={} enabled={} on={:.1f} offEn={} off={:.1f}",
                  autodeafen::levelKey(level), c.enabled, c.onPercent, c.offEnabled, c.offPercent);
        return true;
    }

    bool computeDesired() {
        if (!m_fields->m_ready) return false;
        if (!isEnabled()) return false;
        if (!m_fields->m_cfg.enabled) return false;
        if (this->m_hasCompletedLevel) return false;
        if (m_fields->m_deadSuppress) return false;   // undeafen-on-death attivo
        if (!ad::ipc::isConnected()) return false;

        // getCurrentPercentInt() è 0-100 (getCurrentPercent() invece è 0.0-1.0).
        float p = static_cast<float>(this->getCurrentPercentInt());
        auto const& c = m_fields->m_cfg;
        if (p < c.onPercent) return false;
        if (c.offEnabled && p >= c.offPercent) return false;
        return true;
    }

    void applyDesired() {
        bool desired = computeDesired();
        ad::ipc::setDeafen(desired);   // l'IPC dedupa: invia solo ai cambi reali
        int d = desired ? 1 : 0;
        if (d != m_fields->m_lastLogged) {
            m_fields->m_lastLogged = d;
            auto const& c = m_fields->m_cfg;
            log::info("AutoDeafen: deaf={} (perc={} on={:.1f} offEn={} off={:.1f})",
                      desired, this->getCurrentPercentInt(), c.onPercent, c.offEnabled, c.offPercent);
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // Undeafen-on-death: alla morte restiamo smutati FINCHÉ non ricominciamo
        // (la percentuale torna a/sotto la soglia). Così, se muori sopra la soglia,
        // non senti il mute/smute ripetuto mentre il corpo resta lì fermo.
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

// ---------------------------------------------------------------------------
// Hook su PauseLayer: smuta in pausa, bottone impostazioni per-livello.
// ---------------------------------------------------------------------------
class $modify(ADPauseLayer, PauseLayer) {

    void customSetup() {
        PauseLayer::customSetup();

        // In pausa smutiamo sempre (così puoi parlare). Alla ripresa postUpdate
        // ricalcola lo stato desiderato e l'IPC ri-invia il deafen se serve.
        ad::ipc::setDeafen(false);

        // Bottone circolare in stile GD. Verde = connesso, grigio = da configurare.
        bool connected = ad::ipc::isConnected();
        auto circle = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_optionsBtn_001.png",
            0.8f,
            connected ? CircleBaseColor::Green : CircleBaseColor::Gray,
            CircleBaseSize::Small
        );

        auto btn = CCMenuItemSpriteExtra::create(
            circle, this, menu_selector(ADPauseLayer::onAutoDeafenSettings));
        btn->setID("autodeafen-settings-button"_spr);

        // Menu a tutto schermo con origine in basso a sinistra: coordinate assolute.
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
        // Non connessi -> flusso di connessione (senza campi credenziali).
        if (!ad::ipc::isConnected()) {
            openConnectFlow();
            return;
        }
        auto pl = PlayLayer::get();
        if (!pl) { openConnectFlow(); return; }

        auto cfg = static_cast<ADPlayLayer*>(pl)->m_fields->m_cfg;
        auto sps = autodeafen::startPosPercents(pl);

        ADConfigPopup::create(cfg, sps, [](autodeafen::LevelCfg const& c) {
            if (auto p = PlayLayer::get()) {
                autodeafen::saveCfg(p->m_level, c);
                static_cast<ADPlayLayer*>(p)->m_fields->m_cfg = c;
                static_cast<ADPlayLayer*>(p)->m_fields->m_lastLogged = -1;
                log::info("AutoDeafen: cfg saved enabled={} on={:.1f} offEn={} off={:.1f}",
                          c.enabled, c.onPercent, c.offEnabled, c.offPercent);
            }
        })->show();
    }

    void onQuit(CCObject* sender) {
        ad::ipc::setDeafen(false);
        PauseLayer::onQuit(sender);
    }
};
