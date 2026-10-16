#include "Pairing.h"

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Logger.h>
#include "cJSON.h"

namespace {

constexpr const char *kDeviceUrl = "https://accounts.spotify.com/oauth2/device/authorize";
constexpr const char *kTokenUrl = "https://accounts.spotify.com/api/token";

struct Reply {
    std::string body;
    long status = 0;
};

size_t write_cb(void *data, size_t size, size_t nmemb, void *userp) {
    const size_t n = size * nmemb;
    static_cast<std::string *>(userp)->append(static_cast<char *>(data), n);
    return n;
}

bool post_form(const char *url, const std::string &body, Reply &out,
               std::string &err) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        err = "could not start an HTTPS request";
        return false;
    }
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs,
                             "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void *>(&out.body));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err = curl_easy_strerror(res);
        return false;
    }
    return true;
}

// Reads one string field, so a response that is not JSON at all (a captive
// portal, an HTML error page) is a missing field rather than a crash.
bool json_str(const std::string &body, const char *key, std::string &out) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return false;
    }
    bool ok = false;
    const cJSON *item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        out = item->valuestring;
        ok = !out.empty();
    }
    cJSON_Delete(root);
    return ok;
}

int json_int(const std::string &body, const char *key, int fallback) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return fallback;
    }
    const cJSON *item = cJSON_GetObjectItem(root, key);
    const int v = cJSON_IsNumber(item) ? item->valueint : fallback;
    cJSON_Delete(root);
    return v;
}

// Spotify explains what went wrong in the body, not the status line, so the
// body is what the user is shown.
void describe(const Reply &r, std::string &err) {
    std::string code, desc;
    if (json_str(r.body, "error", code)) {
        json_str(r.body, "error_description", desc);
        err = desc.empty() ? code : code + ": " + desc;
    } else {
        err = "Spotify answered " + std::to_string(r.status);
    }
}

std::string url_encode(const std::string &in) {
    std::string out;
    char buf[4];
    for (const unsigned char c : in) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

std::string base64(const std::string &in) {
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8) |
                           static_cast<uint8_t>(in[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >> 6) & 0x3F]);
        out.push_back(tbl[v & 0x3F]);
    }
    if (i < in.size()) {
        uint32_t v = static_cast<uint8_t>(in[i]) << 16;
        const bool two = i + 1 < in.size();
        if (two) {
            v |= static_cast<uint8_t>(in[i + 1]) << 8;
        }
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(two ? tbl[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

}  // namespace

bool pair_begin(PairRequest &out, std::string &err) {
    out = PairRequest{};

    const std::string body = std::string("client_id=") + SPOTIFY_PAIR_CLIENT_ID +
                             "&scope=" + url_encode(SPOTIFY_PAIR_SCOPES);

    Reply r;
    if (!post_form(kDeviceUrl, body, r, err)) {
        return false;
    }
    if (r.status != 200) {
        describe(r, err);
        return false;
    }

    const bool ok = json_str(r.body, "user_code", out.user_code) &&
                    json_str(r.body, "device_code", out.device_code) &&
                    json_str(r.body, "verification_uri", out.verification_uri);
    out.interval_s = json_int(r.body, "interval", 5);
    out.expires_in_s = json_int(r.body, "expires_in", 3599);

    if (!ok) {
        err = "the device response was missing a required field";
        return false;
    }
    CSPOT_LOG(info, "Pairing code issued, valid for %ds", out.expires_in_s);
    return true;
}

PairStatus pair_poll(const PairRequest &req, std::string &access_token,
                     std::string &err) {
    const std::string body =
        std::string("client_id=") + SPOTIFY_PAIR_CLIENT_ID +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code" +
        "&device_code=" + url_encode(req.device_code);

    Reply r;
    if (!post_form(kTokenUrl, body, r, err)) {
        // A dropped connection mid-wait is not a failed pairing: the code is
        // still good, so the caller should keep polling.
        return PairStatus::Pending;
    }

    if (r.status == 200) {
        if (!json_str(r.body, "access_token", access_token)) {
            err = "no access_token in a 200 response";
            return PairStatus::Error;
        }
        return PairStatus::Ok;
    }

    std::string code;
    json_str(r.body, "error", code);
    if (code == "authorization_pending") {
        return PairStatus::Pending;
    }
    if (code == "slow_down") {
        return PairStatus::SlowDown;
    }

    describe(r, err);
    if (code == "access_denied") {
        return PairStatus::Denied;
    }
    if (code == "expired_token") {
        return PairStatus::Expired;
    }
    return PairStatus::Error;
}

std::string pair_credentials_json(const std::string &access_token) {
    return std::string("{\"authData\":\"") + base64(access_token) +
           "\",\"authType\":3,\"username\":\"\"}";
}

bool pair_credentials_resumable(const std::string &json) {
    cJSON *root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "authType");
    const cJSON *user = cJSON_GetObjectItem(root, "username");
    const cJSON *data = cJSON_GetObjectItem(root, "authData");

    const bool have_data = cJSON_IsString(data) && data->valuestring &&
                           data->valuestring[0] != '\0';
    const bool named = cJSON_IsString(user) && user->valuestring &&
                       user->valuestring[0] != '\0';
    // Only the stored-credential type needs the name; a token blob (type 3) is
    // one this app wrote moments ago and is about to use.
    const bool stored = cJSON_IsNumber(type) && type->valueint == 1;

    cJSON_Delete(root);
    return have_data && (named || !stored);
}
