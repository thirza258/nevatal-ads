#include "smtp.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "utils.hpp"

namespace adcontrol {
namespace smtp {

namespace {

constexpr int kConnectTimeoutMs = 8000;
constexpr int kSocketTimeoutSeconds = 8;
constexpr std::chrono::seconds kTotalTimeout{20};
constexpr size_t kMaxReplyBytes = 64 * 1024;

std::string base64_encode(const std::string& in) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= in.size()) {
        unsigned int value = (static_cast<unsigned char>(in[i]) << 16) |
                             (static_cast<unsigned char>(in[i + 1]) << 8) |
                             static_cast<unsigned char>(in[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back(alphabet[value & 0x3F]);
        i += 3;
    }
    const size_t remaining = in.size() - i;
    if (remaining == 1) {
        unsigned int value = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out += "==";
    } else if (remaining == 2) {
        unsigned int value = (static_cast<unsigned char>(in[i]) << 16) |
                             (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string base64_wrapped(const std::string& in, size_t width = 76) {
    const std::string encoded = base64_encode(in);
    std::string out;
    for (size_t i = 0; i < encoded.size(); i += width) {
        out += encoded.substr(i, width);
        out += "\r\n";
    }
    return out;
}

bool is_ascii_printable(const std::string& value) {
    for (unsigned char c : value) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

std::string rfc2047_encode(const std::string& value) {
    // RFC 2047 limits each encoded word to 75 characters. 45 raw bytes expand
    // to 60 base64 characters, which keeps the whole word under that limit.
    constexpr size_t kChunk = 45;
    std::string out;
    for (size_t i = 0; i < value.size(); i += kChunk) {
        out += "=?UTF-8?B?" + base64_encode(value.substr(i, kChunk)) + "?=";
        if (i + kChunk < value.size()) out += " ";
    }
    return out;
}

std::string encode_subject(const std::string& subject) {
    if (is_ascii_printable(subject)) return subject;
    return rfc2047_encode(subject);
}

std::string encode_display_name(const std::string& name) {
    if (!is_ascii_printable(name)) return rfc2047_encode(name);

    bool needs_quotes = name.empty() || name.front() == ' ' || name.back() == ' ' ||
                        name.find_first_of("()<>[]:;@\\,.\"") != std::string::npos;
    if (!needs_quotes) return name;

    std::string escaped;
    escaped.reserve(name.size() + 2);
    for (char c : name) {
        if (c == '\\' || c == '"') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return "\"" + escaped + "\"";
}

std::string format_address(const std::string& name, const std::string& email) {
    if (name.empty()) return email;
    return encode_display_name(name) + " <" + email + ">";
}

std::string rfc5322_date() {
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::time_t now = utils::utc_now_epoch();
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::ostringstream ss;
    ss << days[tm.tm_wday] << ", " << std::setw(2) << std::setfill('0') << tm.tm_mday
       << " " << months[tm.tm_mon] << " " << (tm.tm_year + 1900) << " "
       << std::setw(2) << std::setfill('0') << tm.tm_hour << ":"
       << std::setw(2) << std::setfill('0') << tm.tm_min << ":"
       << std::setw(2) << std::setfill('0') << tm.tm_sec << " +0000";
    return ss.str();
}

std::string domain_of(const std::string& email) {
    size_t at = email.rfind('@');
    if (at == std::string::npos || at + 1 >= email.size()) return "localhost";
    return email.substr(at + 1);
}

std::string last_ssl_error() {
    unsigned long code = ERR_get_error();
    if (code == 0) return "TLS error";
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    ERR_clear_error();
    return std::string(buffer);
}

std::string single_line(const std::string& text, size_t limit = 200) {
    std::string out;
    for (char c : text) {
        if (c == '\r' || c == '\n') out.push_back(' ');
        else out.push_back(c);
        if (out.size() >= limit) break;
    }
    return out;
}

bool set_socket_timeouts(int fd) {
    timeval tv{};
    tv.tv_sec = kSocketTimeoutSeconds;
    tv.tv_usec = 0;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool open(const std::string& host, int port, bool implicit_tls, std::string& error);
    bool start_tls(const std::string& host, std::string& error);
    bool write_all(const std::string& data, std::string& error);
    bool read_reply(int& code, std::string& text, std::string& error);
    void close();

private:
    bool read_line(std::string& line, std::string& error);
    bool read_some(char* buffer, size_t size, int& received, std::string& error);
    bool timed_out() const { return std::chrono::steady_clock::now() > deadline_; }

    int fd_ = -1;
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    std::string buffer_;
    std::chrono::steady_clock::time_point deadline_{};
};

void Connection::close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    buffer_.clear();
}

bool Connection::open(const std::string& host, int port, bool implicit_tls, std::string& error) {
    close();
    deadline_ = std::chrono::steady_clock::now() + kTotalTimeout;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    int resolved = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
    if (resolved != 0 || !addresses) {
        error = "Could not resolve SMTP host";
        return false;
    }

    int fd = -1;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        bool connected = false;
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            pollfd descriptor{fd, POLLOUT, 0};
            if (::poll(&descriptor, 1, kConnectTimeoutMs) > 0) {
                int socket_error = 0;
                socklen_t length = sizeof(socket_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) == 0 &&
                    socket_error == 0) {
                    connected = true;
                }
            }
        }

        if (connected) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);

    if (fd < 0) {
        error = "Could not connect to the SMTP server";
        return false;
    }

    fd_ = fd;
    if (!set_socket_timeouts(fd_)) {
        error = "Could not configure the SMTP connection";
        close();
        return false;
    }

    // Always verify the server certificate for implicit TLS and STARTTLS.
    ERR_clear_error();
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        error = "Could not initialize TLS";
        close();
        return false;
    }
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(ssl_ctx_) != 1) {
        error = "Could not load trusted CA certificates";
        close();
        return false;
    }

    if (implicit_tls) {
        if (!start_tls(host, error)) {
            close();
            return false;
        }
    }
    return true;
}

bool Connection::start_tls(const std::string& host, std::string& error) {
    if (!ssl_ctx_ || fd_ < 0) {
        error = "TLS is not available on this connection";
        return false;
    }

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
        error = "Could not initialize TLS";
        return false;
    }
    SSL_set_fd(ssl_, fd_);

