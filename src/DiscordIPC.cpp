// Discord IPC client — POSIX implementation (macOS).
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

    std::mutex        g_mutex;          // guards socket writes
    int               g_fd = -1;        // Discord socket file descriptor
    std::atomic<bool> g_authenticated{false};
    std::atomic<bool> g_running{false}; // controls the drain thread
    std::thread       g_drainThread;

    // Last deafen state sent (-1 = unknown). Avoids resending identical commands
    // every frame; reset to -1 on every (re)connect since Discord's real state
    // after a reconnect is unknown.
    std::atomic<int>  g_lastDeafen{-1};

    std::string joinPath(std::string dir, int n) {
        if (!dir.empty() && dir.back() == '/') dir.pop_back();
        return dir + "/discord-ipc-" + std::to_string(n);
    }

    // Discord exposes up to 10 sockets (discord-ipc-0 .. discord-ipc-9), usually
    // in $TMPDIR (a sandbox dir like /var/folders/.../T/ on macOS).
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

    // Connect to any available discord-ipc-N socket.
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

    // Write exactly `len` bytes, handling partial writes and EINTR.
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

    // Read exactly `len` bytes.
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

    // Send one [opcode][length][payload] frame. Thread-safe.
    bool sendFrame(int fd, int32_t opcode, std::string const& json) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (fd < 0) return false;

        int32_t op  = opcode;
        int32_t len = static_cast<int32_t>(json.size());
        // Discord IPC expects little-endian integers; Apple Silicon/Intel are LE.
        if (!writeAll(fd, &op, 4))  return false;
        if (!writeAll(fd, &len, 4)) return false;
        if (len > 0 && !writeAll(fd, json.data(), static_cast<size_t>(len)))
            return false;
        return true;
    }

    // Read and discard one frame; we only care that it arrived.
    bool drainFrame(int fd) {
        int32_t opcode = 0, length = 0;
        if (!readAll(fd, &opcode, 4)) return false;
        if (!readAll(fd, &length, 4)) return false;
        if (length < 0)               return false;
        if (length > 0) {
            std::vector<char> payload(static_cast<size_t>(length));
            if (!readAll(fd, payload.data(), static_cast<size_t>(length)))
                return false;
        }
        return true;
    }

    // Discord sends many unsolicited events; if we never read them the socket
    // buffer eventually blocks our writes. This loop consumes and discards them.
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

} // namespace

bool connect(std::string const& clientId,
             std::string const& accessToken,
             std::string& errOut) {
    disconnect(); // start clean

    if (clientId.empty())    { errOut = "missing Client ID"; return false; }
    if (accessToken.empty()) { errOut = "missing access token"; return false; }

    int fd = openDiscordSocket();
    if (fd < 0) {
        errOut = "could not reach Discord's IPC socket (is Discord running?)";
        return false;
    }

    // 1) Handshake.
    std::string handshake = R"({"v":1,"client_id":")" + clientId + R"("})";
    if (!sendFrame(fd, 0, handshake) || !drainFrame(fd)) {
        ::close(fd);
        errOut = "IPC handshake failed";
        return false;
    }

    // 2) Authenticate with the OAuth access token.
    std::string authenticate =
        R"({"cmd":"AUTHENTICATE","args":{"access_token":")" + accessToken +
        R"("},"nonce":"auth"})";
    if (!sendFrame(fd, 1, authenticate) || !drainFrame(fd)) {
        ::close(fd);
        errOut = "AUTHENTICATE failed (expired token or wrong scopes?)";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_fd = fd;
    }
    g_authenticated.store(true);
    g_running.store(true);
    g_lastDeafen.store(-1);   // real state unknown: force the first send
    g_drainThread = std::thread(drainLoop, fd);
    g_drainThread.detach();

    return true;
}

bool isConnected() {
    return g_authenticated.load() && g_fd >= 0;
}

void setDeafen(bool deaf) {
    if (!isConnected()) return;

    // Only send on a real change, so the caller can invoke this every frame.
    int want = deaf ? 1 : 0;
    if (g_lastDeafen.exchange(want) == want) return;

    int fd = g_fd;
    std::string payload =
        R"({"cmd":"SET_VOICE_SETTINGS","args":{"deaf":)"
        + std::string(deaf ? "true" : "false")
        + R"(},"nonce":"deafen"})";

    // Write off the main thread so socket I/O never stalls the render loop.
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
