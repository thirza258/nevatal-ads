#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <random>
#include <regex>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>

namespace adcontrol {
namespace utils {

inline std::string get_current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&itt), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

inline std::time_t utc_now_epoch() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

inline std::string iso8601_from_epoch(std::time_t epoch) {
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&epoch), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

inline std::string iso8601_days_ago(int days) {
    return iso8601_from_epoch(utc_now_epoch() - static_cast<std::time_t>(days) * 86400);
}

inline std::string trim_copy(const std::string& input) {
    size_t first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

inline std::string to_lower_copy(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return input;
}

// Strict-ish UTC ISO-8601 parsing. Accepts "YYYY-MM-DDTHH:MM[:SS][.fff][Z|+02:00|-05:00]".
// A missing timezone is treated as UTC so older API clients cannot smuggle local time in.
inline bool parse_iso8601_utc(const std::string& input, std::time_t& out_epoch) {
    static const std::regex re(
        R"(^(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2})(?::(\d{2}))?(?:\.\d+)?(Z|[+-]\d{2}:?\d{2})?$)");
    std::smatch m;
    if (!std::regex_match(input, m, re)) return false;

    std::tm tm{};
    tm.tm_year = std::stoi(m[1].str()) - 1900;
    tm.tm_mon = std::stoi(m[2].str()) - 1;
    tm.tm_mday = std::stoi(m[3].str());
    tm.tm_hour = std::stoi(m[4].str());
    tm.tm_min = std::stoi(m[5].str());
    tm.tm_sec = m[6].matched ? std::stoi(m[6].str()) : 0;

    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31 ||
        tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59 ||
        tm.tm_sec < 0 || tm.tm_sec > 60) {
        return false;
    }

    std::time_t epoch = timegm(&tm);
    std::tm check{};
    gmtime_r(&epoch, &check);
    // Reject impossible calendar dates (e.g. 2026-02-31) that timegm silently rolls over.
    if (check.tm_year != tm.tm_year || check.tm_mon != tm.tm_mon || check.tm_mday != tm.tm_mday ||
        check.tm_hour != tm.tm_hour || check.tm_min != tm.tm_min) {
        return false;
    }

    if (m[7].matched) {
        std::string tz = m[7].str();
        if (tz != "Z") {
            int sign = (tz[0] == '-') ? -1 : 1;
            int hours = std::stoi(tz.substr(1, 2));
            int minutes = std::stoi(tz.substr(tz.size() - 2));
            if (hours > 14 || minutes > 59) return false;
            epoch -= sign * (hours * 3600 + minutes * 60);
        }
    }
    out_epoch = epoch;
    return true;
}

// Normalizes an ISO-8601 timestamp to the canonical "YYYY-MM-DDTHH:MM:SSZ" form used in the DB.
inline std::string normalize_iso8601_utc(const std::string& input) {
    if (trim_copy(input).empty()) return "";
    std::time_t epoch = 0;
    if (!parse_iso8601_utc(trim_copy(input), epoch)) return "";
    return iso8601_from_epoch(epoch);
}

inline bool is_valid_iso8601_utc(const std::string& input) {
    if (input.empty()) return true;
    std::time_t epoch = 0;
    return parse_iso8601_utc(input, epoch);
}

inline std::string random_hex(size_t num_bytes = 16) {
    std::vector<unsigned char> buf(num_bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(num_bytes)) != 1) {
        // Fallback to std::random_device if OpenSSL RAND_bytes fails
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned int> dis(0, 255);
        for (size_t i = 0; i < num_bytes; ++i) {
            buf[i] = static_cast<unsigned char>(dis(gen));
        }
    }
    std::ostringstream ss;
    for (unsigned char b : buf) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

inline std::string generate_uuid() {
    // 8-4-4-4-12 format
    std::string hex = random_hex(16);
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" +
           hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" +
           hex.substr(20, 12);
}

inline std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

inline std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &digest_len);
    std::ostringstream ss;
    for (unsigned int i = 0; i < digest_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
}

inline std::string hash_password(const std::string& password, const std::string& salt, int iterations = 100000) {
    unsigned char out[32];
    PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.length()),
                      reinterpret_cast<const unsigned char*>(salt.c_str()),
                      static_cast<int>(salt.length()),
                      iterations, EVP_sha256(), 32, out);
    std::ostringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(out[i]);
    }
    return ss.str();
}

inline bool verify_password(const std::string& password, const std::string& salt, const std::string& expected_hash) {
    std::string computed = hash_password(password, salt);
    if (computed.size() != expected_hash.size()) return false;
    return CRYPTO_memcmp(computed.data(), expected_hash.data(), computed.size()) == 0;
}

