#pragma once

#include "httplib.h"
#include "db.hpp"
#include <memory>
#include <string>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace adcontrol {

class ApiServer {
public:
    ApiServer(std::shared_ptr<Database> db,
              const std::string& secret_key = "adcontrol-secret-key-change-in-prod",
              bool trust_proxy = false);
    ~ApiServer() = default;

    void register_routes(httplib::Server& svr, const std::string& sdk_path = "");

private:
    std::shared_ptr<Database> db_;
    std::string secret_key_;
    bool trust_proxy_;
    // Random per-process value mixed into delivery ETags. A restart (including
    // one that rotates SECRET_KEY) invalidates stale click links held by
    // conditional caches without exposing any key-derived material.
    const std::string etag_nonce_;

    // Auth verification helpers. authenticate_any is used by shared account and
    // learning routes; authenticate() is restricted to back-office roles.
    std::optional<User> authenticate_any(const httplib::Request& req, httplib::Response& res);
    std::optional<User> authenticate(const httplib::Request& req, httplib::Response& res);
    std::optional<User> authenticate_learning(const httplib::Request& req, httplib::Response& res);
    std::optional<User> authenticate_learning_staff(const httplib::Request& req, httplib::Response& res);
    std::optional<User> authenticate_learning_editor(const httplib::Request& req, httplib::Response& res);
    std::optional<User> authenticate_admin(const httplib::Request& req, httplib::Response& res);
    // Admin or publisher: publisher writes are still restricted to their tenant
    // and to house/affiliate sources by the individual handlers.
    std::optional<User> authenticate_writer(const httplib::Request& req, httplib::Response& res);

    // Click token signing / verification (short-lived, campaign-bound).
    // verify_click_token reports expiry separately so the caller can return 410.
    std::string create_click_token(const std::string& campaign_id);
    std::optional<std::string> verify_click_token(const std::string& token, bool& expired);

    // Client address for rate limiting and privacy-minimized hashing.
    std::string client_ip(const httplib::Request& req) const;

    // Sliding-window limiter shared by public endpoints.
    bool allow_request(const std::string& key, int limit, int window_seconds);
    std::mutex rate_mutex_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> rate_buckets_;
    std::chrono::steady_clock::time_point last_rate_prune_ = std::chrono::steady_clock::now();
};

} // namespace adcontrol