    unsigned char address_bytes[16];
    const bool is_ip = inet_pton(AF_INET, host.c_str(), address_bytes) == 1 ||
                       inet_pton(AF_INET6, host.c_str(), address_bytes) == 1;
    if (is_ip) {
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (X509_VERIFY_PARAM_set1_ip_asc(param, host.c_str()) != 1) {
            error = "Could not configure TLS host verification";
            return false;
        }
    } else {
        SSL_set_tlsext_host_name(ssl_, host.c_str());
        SSL_set1_host(ssl_, host.c_str());
    }

    ERR_clear_error();
    if (SSL_connect(ssl_) != 1) {
        const long verify_result = SSL_get_verify_result(ssl_);
        if (verify_result != X509_V_OK) {
            error = "TLS certificate verification failed";
        } else {
            error = "TLS handshake failed: " + last_ssl_error();
        }
        return false;
    }
    return true;
}

bool Connection::read_some(char* buffer, size_t size, int& received, std::string& error) {
    if (timed_out()) {
        error = "SMTP server timed out";
        return false;
    }
    if (ssl_) {
        ERR_clear_error();
        int result = SSL_read(ssl_, buffer, static_cast<int>(size));
        if (result > 0) {
            received = result;
            return true;
        }
        const int ssl_error = SSL_get_error(ssl_, result);
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            error = "SMTP server closed the connection";
        } else if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE ||
                   (ssl_error == SSL_ERROR_SYSCALL && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            error = "SMTP server timed out";
        } else {
            error = "Secure SMTP read failed";
        }
        return false;
    }

    ssize_t result = ::recv(fd_, buffer, size, 0);
    if (result > 0) {
        received = static_cast<int>(result);
        return true;
    }
    if (result == 0) {
        error = "SMTP server closed the connection";
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        error = "SMTP server timed out";
    } else {
        error = "SMTP read failed";
    }
    return false;
}

bool Connection::read_line(std::string& line, std::string& error) {
    for (;;) {
        const size_t end = buffer_.find("\r\n");
        if (end != std::string::npos) {
            line = buffer_.substr(0, end);
            buffer_.erase(0, end + 2);
            return true;
        }
        if (buffer_.size() > kMaxReplyBytes) {
            error = "SMTP reply was too large";
            return false;
        }

        char chunk[2048];
        int received = 0;
        if (!read_some(chunk, sizeof(chunk), received, error)) return false;
        buffer_.append(chunk, static_cast<size_t>(received));
    }
}

bool Connection::write_all(const std::string& data, std::string& error) {
    size_t written = 0;
    while (written < data.size()) {
        if (timed_out()) {
            error = "SMTP server timed out";
            return false;
        }

        int result = 0;
        if (ssl_) {
            ERR_clear_error();
            result = SSL_write(ssl_, data.data() + written, static_cast<int>(data.size() - written));
            if (result <= 0) {
                const int ssl_error = SSL_get_error(ssl_, result);
                if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                    error = "SMTP server timed out";
                } else {
                    error = "Secure SMTP write failed";
                }
                return false;
            }
        } else {
            ssize_t sent = ::send(fd_, data.data() + written,
                                  data.size() - written, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    error = "SMTP server timed out";
                } else {
                    error = "SMTP write failed";
                }
                return false;
            }
            result = static_cast<int>(sent);
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

bool Connection::read_reply(int& code, std::string& text, std::string& error) {
    code = 0;
    text.clear();
    for (;;) {
        std::string line;
        if (!read_line(line, error)) return false;
        if (line.size() < 3) {
            error = "SMTP server sent a malformed reply";
            return false;
        }
        for (size_t i = 0; i < 3; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
                error = "SMTP server sent a malformed reply";
                return false;
            }
        }
        const int line_code = std::stoi(line.substr(0, 3));
        if (code == 0) code = line_code;
        text += line;
        text.push_back('\n');

        if (line.size() == 3 || line[3] != '-') break;
    }
    return true;
}

