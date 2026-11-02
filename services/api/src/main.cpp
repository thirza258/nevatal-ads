#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "httplib.h"
#include "automation.hpp"
#include "db.hpp"
#include "handlers.hpp"
#include "utils.hpp"

namespace utils = adcontrol::utils;

namespace {

std::unique_ptr<httplib::Server> g_server;
volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int signal) {
    g_stop = 1;
    if (g_server) {
        g_server->stop();
    }
    (void)signal;
}

std::string env_string(const char* key, const std::string& fallback = "") {
    const char* value = std::getenv(key);
    return (value && *value) ? std::string(value) : fallback;
}

int env_int(const char* key, int fallback, int min_value, int max_value) {
    const char* value = std::getenv(key);
    if (!value || !*value) return fallback;
    try {
        int parsed = std::stoi(value);
        if (parsed < min_value || parsed > max_value) {
            std::cerr << "[Config] " << key << " must be between " << min_value
                      << " and " << max_value << "; using " << fallback << std::endl;
            return fallback;
        }
        return parsed;
    } catch (...) {
        std::cerr << "[Config] Invalid integer for " << key << "; using " << fallback << std::endl;
        return fallback;
    }
}

bool env_bool(const char* key, bool fallback) {
    std::string value = utils::to_lower_copy(env_string(key));
    if (value.empty()) return fallback;
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    return fallback;
}

std::string backup_timestamp() {
    std::time_t now = utils::utc_now_epoch();
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return std::string(buf);
}

