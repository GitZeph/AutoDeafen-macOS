#pragma once

#include <string>
#include <functional>

// Discord IPC client (macOS / POSIX).
//
// Instead of simulating the deafen hotkey (which Discord ignores for synthetic
// events), this talks to the Discord client directly over its local IPC socket
// (a Unix domain socket at $TMPDIR/discord-ipc-N).
//
// Discord RPC framing:
//   each frame = [int32 opcode LE][int32 length LE][UTF-8 JSON payload]
//   opcode 0 = HANDSHAKE  -> {"v":1,"client_id":"..."}
//   opcode 1 = FRAME      -> commands, e.g. AUTHENTICATE / SET_VOICE_SETTINGS
//
// SET_VOICE_SETTINGS needs an OAuth access token with the "rpc rpc.voice.write"
// scopes; it is obtained elsewhere (see the OAuth flow in main.cpp) and passed
// to connect(). This header intentionally has no Geode dependency.

namespace autodeafen {
namespace ipc {

    // Blocking connect + handshake + AUTHENTICATE. Call from a worker thread,
    // never from the game's main thread. Returns true once authenticated;
    // `errOut` holds a human-readable reason on failure.
    bool connect(std::string const& clientId,
                 std::string const& accessToken,
                 std::string& errOut);

    // True if the socket is open and authentication succeeded.
    bool isConnected();

    // Send SET_VOICE_SETTINGS {deaf: ...}. No-op if not connected. The actual
    // socket write happens on a worker thread so the caller never blocks.
    void setDeafen(bool deaf);

    // Close the socket and stop the drain thread.
    void disconnect();

} // namespace ipc
} // namespace autodeafen
