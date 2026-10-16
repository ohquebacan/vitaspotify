#pragma once

// Spotify's OAuth 2.0 device authorization grant (RFC 8628), run on the Vita
// itself. The console shows a short code, the user types it in at
// spotify.com/pair on a phone or a computer, and the console learns the outcome
// only by polling. No password is ever typed on the Vita and none passes
// through this code.
//
// This replaces the auth helper that had to be run on a computer: the access
// token it yields is good for one AP login with
// AUTHENTICATION_SPOTIFY_TOKEN, and that login hands back the reusable
// credentials the app already saves to auth.json. So the token is needed once
// and never stored.
//
// Only the desktop client ID is enabled for this flow; the Android and iOS IDs
// are refused, and report it as invalid_scope rather than as
// unauthorized_client, which misleads.

#include <string>

#define SPOTIFY_PAIR_CLIENT_ID "65b708073fc0480ea92a077233ca87bd"

// What this client asks for, and nothing more: playback, the library and the
// playlists it lists, plus the account's country and name, which the access
// point wants. Spotify validates the whole list at once -- one unrecognised
// scope fails the request -- so this cannot be probed by asking for extras.
#define SPOTIFY_PAIR_SCOPES                                     \
    "streaming playlist-read-private playlist-read-collaborative " \
    "user-library-read user-library-modify user-read-email user-read-private"

struct PairRequest {
    std::string user_code;          // shown to the user
    std::string verification_uri;   // where they type it in
    std::string device_code;        // secret; never display or log it
    int interval_s = 5;
    int expires_in_s = 3599;
};

enum class PairStatus {
    Pending,    // the user has not acted yet; wait interval_s
    SlowDown,   // polled too fast; back off and keep waiting
    Ok,
    Denied,     // the user refused
    Expired,    // the code timed out; start over
    Error,
};

// Asks Spotify for a code. Returns false with `err` set.
bool pair_begin(PairRequest &out, std::string &err);

// One poll. Pending and SlowDown are normal and expected.
PairStatus pair_poll(const PairRequest &req, std::string &access_token,
                     std::string &err);

// The auth.json an access token logs in with: authType 3
// (AUTHENTICATION_SPOTIFY_TOKEN) and no username, which the access point fills
// in from the token itself. The app overwrites this file with the reusable
// credentials as soon as that login succeeds, so it is short-lived.
std::string pair_credentials_json(const std::string &access_token);

// Whether a saved auth.json can actually log in again. A stored credential is
// sent with the account name alongside it, so one saved without a name -- as
// the first on-console builds did, before the name was read off APWelcome --
// is rejected by the access point and has to be replaced by pairing again.
// Offering to resume it would just fail on every launch.
bool pair_credentials_resumable(const std::string &json);
