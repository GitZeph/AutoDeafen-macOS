#pragma once

#include <string>
#include <functional>

// Tiny HTTP server for the OAuth redirect.
//
// After the user authorizes, Discord redirects the browser to
// http://localhost:8000/?code=XXXX. We listen on 127.0.0.1:8000, read that
// request, extract the "code" and hand it to the callback, then serve a small
// HTML confirmation page. Everything runs on a worker thread.

namespace autodeafen {
namespace oauth {

    // Start the redirect server on 127.0.0.1:8000 (only one at a time).
    // onResult is invoked (on a worker thread) with (code, error):
    //   - non-empty code, empty error -> authorization succeeded
    //   - empty code, non-empty error -> failure (timeout, Discord error, ...)
    void startRedirectServer(std::function<void(std::string code,
                                                std::string error)> onResult);

} // namespace oauth
} // namespace autodeafen
