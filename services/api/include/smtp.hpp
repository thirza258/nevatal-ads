#pragma once

#include <string>

namespace adcontrol {
namespace smtp {

// Decrypted SMTP transport settings. This struct exists only in memory; the
// database stores the password as ciphertext and the API never returns it.
struct Settings {
    bool enabled = false;
    std::string host;
    int port = 587;
    std::string encryption; // "starttls", "ssl", or "none"
    std::string username;
    std::string password;
    std::string from_email;
    std::string from_name;
    std::string reply_to;
};

struct Result {
    bool ok = false;
    std::string error;
};

// Empty string when the settings are usable; otherwise a human-readable reason.
std::string validate_settings(const Settings& settings);
bool is_valid_host(const std::string& host);
bool is_valid_email(const std::string& email);

// Builds an RFC 5322 HTML message (base64 body). Exposed for unit tests.
std::string build_message(const Settings& settings, const std::string& to,
                          const std::string& subject, const std::string& html_body);

// Connects, negotiates TLS (implicit or STARTTLS, certificate verified),
// authenticates when a username is configured, and submits one message.
Result send(const Settings& settings, const std::string& to,
            const std::string& subject, const std::string& html_body);

} // namespace smtp
} // namespace adcontrol