std::string command_verb(const std::string& line) {
    const size_t space = line.find(' ');
    std::string verb = space == std::string::npos ? line : line.substr(0, space);
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return verb;
}

class Session {
public:
    explicit Session(Connection& connection) : connection_(connection) {}

    bool command(const std::string& line, int expected, std::string& error, int alternative = -1,
                 const std::string& context = "") {
        if (!connection_.write_all(line + "\r\n", error)) return false;
        return reply(expected, error, context.empty() ? command_verb(line) : context, alternative);
    }

    bool reply(int expected, std::string& error, const std::string& context, int alternative = -1) {
        int code = 0;
        std::string text;
        if (!connection_.read_reply(code, text, error)) return false;
        last_code_ = code;
        last_reply_ = text;
        if (code != expected && (alternative < 0 || code != alternative)) {
            error = "SMTP server rejected " + context + " (" + std::to_string(code) + ")";
            const std::string detail = single_line(text);
            if (!detail.empty()) error += ": " + detail;
            return false;
        }
        return true;
    }

    int last_code() const { return last_code_; }
    const std::string& last_reply() const { return last_reply_; }

private:
    Connection& connection_;
    int last_code_ = 0;
    std::string last_reply_;
};

bool matches_auth_mechanism(const std::string& reply, const std::string& mechanism) {
    std::string upper = reply;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper.find("AUTH") != std::string::npos && upper.find(mechanism) != std::string::npos;
}

bool authenticate(Session& session, const Settings& settings, std::string& error) {
    if (settings.username.empty()) return true;

    const std::string plain_payload =
        base64_encode(std::string(1, '\0') + settings.username + std::string(1, '\0') + settings.password);

    // Prefer AUTH PLAIN with an initial response. If the server advertises only
    // LOGIN, or rejects PLAIN, fall back to the LOGIN challenge/response flow.
    bool plain_supported = matches_auth_mechanism(session.last_reply(), "PLAIN");
    bool login_supported = matches_auth_mechanism(session.last_reply(), "LOGIN");

    if (plain_supported || !login_supported) {
        std::string plain_error;
        if (session.command("AUTH PLAIN " + plain_payload, 235, plain_error, 334)) {
            if (session.last_code() == 334) {
                if (!session.command(plain_payload, 235, error)) return false;
            }
            return true;
        }
        if (!login_supported) {
            error = plain_error;
            return false;
        }
    }

    if (!session.command("AUTH LOGIN", 334, error)) return false;
    // The base64 challenges contain credentials; label failures generically so
    // they are never echoed into API errors or the audit log.
    if (!session.command(base64_encode(settings.username), 334, error, -1, "authentication")) {
        return false;
    }
    if (!session.command(base64_encode(settings.password), 235, error, -1, "authentication")) {
        return false;
    }
    return true;
}

std::string dot_stuff(const std::string& message) {
    std::string out;
    out.reserve(message.size() + 16);
    size_t start = 0;
    while (start <= message.size()) {
        size_t end = message.find("\r\n", start);
        const bool last = end == std::string::npos;
        const std::string line = last ? message.substr(start) : message.substr(start, end - start);
        if (!line.empty() && line.front() == '.') out.push_back('.');
        out += line;
        if (!last) out += "\r\n";
        if (last) break;
        start = end + 2;
    }
    return out;
}

} // namespace

