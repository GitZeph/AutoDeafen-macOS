// ---------------------------------------------------------------------------
// AutoDeafen - Client Discord IPC (implementazione POSIX, macOS)
// ---------------------------------------------------------------------------
#include "DiscordIPC.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

namespace autodeafen {
namespace ipc {

namespace {

    std::mutex        g_mutex;          // protegge le scritture sulla socket
    int               g_fd = -1;        // file descriptor della socket Discord
    std::atomic<bool> g_authenticated{false};
    std::atomic<bool> g_running{false}; // controlla il thread di drain
    std::thread       g_drainThread;

    // Ultimo stato deafen inviato (-1 = sconosciuto). Serve a non rispedire
    // comandi identici ogni frame; va resettato a -1 ad ogni (ri)connessione,
    // perché lo stato reale di Discord dopo una riconnessione è ignoto.
    std::atomic<int>  g_lastDeafen{-1};

    // Costruisce il path "<dir>/discord-ipc-<n>" gestendo lo slash finale.
    std::string joinPath(std::string dir, int n) {
        if (!dir.empty() && dir.back() == '/') dir.pop_back();
        return dir + "/discord-ipc-" + std::to_string(n);
    }

    // Discord espone fino a 10 socket (discord-ipc-0 .. discord-ipc-9), di
    // solito in $TMPDIR (su macOS è una dir sandbox tipo /var/folders/.../T/).
    // Proviamo $TMPDIR, poi i classici fallback.
    std::vector<std::string> candidateDirs() {
        std::vector<std::string> dirs;
        auto add = [&](const char* env) {
            if (const char* v = std::getenv(env); v && *v) dirs.emplace_back(v);
        };
        add("TMPDIR");
        add("XDG_RUNTIME_DIR");
        dirs.emplace_back("/tmp");
        return dirs;
    }

    // Apre una connessione a uno qualsiasi dei socket discord-ipc-N.
    int openDiscordSocket() {
        for (auto const& dir : candidateDirs()) {
            for (int n = 0; n < 10; ++n) {
                std::string path = joinPath(dir, n);
                if (path.size() >= sizeof(sockaddr_un{}.sun_path)) continue;

                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) continue;

                sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

                if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    return fd;
                }
                ::close(fd);
            }
        }
        return -1;
    }

    // Scrive esattamente `len` byte (gestendo le scritture parziali).
    bool writeAll(int fd, const void* buf, size_t len) {
        const char* p = static_cast<const char*>(buf);
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::write(fd, p + sent, len - sent);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // Legge esattamente `len` byte.
    bool readAll(int fd, void* buf, size_t len) {
        char* p = static_cast<char*>(buf);
        size_t got = 0;
        while (got < len) {
            ssize_t n = ::read(fd, p + got, len - got);
            if (n == 0) return false; // EOF
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            got += static_cast<size_t>(n);
        }
        return true;
    }

    // Invia un frame [opcode][length][payload]. Thread-safe.
    bool sendFrame(int fd, int32_t opcode, std::string const& json) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (fd < 0) return false;

        int32_t op  = opcode;
        int32_t len = static_cast<int32_t>(json.size());
        // Discord IPC usa interi little-endian; su Apple Silicon/Intel siamo già LE.
        if (!writeAll(fd, &op, 4))  return false;
        if (!writeAll(fd, &len, 4)) return false;
        if (len > 0 && !writeAll(fd, json.data(), static_cast<size_t>(len)))
            return false;
        return true;
    }

    // Legge e scarta un frame (ci interessa solo che sia arrivato).
    bool drainFrame(int fd) {
        int32_t opcode = 0, length = 0;
        if (!readAll(fd, &opcode, 4))  return false;
        if (!readAll(fd, &length, 4))  return false;
        if (length < 0)                return false;
        if (length > 0) {
            std::vector<char> payload(static_cast<size_t>(length));
            if (!readAll(fd, payload.data(), static_cast<size_t>(length)))
                return false;
        }
        return true;
    }

    // Discord manda parecchi eventi non richiesti: se non li leggiamo, prima o
    // poi le scritture si bloccano. Questo loop li consuma e li butta via.
    void drainLoop(int fd) {
        while (g_running.load()) {
            if (!drainFrame(fd)) break;
        }
        g_authenticated.store(false);
    }

    void closeLocked() {
        g_running.store(false);
        g_authenticated.store(false);
        if (g_fd >= 0) {
            ::shutdown(g_fd, SHUT_RDWR);
            ::close(g_fd);
            g_fd = -1;
        }
    }

} // anonymous namespace

bool connect(std::string const& clientId,
             std::string const& accessToken,
             std::string& errOut) {
    disconnect(); // riparti pulito

    if (clientId.empty()) { errOut = "Client ID mancante"; return false; }
    if (accessToken.empty()) { errOut = "Access token mancante"; return false; }

    int fd = openDiscordSocket();
    if (fd < 0) {
        errOut = "Impossibile connettersi alla socket IPC di Discord "
                 "(Discord è aperto?)";
        return false;
    }

    // 1) HANDSHAKE
    std::string handshake =
        R"({"v":1,"client_id":")" + clientId + R"("})";
    if (!sendFrame(fd, 0, handshake) || !drainFrame(fd)) {
        ::close(fd);
        errOut = "Handshake IPC fallito";
        return false;
    }

    // 2) AUTHENTICATE con l'access_token OAuth.
    std::string authenticate =
        R"({"cmd":"AUTHENTICATE","args":{"access_token":")" + accessToken +
        R"("},"nonce":"auth"})";
    if (!sendFrame(fd, 1, authenticate) || !drainFrame(fd)) {
        ::close(fd);
        errOut = "AUTHENTICATE fallito (token scaduto o scope errati?)";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_fd = fd;
    }
    g_authenticated.store(true);
    g_running.store(true);
    g_lastDeafen.store(-1);   // stato reale ignoto: forza il primo invio
    g_drainThread = std::thread(drainLoop, fd);
    g_drainThread.detach();

    return true;
}

bool isConnected() {
    return g_authenticated.load() && g_fd >= 0;
}

void setDeafen(bool deaf) {
    if (!isConnected()) return;

    // Dedup: inviamo solo quando lo stato cambia davvero. Così il chiamante può
    // invocare setDeafen() ad ogni frame senza spammare la socket.
    int want = deaf ? 1 : 0;
    if (g_lastDeafen.exchange(want) == want) return;

    int fd = g_fd;
    std::string payload =
        R"({"cmd":"SET_VOICE_SETTINGS","args":{"deaf":)"
        + std::string(deaf ? "true" : "false")
        + R"(},"nonce":"deafen"})";

    // Inviamo su un thread dedicato: l'I/O sulla socket non deve mai bloccare
    // il render loop del gioco.
    std::thread([fd, payload] {
        sendFrame(fd, 1, payload);
    }).detach();
}

void disconnect() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lastDeafen.store(-1);
    closeLocked();
}

} // namespace ipc
} // namespace autodeafen