bool create_backup(adcontrol::Database& db, const std::filesystem::path& backup_dir,
                   int retention, const std::string& label) {
    std::error_code ec;
    std::filesystem::create_directories(backup_dir, ec);
    if (ec) {
        std::cerr << "[Backup] Cannot create backup directory " << backup_dir
                  << ": " << ec.message() << std::endl;
        return false;
    }
    std::error_code dir_ec;
    std::filesystem::permissions(
        backup_dir,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace,
        dir_ec);

    const std::string filename = "adcontrol-" + backup_timestamp() + ".db";
    const auto final_path = backup_dir / filename;
    const auto temp_path = backup_dir / (".tmp-" + filename);

    std::filesystem::remove(temp_path, ec);
    if (!db.backup_to(temp_path.string())) {
        std::filesystem::remove(temp_path, ec);
        std::cerr << "[Backup] Backup failed for " << label << std::endl;
        return false;
    }
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        std::cerr << "[Backup] Cannot finalize backup: " << ec.message() << std::endl;
        return false;
    }
    std::cout << "[Backup] Wrote verified backup " << final_path << std::endl;

    // Retain only the newest N verified backups.
    std::vector<std::filesystem::path> existing;
    for (const auto& entry : std::filesystem::directory_iterator(backup_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("adcontrol-", 0) == 0 && entry.path().extension() == ".db") {
            existing.push_back(entry.path());
        }
    }
    std::sort(existing.begin(), existing.end());
    while (static_cast<int>(existing.size()) > retention) {
        std::error_code remove_ec;
        std::filesystem::remove(existing.front(), remove_ec);
        if (!remove_ec) {
            std::cout << "[Backup] Pruned old backup " << existing.front() << std::endl;
        }
        existing.erase(existing.begin());
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // The database, WAL/SHM files, and online backups contain password hashes
    // and analytics. Keep everything the service creates private to its user.
    ::umask(0077);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const std::string environment = env_string("ADCONTROL_ENV", "development");
    const bool production = utils::to_lower_copy(environment) == "production";

    int port = env_int("PORT", 8088, 1, 65535);
    std::string host = env_string("HOST", "0.0.0.0");
    std::string db_path = env_string("DB_PATH", "adcontrol.db");
    std::string secret_key = env_string("SECRET_KEY");
    std::string sdk_path = env_string("SDK_PATH", "adcontrol.js");
    const bool trust_proxy = env_bool("TRUST_PROXY", false);

    int event_retention_days = env_int("EVENT_RETENTION_DAYS", 180, 1, 3650);
    int backup_interval_hours = env_int("BACKUP_INTERVAL_HOURS", 24, 0, 24 * 30);
    int backup_retention = env_int("BACKUP_RETENTION", 14, 1, 365);
    const std::string backup_dir_env = env_string("BACKUP_DIR");

    if (secret_key.empty()) {
        if (production) {
            std::cerr << "[Fatal] SECRET_KEY is required in production. "
                         "Generate one with: openssl rand -hex 32" << std::endl;
            return 1;
        }
        secret_key = "dev-only-insecure-secret-key-do-not-use-in-production";
        std::cerr << "[Config] WARNING: SECRET_KEY is not set; using a development-only key." << std::endl;
    } else if (production && secret_key.size() < 32) {
        std::cerr << "[Fatal] SECRET_KEY must be at least 32 characters in production." << std::endl;
        return 1;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   AdControl Centralized Ad Management Service   " << std::endl;
    std::cout << "=================================================" << std::endl;
    std::cout << "[Config] Environment: " << environment << std::endl;
    std::cout << "[Config] Port:        " << port << std::endl;
    std::cout << "[Config] Host:        " << host << std::endl;
    std::cout << "[Config] DB Path:     " << db_path << std::endl;
    std::cout << "[Config] SDK Path:    " << sdk_path << std::endl;
    std::cout << "[Config] Trust proxy: " << (trust_proxy ? "yes" : "no") << std::endl;
    std::cout << "[Config] Retention:   " << event_retention_days << " days" << std::endl;
    if (backup_interval_hours > 0) {
        std::cout << "[Config] Backups:     every " << backup_interval_hours
                  << "h, keep " << backup_retention << std::endl;
    } else {
        std::cout << "[Config] Backups:     disabled (BACKUP_INTERVAL_HOURS=0)" << std::endl;
    }

    if (!std::filesystem::exists(sdk_path)) {
        std::cerr << "[Config] WARNING: SDK asset not found at " << sdk_path
                  << "; /sdk/v1/adcontrol.js will return 404." << std::endl;
    }

    // Ensure the database's parent directory exists so a fresh volume works.
    {
        std::filesystem::path db_file(db_path);
        std::filesystem::path parent = db_file.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[Fatal] Cannot create database directory " << parent
                          << ": " << ec.message() << std::endl;
                return 1;
            }
        }
    }

    auto db = std::make_shared<adcontrol::Database>();
    if (!db->init(db_path)) {
        std::cerr << "[Fatal] Failed to initialize SQLite database at " << db_path << std::endl;
        return 1;
    }
    std::cout << "[DB] Database initialized successfully." << std::endl;

    std::filesystem::path backup_dir;
    if (!backup_dir_env.empty()) {
        backup_dir = backup_dir_env;
    } else {
        std::filesystem::path parent = std::filesystem::path(db_path).parent_path();
        if (parent.empty()) parent = ".";
        backup_dir = parent / "backups";
    }

    g_server = std::make_unique<httplib::Server>();
    g_server->set_payload_max_length(256 * 1024);
    g_server->set_read_timeout(15, 0);
    g_server->set_write_timeout(30, 0);
    g_server->set_keep_alive_max_count(100);
    g_server->set_tcp_nodelay(true);
    g_server->set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                       std::exception_ptr ep) {
        std::string what = "unknown error";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        std::cerr << "[Error] Unhandled exception on " << req.method << " " << req.path
                  << ": " << what << std::endl;
        res.status = 500;
        res.set_header("Cache-Control", "no-store");
        res.set_content("{\"error\":\"Internal server error\"}", "application/json");
    });

    adcontrol::ApiServer api_server(db, secret_key, trust_proxy);
    api_server.register_routes(*g_server, sdk_path);

    std::cout << "[Server] Listening on http://" << host << ":" << port << std::endl;
    const std::string admin_email = env_string("ADMIN_EMAIL", "admin@adcontrol.local");
    if (!production && env_string("ADMIN_PASSWORD").empty()) {
        std::cout << "[Server] Development admin login: " << admin_email
                  << " / admin12345 (change it in Settings)" << std::endl;
    } else {
        std::cout << "[Server] Admin credentials are configured via environment variables." << std::endl;
    }

    std::thread maintenance([db, backup_dir, backup_interval_hours, backup_retention,
                             event_retention_days]() {
        using clock = std::chrono::steady_clock;
        auto last_maintenance = clock::now() - std::chrono::hours(1); // run immediately
        auto last_backup = clock::now() - std::chrono::hours(24 * 30); // back up on startup
        auto last_automation = clock::now() - std::chrono::minutes(1); // run immediately

        while (!g_stop) {
            if (clock::now() - last_automation >= std::chrono::seconds(30)) {
                int automation_processed = adcontrol::run_automation_tick(*db);
                if (automation_processed > 0) {
                    std::cout << "[Automation] Processed " << automation_processed
                              << " workflow run(s)." << std::endl;
                }
                last_automation = clock::now();
            }

            if (clock::now() - last_maintenance >= std::chrono::hours(1)) {
                if (event_retention_days > 0) {
                    int purged = db->purge_events_older_than(event_retention_days);
                    if (purged > 0) {
                        std::cout << "[Retention] Purged " << purged
                                  << " events older than " << event_retention_days << " days." << std::endl;
                    }
                }
                int advanced = db->advance_campaign_states(adcontrol::utils::get_current_iso8601());
                if (advanced > 0) {
                    std::cout << "[Scheduler] Advanced " << advanced << " campaign state(s)." << std::endl;
                }
                int optimized = db->optimize_experiments(adcontrol::utils::get_current_iso8601());
                if (optimized > 0) {
                    std::cout << "[Optimizer] Updated " << optimized << " experiment(s)." << std::endl;
                }
                last_maintenance = clock::now();
            }

            if (backup_interval_hours > 0 &&
                clock::now() - last_backup >= std::chrono::hours(backup_interval_hours)) {
                create_backup(*db, backup_dir, backup_retention, "scheduled");
                last_backup = clock::now();
            }

            for (int i = 0; i < 60 && !g_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });

    bool listened = g_server->listen(host, port);
    g_stop = 1;
    if (maintenance.joinable()) maintenance.join();

    if (!listened) {
        std::cerr << "[Fatal] Failed to bind to " << host << ":" << port << std::endl;
        return 1;
    }

    std::cout << "[Server] Stopped cleanly." << std::endl;
    return 0;
}
