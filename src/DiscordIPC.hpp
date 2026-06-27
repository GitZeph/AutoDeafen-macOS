#pragma once

#include <string>
#include <functional>

// ---------------------------------------------------------------------------
// AutoDeafen - Client Discord IPC (macOS / POSIX)
// ---------------------------------------------------------------------------
// Invece di simulare la hotkey di deafen (cosa che Discord ignora se l'evento
// è sintetico), parliamo DIRETTAMENTE con il client Discord tramite la sua
// socket IPC locale (Unix domain socket in $TMPDIR/discord-ipc-N).
//
// Protocollo (Discord RPC over IPC):
//   ogni frame = [int32 opcode LE][int32 length LE][payload JSON UTF-8]
//   opcode 0 = HANDSHAKE   -> {"v":1,"client_id":"..."}
//   opcode 1 = FRAME       -> comandi, es. AUTHENTICATE / SET_VOICE_SETTINGS
//
// Per usare SET_VOICE_SETTINGS serve un access_token OAuth con scope
// "rpc rpc.voice.write". Lo otteniamo a parte (vedi OAuth in main.cpp) e lo
// passiamo qui in connect().
//
// Questo header è volutamente privo di dipendenze da Geode.
// ---------------------------------------------------------------------------

namespace autodeafen {
namespace ipc {

    // Tenta connessione + handshake + AUTHENTICATE in modo BLOCCANTE.
    // Va chiamata da un thread secondario (non dal main thread del gioco).
    //   clientId    : Client ID dell'applicazione Discord dell'utente
    //   accessToken : access_token OAuth ottenuto in precedenza
    //   errOut      : messaggio d'errore in caso di fallimento
    // Ritorna true se siamo connessi e autenticati.
    bool connect(std::string const& clientId,
                 std::string const& accessToken,
                 std::string& errOut);

    // true se la socket è aperta e l'autenticazione è andata a buon fine.
    bool isConnected();

    // Invia SET_VOICE_SETTINGS {deaf: ...}. No-op se non connessi.
    // L'invio avviene su un thread dedicato per non bloccare il chiamante.
    void setDeafen(bool deaf);

    // Chiude la socket e ferma il thread di drain.
    void disconnect();

} // namespace ipc
} // namespace autodeafen