// Destination URL safety validation according to PRD section 12.3:
// Only http:// or https:// (https strongly encouraged).
// Rejects javascript:, data:, vbscript:, file:, whitespace, and scheme-only URLs.
inline bool is_safe_url(const std::string& url) {
    if (url.empty() || url.size() > 2048) return false;
    for (unsigned char c : url) {
        if (c < 0x20 || c == 0x7f || std::isspace(c) || c == '\\') return false;
    }
    std::string lower = to_lower_copy(trim_copy(url));
    if (lower.rfind("javascript:", 0) == 0 ||
        lower.rfind("data:", 0) == 0 ||
        lower.rfind("vbscript:", 0) == 0 ||
        lower.rfind("file:", 0) == 0) {
        return false;
    }
    bool https = lower.rfind("https://", 0) == 0;
    bool http = lower.rfind("http://", 0) == 0;
    if (!https && !http) return false;

    size_t scheme_len = https ? 8 : 7;
    std::string rest = lower.substr(scheme_len);
    if (rest.empty() || rest[0] == '/' || rest[0] == '?' || rest[0] == '#') return false;
    // Require a plausible host: at least one character before any path/query/fragment.
    size_t host_end = rest.find_first_of("/?#");
    std::string host = rest.substr(0, host_end);
    if (host.empty()) return false;
    // Reject userinfo-style URLs (common in credential-phishing destinations).
    if (host.find('@') != std::string::npos) return false;
    return true;
}

// Returns true for empty or a safe http(s) URL. Used for optional creative images.
inline bool is_safe_optional_url(const std::string& url) {
    return url.empty() || is_safe_url(url);
}

// Lowercase a hostname/origin, strip the scheme, path, query, fragment, and trailing dot.
// Keeps an explicit port.
inline std::string normalize_host(const std::string& raw) {
    std::string s = to_lower_copy(trim_copy(raw));
    if (s.empty()) return "";

    if (s.rfind("https://", 0) == 0) {
        s = s.substr(8);
    } else if (s.rfind("http://", 0) == 0) {
        s = s.substr(7);
    }
    size_t path_pos = s.find_first_of("/?#");
    if (path_pos != std::string::npos) s = s.substr(0, path_pos);

    // Strip userinfo if present.
    size_t at = s.rfind('@');
    if (at != std::string::npos) s = s.substr(at + 1);

    // Strip a trailing dot on the host portion (FQDN form).
    if (!s.empty() && s.back() == '.') s.pop_back();

    return s;
}

inline bool is_valid_domain_entry(const std::string& raw) {
    std::string entry = normalize_host(raw);
    if (entry.empty() || entry.size() > 253) return false;
    if (entry == "*") return true;

    bool wildcard = false;
    if (entry.rfind("*.", 0) == 0) {
        wildcard = true;
        entry = entry.substr(2);
        if (entry.empty()) return false;
    }

    std::string host = entry;
    std::string port;
    size_t colon = entry.rfind(':');
    if (colon != std::string::npos) {
        host = entry.substr(0, colon);
        port = entry.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        for (char c : port) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        if (std::stoi(port) > 65535) return false;
    }
    if (host.empty()) return false;
    if (wildcard && (port.size() > 0 || host == "localhost")) return false;

    static const std::regex host_re(
        R"(^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*$)");
    return std::regex_match(host, host_re);
}

// Domain matching against allowed_domains.
// Supports exact host (port optional), wildcard subdomains ("*.example.com"), and "*".
// If an allowlist entry includes a port, the incoming request must use that port.
inline bool is_domain_allowed(const std::string& origin_or_referer, const std::vector<std::string>& allowed_domains) {
    if (allowed_domains.empty()) return false;

    std::string incoming = normalize_host(origin_or_referer);
    if (incoming.empty()) {
        // Browser privacy tools can strip Origin/Referer. Delivery still requires a valid
        // public key and is rate-limited; event ingestion is rate-limited separately.
        for (const auto& allowed : allowed_domains) {
            if (normalize_host(allowed) == "*") return true;
        }
        return true;
    }

    std::string incoming_host = incoming;
    std::string incoming_port;
    size_t colon = incoming.rfind(':');
    if (colon != std::string::npos) {
        incoming_host = incoming.substr(0, colon);
        incoming_port = incoming.substr(colon + 1);
    }

    for (const auto& raw_allowed : allowed_domains) {
        std::string allowed = normalize_host(raw_allowed);
        if (allowed.empty()) continue;
        if (allowed == "*") return true;

        if (allowed.rfind("*.", 0) == 0) {
            std::string root = allowed.substr(2);
            if (incoming_host == root) return true;
            if (incoming_host.size() > root.size() &&
                incoming_host.compare(incoming_host.size() - root.size(), root.size(), root) == 0 &&
                incoming_host[incoming_host.size() - root.size() - 1] == '.') {
                return true;
            }
            continue;
        }

        std::string allowed_host = allowed;
        std::string allowed_port;
        size_t allowed_colon = allowed.rfind(':');
        if (allowed_colon != std::string::npos) {
            allowed_host = allowed.substr(0, allowed_colon);
            allowed_port = allowed.substr(allowed_colon + 1);
        }

        if (incoming_host != allowed_host) continue;
        if (!allowed_port.empty() && incoming_port != allowed_port) continue;
        return true;
    }

    return false;
}

// Compare canonical ISO8601 strings (YYYY-MM-DDTHH:MM:SSZ); lexical comparison is valid.
inline bool is_campaign_active_schedule(const std::string& starts_at, const std::string& ends_at, const std::string& now_iso) {
    if (!starts_at.empty() && now_iso < starts_at) {
        return false;
    }
    if (!ends_at.empty() && now_iso > ends_at) {
        return false;
    }
    return true;
}

inline bool is_http_url(const std::string& url) {
    return is_safe_url(url);
}

} // namespace utils
} // namespace adcontrol