bool is_valid_host(const std::string& host) {
    if (host.empty() || host.size() > 253) return false;
    for (unsigned char c : host) {
        if (c <= 0x20 || c >= 0x7F) return false;
    }
    if (host.front() == '.' || host.back() == '.' || host.find("..") != std::string::npos) {
        return false;
    }

    size_t start = 0;
    while (start <= host.size()) {
        size_t end = host.find('.', start);
        const std::string label = end == std::string::npos
                                      ? host.substr(start)
                                      : host.substr(start, end - start);
        if (label.empty() || label.size() > 63) return false;
        if (!std::isalnum(static_cast<unsigned char>(label.front())) ||
            !std::isalnum(static_cast<unsigned char>(label.back()))) {
            return false;
        }
        for (char c : label) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (!std::isalnum(uc) && c != '-') return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool is_valid_email(const std::string& email) {
    if (email.empty() || email.size() > 320) return false;
    if (email.find_first_of("\r\n\t ") != std::string::npos) return false;

    const size_t at = email.rfind('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size()) return false;
    const std::string local = email.substr(0, at);
    const std::string domain = email.substr(at + 1);
    if (local.size() > 64) return false;
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string::npos) {
        return false;
    }

    static const std::string allowed =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!#$%&'*+/=?^_`{|}~-";
    for (char c : local) {
        if (allowed.find(c) == std::string::npos) return false;
    }

    return is_valid_host(domain);
}

std::string validate_settings(const Settings& settings) {
    if (!is_valid_host(settings.host)) return "A valid SMTP host is required";
    if (settings.port < 1 || settings.port > 65535) {
        return "SMTP port must be between 1 and 65535";
    }
    if (settings.encryption != "starttls" && settings.encryption != "ssl" &&
        settings.encryption != "none") {
        return "SMTP encryption must be starttls, ssl, or none";
    }
    if (!is_valid_email(settings.from_email)) {
        return "A valid sender email address is required";
    }
    if (settings.from_name.size() > 128) return "Sender name is too long";
    if (settings.from_name.find_first_of("\r\n") != std::string::npos) {
        return "Sender name contains invalid characters";
    }
    if (!settings.reply_to.empty() && !is_valid_email(settings.reply_to)) {
        return "Reply-To must be a valid email address";
    }
    if (!settings.username.empty() &&
        settings.username.find_first_of("\r\n") != std::string::npos) {
        return "SMTP username contains invalid characters";
    }
    if (settings.username.size() > 320) return "SMTP username is too long";
    if (settings.password.size() > 512) return "SMTP password is too long";
    return "";
}

std::string build_message(const Settings& settings, const std::string& to,
                          const std::string& subject, const std::string& html_body) {
    std::ostringstream message;
    message << "Date: " << rfc5322_date() << "\r\n";
    message << "From: " << format_address(settings.from_name, settings.from_email) << "\r\n";
    message << "To: " << to << "\r\n";
    if (!settings.reply_to.empty()) {
        message << "Reply-To: " << settings.reply_to << "\r\n";
    }
    message << "Subject: " << encode_subject(subject) << "\r\n";
    message << "Message-ID: <" << utils::generate_uuid() << "@" << domain_of(settings.from_email)
            << ">\r\n";
    message << "MIME-Version: 1.0\r\n";
    message << "Content-Type: text/html; charset=UTF-8\r\n";
    message << "Content-Transfer-Encoding: base64\r\n";
    message << "\r\n";
    message << base64_wrapped(html_body);
    return message.str();
}

Result send(const Settings& settings, const std::string& to,
            const std::string& subject, const std::string& html_body) {
    Result result;

    const std::string validation_error = validate_settings(settings);
    if (!validation_error.empty()) {
        result.error = validation_error;
        return result;
    }
    if (!is_valid_email(to)) {
        result.error = "Invalid recipient email address";
        return result;
    }
    if (settings.username.empty() && !settings.password.empty()) {
        result.error = "An SMTP username is required when a password is set";
        return result;
    }

    Connection connection;
    if (!connection.open(settings.host, settings.port, settings.encryption == "ssl", result.error)) {
        return result;
    }

    Session session(connection);
    if (!session.reply(220, result.error, "connection greeting")) return result;

    const std::string ehlo_domain = domain_of(settings.from_email);
    if (!session.command("EHLO " + ehlo_domain, 250, result.error)) return result;

    if (settings.encryption == "starttls") {
        if (!session.command("STARTTLS", 220, result.error)) {
            result.error = "SMTP server refused STARTTLS. " + result.error;
            return result;
        }
        if (!connection.start_tls(settings.host, result.error)) return result;
        if (!session.command("EHLO " + ehlo_domain, 250, result.error)) return result;
    }

    if (!authenticate(session, settings, result.error)) return result;

    if (!session.command("MAIL FROM:<" + settings.from_email + ">", 250, result.error)) {
        return result;
    }
    if (!session.command("RCPT TO:<" + to + ">", 250, result.error, 251)) return result;
    if (!session.command("DATA", 354, result.error)) return result;

    const std::string message = dot_stuff(build_message(settings, to, subject, html_body));
    const std::string terminator =
        message.size() >= 2 && message.compare(message.size() - 2, 2, "\r\n") == 0 ? ".\r\n"
                                                                                   : "\r\n.\r\n";
    if (!connection.write_all(message + terminator, result.error)) return result;
    if (!session.reply(250, result.error, "message body")) return result;

    std::string ignored;
    connection.write_all("QUIT\r\n", ignored);

    result.ok = true;
    return result;
}

} // namespace smtp
} // namespace adcontrol
