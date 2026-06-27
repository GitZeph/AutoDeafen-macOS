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

using namespace geode::prelude;

namespace ad = autodeafen;

// ---------------------------------------------------------------------------
// Impostazioni + gestione token
// ---------------------------------------------------------------------------
namespace {

    constexpr const char* kRedirectUri = "http://localhost:8000";

    bool isEnabled()        { return Mod::get()->getSettingValue<bool>("enabled"); }
    int  getMinPercent()    { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("min-percent")); }
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

    // Applica lo stato di deafen solo se l'IPC è pronto e la mod è attiva.
    void applyDeafen(bool deaf) {
        if (deaf && !isEnabled()) return;
        if (!ad::ipc::isConnected()) return;
        ad::ipc::setDeafen(deaf);
        log::info("AutoDeafen: SET deaf={}", deaf);
    }

    // Popup richiamato dal bottone nel menu di pausa.
    void openAutoDeafenPopup() {
        if (ad::ipc::isConnected()) {
            createQuickPopup(
                "AutoDeafen",
                "Connesso a <cg>Discord</c>. Puoi regolare percentuale e opzioni "
                "nelle impostazioni della mod.",
                "OK", "Impostazioni",
                [](auto, bool settings) {
                    if (settings) openSettingsPopup(Mod::get());
                });
            return;
        }

        if (clientId().empty() || clientSecret().empty()) {
            createQuickPopup(
                "AutoDeafen",
                "Per funzionare serve un'app Discord tua.\n"
                "Apri le <cy>impostazioni</c> e incolla <cg>Client ID</c> e "
                "<cg>Client Secret</c>, poi premi <cb>Autorizza</c>.",
                "Annulla", "Impostazioni",
                [](auto, bool settings) {
                    if (settings) openSettingsPopup(Mod::get());
                });
            return;
        }

        createQuickPopup(
            "AutoDeafen",
            "Pronto a collegarsi a <cg>Discord</c>.\n"
            "Premendo <cb>Autorizza</c> si aprirà il browser: accetta, poi torna "
            "qui. <cy>Discord deve essere aperto.</c>",
            "Annulla", "Autorizza",
            [](auto, bool ok) {
                if (ok) startAuthFlow();
            });
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
// Hook su PlayLayer: soglia, reset, morte, uscita.
// ---------------------------------------------------------------------------
class $modify(ADPlayLayer, PlayLayer) {
    struct Fields {
        bool m_hasDeafened = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        m_fields->m_hasDeafened = false;
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (m_fields->m_hasDeafened) return;
        if (!isEnabled()) return;

        if (this->getCurrentPercentInt() >= getMinPercent()) {
            applyDeafen(true);
            m_fields->m_hasDeafened = true;
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->m_hasDeafened) {
            applyDeafen(false);
            m_fields->m_hasDeafened = false;
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (undeafenOnDeath() && m_fields->m_hasDeafened) {
            applyDeafen(false);
        }
    }

    void onQuit() {
        if (m_fields->m_hasDeafened) {
            applyDeafen(false);
            m_fields->m_hasDeafened = false;
        }
        PlayLayer::onQuit();
    }
};

// ---------------------------------------------------------------------------
// Hook su PauseLayer: smuta in pausa, rimuta alla ripresa, bottone impostazioni.
// ---------------------------------------------------------------------------
class $modify(ADPauseLayer, PauseLayer) {
    static bool wasDeafened() {
        auto pl = PlayLayer::get();
        if (!pl) return false;
        return static_cast<ADPlayLayer*>(pl)->m_fields->m_hasDeafened;
    }

    void customSetup() {
        PauseLayer::customSetup();

        if (wasDeafened()) applyDeafen(false);

        // Bottone circolare in stile GD. Il colore segnala lo stato:
        //   verde = connesso a Discord, grigio = da configurare/autorizzare.
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

        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setContentSize(btn->getContentSize());
        btn->setPosition(btn->getContentSize() / 2.f);
        menu->setAnchorPoint({ 0.f, 1.f });
        menu->setPosition({ 12.f, CCDirector::get()->getWinSize().height - 12.f });
        menu->setID("autodeafen-menu"_spr);
        this->addChild(menu);
    }

    void onAutoDeafenSettings(CCObject*) {
        openAutoDeafenPopup();
    }

    void onResume(CCObject* sender) {
        if (wasDeafened()) applyDeafen(true);
        PauseLayer::onResume(sender);
    }

    void onQuit(CCObject* sender) {
        if (wasDeafened()) applyDeafen(false);
        PauseLayer::onQuit(sender);
    }
};
