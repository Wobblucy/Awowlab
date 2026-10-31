#pragma once

#include "AwowLabBuildOptions.h"

#include <string>

namespace warcraftlogs {

// Application-level WarcraftLogs settings shared by every user. Each user
// gets their own access token at sign-in.
namespace credentials {

    // A public client using PKCE, so there is no client secret. Set at
    // configure time with AWOWLAB_WCL_CLIENT_ID (docs/BUILD-OPTIONS.md).
    constexpr const char* CLIENT_ID = AWOWLAB_WCL_CLIENT_ID;

    // PKCE sends a code_verifier instead.
    constexpr const char* CLIENT_SECRET = "";

    constexpr const char* OAUTH_AUTHORIZE_URL = "https://www.warcraftlogs.com/oauth/authorize";
    constexpr const char* OAUTH_TOKEN_URL = "https://www.warcraftlogs.com/oauth/token";

    constexpr const char* GRAPHQL_CLIENT_ENDPOINT = "https://www.warcraftlogs.com/api/v2/client";
    constexpr const char* GRAPHQL_USER_ENDPOINT = "https://www.warcraftlogs.com/api/v2/user";

    constexpr const char* API_HOST = "www.warcraftlogs.com";
    constexpr const char* API_PORT = "443";

    // Sign-in tries these in order until one binds. Each must be registered
    // as http://localhost:<port>/callback on the WarcraftLogs client. Set
    // with wcl_redirect_ports (docs/BUILD-OPTIONS.md).
    constexpr int REDIRECT_PORTS[] = {AWOWLAB_WCL_REDIRECT_PORTS};

    inline std::string getRedirectUri(int port) {
        return "http://localhost:" + std::to_string(port) + "/callback";
    }

} // namespace credentials

} // namespace warcraftlogs
