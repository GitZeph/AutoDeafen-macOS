// Tiny HTTP server for the OAuth redirect — POSIX implementation (macOS).
#include "OAuthServer.hpp"

#include <atomic>
#include <thread>
#include <string>
#include <cstring>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

namespace autodeafen {
namespace oauth {

namespace {

    // Prevents opening two servers on the same port at once.
    std::atomic<bool> g_serverRunning{false};

    constexpr int kPort        = 8000;
    constexpr int kRecvTimeout = 120; // seconds to wait for the redirect

    // Minimal percent-decoding. OAuth codes are URL-safe, but handle %XX anyway.
    std::string urlDecode(std::string const& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size()) {
                auto hex = in.substr(i + 1, 2);
                out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
                i += 2;
            } else if (in[i] == '+') {
                out.push_back(' ');
            } else {
                out.push_back(in[i]);
            }
        }
        return out;
    }

    // Extract a query parameter value from the "GET /?..." request line.
    std::string extractParam(std::string const& request, std::string const& key) {
        std::string needle = key + "=";
        size_t pos = request.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        size_t end = request.find_first_of("& \r\n", pos);
        if (end == std::string::npos) end = request.size();
        return urlDecode(request.substr(pos, end - pos));
    }

    void sendHttp(int fd, std::string const& title, std::string const& body) {
        std::string html =
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<title>AutoDeafen</title></head>"
            "<body style='font-family:sans-serif;text-align:center;margin-top:60px'>"
            "<h2>" + title + "</h2><p>" + body + "</p></body></html>";
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(html.size()) + "\r\n"
            "Connection: close\r\n\r\n" + html;
        ::send(fd, resp.data(), resp.size(), 0);
    }

} // namespace

void startRedirectServer(std::function<void(std::string, std::string)> onResult) {
    bool expected = false;
    if (!g_serverRunning.compare_exchange_strong(expected, true)) {
        return; // a server is already running
    }

    std::thread([onResult = std::move(onResult)] {
        auto finish = [&](std::string code, std::string error) {
            g_serverRunning.store(false);
            if (onResult) onResult(std::move(code), std::move(error));
        };

        int lsock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (lsock < 0) { finish("", "socket() failed"); return; }

        int yes = 1;
        ::setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only

        if (::bind(lsock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(lsock);
            finish("", "port 8000 is busy (bind failed)");
            return;
        }
        if (::listen(lsock, 1) != 0) {
            ::close(lsock);
            finish("", "listen() failed");
            return;
        }

        // Accept timeout so we never hang forever.
        timeval tv{ kRecvTimeout, 0 };
        ::setsockopt(lsock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int csock = ::accept(lsock, nullptr, nullptr);
        if (csock < 0) {
            ::close(lsock);
            finish("", "no redirect received (timeout)");
            return;
        }

        ::setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buffer[8192];
        ssize_t n = ::recv(csock, buffer, sizeof(buffer) - 1, 0);
        std::string request(buffer, n > 0 ? static_cast<size_t>(n) : 0);

        std::string code  = extractParam(request, "code");
        std::string error = extractParam(request, "error");

        if (!code.empty()) {
            sendHttp(csock, "All set!",
                     "You can close this tab and return to Geometry Dash.");
        } else {
            sendHttp(csock, "Authorization error",
                     "Discord did not return a code. Please try again.");
        }

        ::close(csock);
        ::close(lsock);

        if (!code.empty()) finish(code, "");
        else               finish("", error.empty() ? "no code received" : error);
    }).detach();
}

} // namespace oauth
} // namespace autodeafen
