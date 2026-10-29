#include "db.hpp"
#include "utils.hpp"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <thread>
#include <algorithm>

namespace adcontrol {

Database::Database() : db_(nullptr) {}

Database::~Database() {
    close();
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::init(const std::string& db_path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        // Tighten permissions for databases created by older releases too.
        std::error_code perm_ec;
        std::filesystem::permissions(
            db_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            perm_ec);

        // Enable WAL mode for high concurrency
        char* err_msg = nullptr;
        sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err_msg);
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err_msg);
        sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err_msg);
        sqlite3_exec(db_, "PRAGMA busy_timeout = 5000;", nullptr, nullptr, &err_msg);

        if (!run_migrations()) {
            std::cerr << "Failed to run migrations" << std::endl;
            return false;
        }
    }

    // Seeding uses public methods that acquire the mutex themselves; do it outside the lock.
    if (!seed_default_admin()) {
        return false;
    }
    if (!seed_learning_catalog()) {
        return false;
    }
    return true;
}

bool Database::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::backup_to(const std::string& destination_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || destination_path.empty()) return false;

    sqlite3* dest = nullptr;
    if (sqlite3_open(destination_path.c_str(), &dest) != SQLITE_OK) {
        std::cerr << "[Backup] Cannot open destination database: "
                  << (dest ? sqlite3_errmsg(dest) : "unknown error") << std::endl;
        if (dest) sqlite3_close(dest);
        return false;
    }

    bool ok = false;
    sqlite3_backup* backup = sqlite3_backup_init(dest, "main", db_, "main");
    if (!backup) {
        std::cerr << "[Backup] Failed to initialize backup: " << sqlite3_errmsg(dest) << std::endl;
    } else {
        int rc = SQLITE_OK;
        // Retry briefly if a writer holds the database; SQLite's backup API produces a
        // consistent snapshot even while the server continues to serve requests.
        for (int attempt = 0; attempt < 50; ++attempt) {
            rc = sqlite3_backup_step(backup, -1);
            if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        sqlite3_backup_finish(backup);

        if (rc == SQLITE_DONE) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(dest, "PRAGMA integrity_check;", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    ok = text && std::string(text) == "ok";
                }
                sqlite3_finalize(stmt);
            }
        } else {
            std::cerr << "[Backup] Backup step failed (code " << rc << "): "
                      << sqlite3_errmsg(dest) << std::endl;
        }
    }

    if (sqlite3_close(dest) != SQLITE_OK) {
        std::cerr << "[Backup] Failed to close destination database" << std::endl;
        ok = false;
    }

    if (ok) {
        std::error_code ec;
        std::filesystem::permissions(
            destination_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            ec);
        if (ec) {
            std::cerr << "[Backup] Warning: could not restrict backup permissions: "
                      << ec.message() << std::endl;
        }
    }
    return ok;
}

namespace {
bool column_exists(sqlite3* db, const char* table, const char* column) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("PRAGMA table_info(") + table + ");";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name && std::string(name) == column) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool add_column_if_missing(sqlite3* db, const char* table, const char* column, const char* definition) {
    if (column_exists(db, table, column)) return true;
    std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + definition + ";";
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Migration failed for " << table << "." << column << ": "
                  << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

std::vector<std::string> parse_string_array(const char* raw, const std::vector<std::string>& fallback) {
    if (!raw) return fallback;
    try {
        auto j = nlohmann::json::parse(raw);
        if (j.is_array()) return j.get<std::vector<std::string>>();
    } catch (...) {}
    return fallback;
}

std::string json_string_array(const std::vector<std::string>& values) {
    return nlohmann::json(values).dump();
}
} // namespace

bool Database::run_migrations() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS users (
            id TEXT PRIMARY KEY,
            email TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            role TEXT NOT NULL DEFAULT 'admin',
            publisher_id TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS publishers (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            owner_user_id TEXT,
            status TEXT NOT NULL DEFAULT 'active',
            contact_email TEXT,
            timezone TEXT NOT NULL DEFAULT 'UTC',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_publishers_status ON publishers(status);

        CREATE TABLE IF NOT EXISTS affiliate_offers (
            id TEXT PRIMARY KEY,
            publisher_id TEXT NOT NULL,
            provider TEXT NOT NULL DEFAULT 'shopee_affiliate',
            offer_name TEXT NOT NULL,
            product_title TEXT NOT NULL,
            product_image_url TEXT,
            product_description TEXT,
            merchant_name TEXT,
            affiliate_url TEXT NOT NULL,
            canonical_product_url TEXT,
            price_text TEXT,
            currency TEXT,
            availability_checked_at TEXT,
            disclosure_text TEXT,
            status TEXT NOT NULL DEFAULT 'draft',
            validation_status TEXT NOT NULL DEFAULT 'unverified',
            validation_reason TEXT,
            last_link_validation_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_offers_publisher ON affiliate_offers(publisher_id);
        CREATE INDEX IF NOT EXISTS idx_offers_status ON affiliate_offers(status);

        CREATE TABLE IF NOT EXISTS sessions (
            token TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            expires_at TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS sites (
            id TEXT PRIMARY KEY,
            workspace_id TEXT NOT NULL,
            publisher_id TEXT,
            name TEXT NOT NULL,
            primary_domain TEXT NOT NULL,
            allowed_domains TEXT NOT NULL,
            public_site_key TEXT UNIQUE NOT NULL,
            status TEXT NOT NULL DEFAULT 'active',
            config_version INTEGER NOT NULL DEFAULT 1,
            verification_status TEXT NOT NULL DEFAULT 'verified',
            verification_token TEXT,
            verified_at TEXT,
            monetization_status TEXT NOT NULL DEFAULT 'enabled',
            default_disclosure_text TEXT,
            allowed_ad_sources TEXT NOT NULL DEFAULT '["house","direct","affiliate"]',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_sites_public_key ON sites(public_site_key);

        CREATE TABLE IF NOT EXISTS placements (
            id TEXT PRIMARY KEY,
            site_id TEXT NOT NULL,
            placement_key TEXT NOT NULL,
            name TEXT NOT NULL,
            description TEXT,
            format TEXT NOT NULL DEFAULT 'responsive_banner',
            width INTEGER NOT NULL DEFAULT 0,
            height INTEGER NOT NULL DEFAULT 0,
            fallback_behavior TEXT NOT NULL DEFAULT 'hide',
            allowed_source_types TEXT NOT NULL DEFAULT '["house","direct","affiliate"]',
            max_items INTEGER NOT NULL DEFAULT 1,
            layout_config TEXT NOT NULL DEFAULT '{}',
            disclosure_mode TEXT NOT NULL DEFAULT 'inherited',
            status TEXT NOT NULL DEFAULT 'active',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE(site_id, placement_key),
            FOREIGN KEY(site_id) REFERENCES sites(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS campaigns (
            id TEXT PRIMARY KEY,
            site_id TEXT NOT NULL,
            placement_id TEXT NOT NULL,
            name TEXT NOT NULL,
            destination_url TEXT NOT NULL,
            format TEXT NOT NULL DEFAULT 'responsive_banner',
            title TEXT,
            body TEXT,
            image_url TEXT,
            alt_text TEXT,
            cta_label TEXT,
            source_type TEXT NOT NULL DEFAULT 'house',
            affiliate_offer_id TEXT,
            disclosure_override TEXT,
            status TEXT NOT NULL DEFAULT 'draft',
            starts_at TEXT,
            ends_at TEXT,
            priority INTEGER NOT NULL DEFAULT 1,
            weight INTEGER NOT NULL DEFAULT 100,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            published_at TEXT,
            FOREIGN KEY(site_id) REFERENCES sites(id) ON DELETE CASCADE,
            FOREIGN KEY(placement_id) REFERENCES placements(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS ad_events (
            id TEXT PRIMARY KEY,
            site_id TEXT NOT NULL,
            placement_id TEXT,
            campaign_id TEXT,
            source_type TEXT,
            affiliate_offer_id TEXT,
            event_type TEXT NOT NULL,
            event_id TEXT UNIQUE NOT NULL,
            occurred_at TEXT NOT NULL,
            page_origin TEXT,
            referrer TEXT,
            user_agent_category TEXT,
            ip_hash TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_events_site ON ad_events(site_id);
        CREATE INDEX IF NOT EXISTS idx_events_campaign ON ad_events(campaign_id);
        CREATE INDEX IF NOT EXISTS idx_events_placement ON ad_events(placement_id);
        CREATE INDEX IF NOT EXISTS idx_events_occurred ON ad_events(occurred_at);
        CREATE INDEX IF NOT EXISTS idx_events_type ON ad_events(event_type);

        CREATE TABLE IF NOT EXISTS audit_logs (
            id TEXT PRIMARY KEY,
            actor TEXT NOT NULL,
            action TEXT NOT NULL,
            entity_type TEXT NOT NULL,
            entity_id TEXT NOT NULL,
            details TEXT,
            created_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_logs(created_at);

        -- Public publisher/client applications from the marketing site. These
        -- are reviewed by an administrator and never provision an account.
        CREATE TABLE IF NOT EXISTS partner_inquiries (
            id TEXT PRIMARY KEY,
            kind TEXT NOT NULL CHECK (kind IN ('publisher', 'client')),
            name TEXT NOT NULL,
            email TEXT NOT NULL,
            company TEXT NOT NULL DEFAULT '',
            website TEXT NOT NULL DEFAULT '',
            message TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'new' CHECK (status IN ('new', 'reviewing', 'closed')),
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_partner_inquiries_created ON partner_inquiries(created_at);
        CREATE INDEX IF NOT EXISTS idx_partner_inquiries_kind ON partner_inquiries(kind);
        CREATE INDEX IF NOT EXISTS idx_partner_inquiries_status ON partner_inquiries(status);

        -- Single-row SMTP configuration for administrator-managed email. The
        -- password is AES-256-GCM ciphertext derived from SECRET_KEY and is
        -- never returned by the API.
        CREATE TABLE IF NOT EXISTS email_settings (
            id TEXT PRIMARY KEY CHECK (id = 'default'),
            enabled INTEGER NOT NULL DEFAULT 0,
            host TEXT NOT NULL DEFAULT '',
            port INTEGER NOT NULL DEFAULT 587,
            encryption TEXT NOT NULL DEFAULT 'starttls'
                CHECK (encryption IN ('starttls', 'ssl', 'none')),
            username TEXT NOT NULL DEFAULT '',
            password_cipher TEXT NOT NULL DEFAULT '',
            from_email TEXT NOT NULL DEFAULT '',
            from_name TEXT NOT NULL DEFAULT '',
            reply_to TEXT NOT NULL DEFAULT '',
            updated_at TEXT NOT NULL,
            updated_by TEXT NOT NULL DEFAULT ''
        );
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error in migrations: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    // Additive Phase 2 migrations. Existing Phase 1 databases are upgraded in
    // place; no Phase 1 column is renamed or dropped, and site keys, placement
    // keys, and campaign IDs remain valid.
    const bool columns_ok =
        add_column_if_missing(db_, "users", "publisher_id", "TEXT") &&
        add_column_if_missing(db_, "sites", "publisher_id", "TEXT") &&
        add_column_if_missing(db_, "sites", "verification_status", "TEXT NOT NULL DEFAULT 'verified'") &&
        add_column_if_missing(db_, "sites", "verification_token", "TEXT") &&
        add_column_if_missing(db_, "sites", "verified_at", "TEXT") &&
        add_column_if_missing(db_, "sites", "monetization_status", "TEXT NOT NULL DEFAULT 'enabled'") &&
        add_column_if_missing(db_, "sites", "default_disclosure_text", "TEXT") &&
        add_column_if_missing(db_, "sites", "allowed_ad_sources", "TEXT NOT NULL DEFAULT '[\"house\",\"direct\",\"affiliate\"]'") &&
        add_column_if_missing(db_, "placements", "allowed_source_types", "TEXT NOT NULL DEFAULT '[\"house\",\"direct\",\"affiliate\"]'") &&
        add_column_if_missing(db_, "placements", "max_items", "INTEGER NOT NULL DEFAULT 1") &&
        add_column_if_missing(db_, "placements", "layout_config", "TEXT NOT NULL DEFAULT '{}'") &&
        add_column_if_missing(db_, "placements", "disclosure_mode", "TEXT NOT NULL DEFAULT 'inherited'") &&
        add_column_if_missing(db_, "campaigns", "source_type", "TEXT NOT NULL DEFAULT 'house'") &&
        add_column_if_missing(db_, "campaigns", "affiliate_offer_id", "TEXT") &&
        add_column_if_missing(db_, "campaigns", "disclosure_override", "TEXT") &&
        add_column_if_missing(db_, "ad_events", "source_type", "TEXT") &&
        add_column_if_missing(db_, "ad_events", "affiliate_offer_id", "TEXT");
    if (!columns_ok) return false;

    const char* phase2_indexes = R"(
        CREATE INDEX IF NOT EXISTS idx_sites_publisher ON sites(publisher_id);
        CREATE INDEX IF NOT EXISTS idx_campaigns_offer ON campaigns(affiliate_offer_id);
        CREATE INDEX IF NOT EXISTS idx_events_source ON ad_events(source_type);
        CREATE INDEX IF NOT EXISTS idx_events_offer ON ad_events(affiliate_offer_id);
    )";
    err_msg = nullptr;
    rc = sqlite3_exec(db_, phase2_indexes, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error in Phase 2 indexes: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    // Additive Phase 3 learning schema. Existing ad delivery tables and routes
    // are untouched, so an upgraded database keeps Phase 1/2 behavior.
    const char* learning_schema = R"(
        CREATE TABLE IF NOT EXISTS learning_seed_state (
            key TEXT PRIMARY KEY,
            applied_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS learning_topics (
            id TEXT PRIMARY KEY,
            slug TEXT UNIQUE NOT NULL,
            title TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            status TEXT NOT NULL DEFAULT 'published',
            sort_order INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS learning_courses (
            id TEXT PRIMARY KEY,
            slug TEXT UNIQUE NOT NULL,
            topic_id TEXT NOT NULL,
            title TEXT NOT NULL,
            summary TEXT NOT NULL DEFAULT '',
            description_md TEXT NOT NULL DEFAULT '',
            level TEXT NOT NULL DEFAULT 'beginner',
            duration_minutes INTEGER NOT NULL DEFAULT 0,
            outcomes TEXT NOT NULL DEFAULT '[]',
            status TEXT NOT NULL DEFAULT 'draft',
            author_id TEXT,
            reviewer_id TEXT,
            version INTEGER NOT NULL DEFAULT 1,
            published_at TEXT,
            last_reviewed_at TEXT,
            next_review_due_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(topic_id) REFERENCES learning_topics(id),
            FOREIGN KEY(author_id) REFERENCES users(id),
            FOREIGN KEY(reviewer_id) REFERENCES users(id)
        );
        CREATE INDEX IF NOT EXISTS idx_learning_courses_status ON learning_courses(status);
        CREATE INDEX IF NOT EXISTS idx_learning_courses_topic ON learning_courses(topic_id);
        CREATE INDEX IF NOT EXISTS idx_learning_courses_author ON learning_courses(author_id);

        CREATE TABLE IF NOT EXISTS learning_modules (
            id TEXT PRIMARY KEY,
            course_id TEXT NOT NULL,
            title TEXT NOT NULL,
            summary TEXT NOT NULL DEFAULT '',
            sort_order INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'active',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_modules_course ON learning_modules(course_id);

        CREATE TABLE IF NOT EXISTS learning_lessons (
            id TEXT PRIMARY KEY,
            course_id TEXT NOT NULL,
            module_id TEXT NOT NULL,
            slug TEXT NOT NULL,
            title TEXT NOT NULL,
            content_md TEXT NOT NULL DEFAULT '',
            estimated_minutes INTEGER NOT NULL DEFAULT 10,
            sort_order INTEGER NOT NULL DEFAULT 0,
            is_preview INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'active',
            video_provider TEXT,
            video_id TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE(course_id, slug),
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE,
            FOREIGN KEY(module_id) REFERENCES learning_modules(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_lessons_course ON learning_lessons(course_id);
        CREATE INDEX IF NOT EXISTS idx_learning_lessons_module ON learning_lessons(module_id);

        CREATE TABLE IF NOT EXISTS learning_resources (
            id TEXT PRIMARY KEY,
            course_id TEXT NOT NULL,
            lesson_id TEXT,
            title TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            url TEXT NOT NULL,
            kind TEXT NOT NULL DEFAULT 'link',
            sort_order INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE,
            FOREIGN KEY(lesson_id) REFERENCES learning_lessons(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_resources_course ON learning_resources(course_id);

        CREATE TABLE IF NOT EXISTS learning_quizzes (
            id TEXT PRIMARY KEY,
            lesson_id TEXT UNIQUE NOT NULL,
            course_id TEXT NOT NULL,
            title TEXT NOT NULL,
            passing_score INTEGER NOT NULL DEFAULT 70,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(lesson_id) REFERENCES learning_lessons(id) ON DELETE CASCADE,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS learning_quiz_questions (
            id TEXT PRIMARY KEY,
            quiz_id TEXT NOT NULL,
            prompt TEXT NOT NULL,
            explanation TEXT NOT NULL DEFAULT '',
            sort_order INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(quiz_id) REFERENCES learning_quizzes(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_quiz_questions_quiz ON learning_quiz_questions(quiz_id);

        CREATE TABLE IF NOT EXISTS learning_quiz_options (
            id TEXT PRIMARY KEY,
            question_id TEXT NOT NULL,
            option_text TEXT NOT NULL,
            is_correct INTEGER NOT NULL DEFAULT 0,
            sort_order INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(question_id) REFERENCES learning_quiz_questions(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_quiz_options_question ON learning_quiz_options(question_id);

        CREATE TABLE IF NOT EXISTS learning_paths (
            id TEXT PRIMARY KEY,
            slug TEXT UNIQUE NOT NULL,
            title TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            level TEXT NOT NULL DEFAULT 'beginner',
            status TEXT NOT NULL DEFAULT 'draft',
            sort_order INTEGER NOT NULL DEFAULT 0,
            published_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_learning_paths_status ON learning_paths(status);

        CREATE TABLE IF NOT EXISTS learning_path_courses (
            path_id TEXT NOT NULL,
            course_id TEXT NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY(path_id, course_id),
            FOREIGN KEY(path_id) REFERENCES learning_paths(id) ON DELETE CASCADE,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS learning_enrollments (
            id TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            course_id TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'active',
            last_lesson_id TEXT,
            enrolled_at TEXT NOT NULL,
            completed_at TEXT,
            updated_at TEXT NOT NULL,
            UNIQUE(user_id, course_id),
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_enrollments_user ON learning_enrollments(user_id);
        CREATE INDEX IF NOT EXISTS idx_learning_enrollments_course ON learning_enrollments(course_id);

        CREATE TABLE IF NOT EXISTS learning_lesson_progress (
            id TEXT PRIMARY KEY,
            enrollment_id TEXT NOT NULL,
            lesson_id TEXT NOT NULL,
            completed_at TEXT NOT NULL,
            created_at TEXT NOT NULL,
            UNIQUE(enrollment_id, lesson_id),
            FOREIGN KEY(enrollment_id) REFERENCES learning_enrollments(id) ON DELETE CASCADE,
            FOREIGN KEY(lesson_id) REFERENCES learning_lessons(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_progress_enrollment ON learning_lesson_progress(enrollment_id);

        CREATE TABLE IF NOT EXISTS learning_quiz_attempts (
            id TEXT PRIMARY KEY,
            enrollment_id TEXT NOT NULL,
            user_id TEXT NOT NULL,
            course_id TEXT NOT NULL,
            lesson_id TEXT NOT NULL,
            quiz_id TEXT NOT NULL,
            score INTEGER NOT NULL,
            passed INTEGER NOT NULL DEFAULT 0,
            correct_count INTEGER NOT NULL DEFAULT 0,
            total_questions INTEGER NOT NULL DEFAULT 0,
            attempted_at TEXT NOT NULL,
            FOREIGN KEY(enrollment_id) REFERENCES learning_enrollments(id) ON DELETE CASCADE,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE,
            FOREIGN KEY(lesson_id) REFERENCES learning_lessons(id) ON DELETE CASCADE,
            FOREIGN KEY(quiz_id) REFERENCES learning_quizzes(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_learning_attempts_user ON learning_quiz_attempts(user_id);
        CREATE INDEX IF NOT EXISTS idx_learning_attempts_course ON learning_quiz_attempts(course_id);

        CREATE TABLE IF NOT EXISTS learning_course_revisions (
            id TEXT PRIMARY KEY,
            course_id TEXT NOT NULL,
            version INTEGER NOT NULL,
            created_by TEXT,
            note TEXT NOT NULL DEFAULT '',
            snapshot TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(course_id) REFERENCES learning_courses(id) ON DELETE CASCADE,
            FOREIGN KEY(created_by) REFERENCES users(id)
        );
        CREATE INDEX IF NOT EXISTS idx_learning_revisions_course ON learning_course_revisions(course_id);
    )";
    err_msg = nullptr;
    rc = sqlite3_exec(db_, learning_schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error in Phase 3 learning schema: " << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    // Legacy campaigns were managed exclusively by the platform owner. They are
    // classified as house inventory rather than guessing an external advertiser;
    // the operator can change the source on each campaign.
    sqlite3_exec(db_, "UPDATE campaigns SET source_type = 'house' WHERE source_type IS NULL OR source_type = '';",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "UPDATE sites SET verification_status = 'verified' WHERE publisher_id IS NULL OR publisher_id = '';",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "UPDATE sites SET monetization_status = 'enabled' WHERE publisher_id IS NULL OR publisher_id = '';",
                 nullptr, nullptr, nullptr);
    return true;
}

bool Database::seed_default_admin() {
    const char* env_email = std::getenv("ADMIN_EMAIL");
    const char* env_password = std::getenv("ADMIN_PASSWORD");
    const char* env_mode = std::getenv("ADCONTROL_ENV");
    const bool production = env_mode && std::string(env_mode) == "production";

    const std::string email = (env_email && *env_email) ? env_email : "admin@adcontrol.local";

    auto existing = get_user_by_email(email);
    if (existing) {
        return true;
    }

    std::string password;
    bool using_dev_default = false;
    if (env_password && *env_password) {
        password = env_password;
        if (production && password.size() < 12) {
            std::cerr << "[Fatal] ADMIN_PASSWORD must be at least 12 characters in production." << std::endl;
            return false;
        }
    } else if (production) {
        std::cerr << "[Fatal] No administrator exists. Set ADMIN_PASSWORD (>= 12 chars) before starting in production." << std::endl;
        return false;
    } else {
        password = "admin12345";
        using_dev_default = true;
    }

    User admin;
    admin.id = utils::generate_uuid();
    admin.email = email;
    admin.salt = utils::random_hex(16);
    admin.password_hash = utils::hash_password(password, admin.salt);
    admin.role = "admin";
    admin.created_at = utils::get_current_iso8601();
    admin.updated_at = admin.created_at;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO users (id, email, password_hash, salt, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, admin.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, admin.email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, admin.password_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, admin.salt.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, admin.role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, admin.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, admin.updated_at.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return false;
    }

    if (using_dev_default) {
        std::cout << "[DB] WARNING: seeded development administrator " << email
                  << " with the default password. Set ADMIN_PASSWORD before production use." << std::endl;
    } else {
        std::cout << "[DB] Seeded administrator account: " << email << std::endl;
    }
    return true;
}

std::optional<User> Database::get_user_by_email(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, email, password_hash, salt, role, COALESCE(publisher_id, ''), created_at, updated_at FROM users WHERE email = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);

    std::optional<User> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u.publisher_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        u.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        result = u;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> Database::get_user_by_id(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, email, password_hash, salt, role, COALESCE(publisher_id, ''), created_at, updated_at FROM users WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    std::optional<User> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u.publisher_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        u.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        result = u;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::create_session(const std::string& token, const std::string& user_id, const std::string& expires_at) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO sessions (token, user_id, expires_at, created_at) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, expires_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::optional<User> Database::get_user_by_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT u.id, u.email, u.password_hash, u.salt, u.role, COALESCE(u.publisher_id, ''), u.created_at, u.updated_at
        FROM sessions s
        JOIN users u ON s.user_id = u.id
        WHERE s.token = ? AND s.expires_at > ?
        LIMIT 1;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);

    std::optional<User> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u.publisher_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        u.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        result = u;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::delete_session(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM sessions WHERE token = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::delete_user_sessions(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM sessions WHERE user_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::update_user_password(const std::string& user_id, const std::string& password_hash, const std::string& salt) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE users SET password_hash = ?, salt = ?, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, password_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);
}

// Publishers
namespace {
Publisher publisher_from_row(sqlite3_stmt* stmt) {
    Publisher p;
    p.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* owner_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (owner_id) p.owner_user_id = owner_id;
    const char* owner_email = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (owner_email) p.owner_email = owner_email;
    p.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const char* contact = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    if (contact) p.contact_email = contact;
    const char* tz = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if (tz) p.timezone = tz;
    p.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    p.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    return p;
}
} // namespace

bool Database::create_publisher_with_owner(Publisher& publisher, User& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (publisher.id.empty()) publisher.id = utils::generate_uuid();
    if (owner.id.empty()) owner.id = utils::generate_uuid();
    const std::string now = utils::get_current_iso8601();
    publisher.owner_user_id = owner.id;
    publisher.created_at = now;
    publisher.updated_at = now;
    owner.publisher_id = publisher.id;
    owner.role = "publisher";
    owner.created_at = now;
    owner.updated_at = now;

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt = nullptr;
    const char* pub_sql = R"(
        INSERT INTO publishers (id, name, owner_user_id, status, contact_email, timezone, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    bool ok = sqlite3_prepare_v2(db_, pub_sql, -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt, 1, publisher.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, publisher.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, publisher.owner_user_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, publisher.status.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, publisher.contact_email.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, publisher.timezone.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, publisher.created_at.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, publisher.updated_at.c_str(), -1, SQLITE_STATIC);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    if (ok) {
        const char* user_sql = R"(
            INSERT INTO users (id, email, password_hash, salt, role, publisher_id, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?);
        )";
        ok = sqlite3_prepare_v2(db_, user_sql, -1, &stmt, nullptr) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_text(stmt, 1, owner.id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, owner.email.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, owner.password_hash.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, owner.salt.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, owner.role.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, owner.publisher_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 7, owner.created_at.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 8, owner.updated_at.c_str(), -1, SQLITE_STATIC);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

std::vector<Publisher> Database::list_publishers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Publisher> list;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT p.id, p.name, p.owner_user_id, COALESCE(u.email, ''), p.status,
               p.contact_email, p.timezone, p.created_at, p.updated_at
        FROM publishers p
        LEFT JOIN users u ON u.id = p.owner_user_id
        ORDER BY p.created_at DESC;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(publisher_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<Publisher> Database::get_publisher(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT p.id, p.name, p.owner_user_id, COALESCE(u.email, ''), p.status,
               p.contact_email, p.timezone, p.created_at, p.updated_at
        FROM publishers p
        LEFT JOIN users u ON u.id = p.owner_user_id
        WHERE p.id = ? LIMIT 1;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<Publisher> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = publisher_from_row(stmt);
    sqlite3_finalize(stmt);
    return result;
}

bool Database::update_publisher(const Publisher& publisher) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE publishers SET name = ?, status = ?, contact_email = ?, timezone = ?, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, publisher.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, publisher.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, publisher.contact_email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, publisher.timezone.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, publisher.id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || sqlite3_changes(db_) == 0) return false;

    // Force delivery caches to revalidate when a publisher is suspended or
    // reinstated. Campaign data itself is untouched.
    const char* bump = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE publisher_id = ?;";
    if (sqlite3_prepare_v2(db_, bump, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, publisher.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
}

// Partner inquiries
namespace {
PartnerInquiry partner_inquiry_from_row(sqlite3_stmt* stmt) {
    auto text = [&](int i) -> std::string {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return v ? v : "";
    };
    PartnerInquiry i;
    i.id = text(0);
    i.kind = text(1);
    i.name = text(2);
    i.email = text(3);
    i.company = text(4);
    i.website = text(5);
    i.message = text(6);
    i.status = text(7);
    i.created_at = text(8);
    i.updated_at = text(9);
    return i;
}

const char* kPartnerInquiryColumns =
    "id, kind, name, email, company, website, message, status, created_at, updated_at";
} // namespace

bool Database::create_partner_inquiry(PartnerInquiry& inquiry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inquiry.id.empty()) inquiry.id = utils::generate_uuid();
    if (inquiry.status.empty()) inquiry.status = "new";
    const std::string now = utils::get_current_iso8601();
    if (inquiry.created_at.empty()) inquiry.created_at = now;
    inquiry.updated_at = now;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO partner_inquiries
            (id, kind, name, email, company, website, message, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, inquiry.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, inquiry.kind.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, inquiry.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, inquiry.email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, inquiry.company.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, inquiry.website.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, inquiry.message.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, inquiry.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, inquiry.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, inquiry.updated_at.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<PartnerInquiry> Database::list_partner_inquiries(
    const std::optional<std::string>& kind, const std::optional<std::string>& status,
    int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PartnerInquiry> list;

    std::string sql = std::string("SELECT ") + kPartnerInquiryColumns +
                      " FROM partner_inquiries WHERE 1=1";
    if (kind) sql += " AND kind = ?";
    if (status) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;
    int index = 1;
    if (kind) sqlite3_bind_text(stmt, index++, kind->c_str(), -1, SQLITE_STATIC);
    if (status) sqlite3_bind_text(stmt, index++, status->c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, index++, limit);
    sqlite3_bind_int(stmt, index, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(partner_inquiry_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<PartnerInquiry> Database::get_partner_inquiry(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + kPartnerInquiryColumns +
                            " FROM partner_inquiries WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<PartnerInquiry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = partner_inquiry_from_row(stmt);
    sqlite3_finalize(stmt);
    return result;
}

bool Database::update_partner_inquiry_status(const std::string& id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE partner_inquiries SET status = ?, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    const std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

// Email / SMTP settings
std::optional<EmailSettings> Database::get_email_settings() {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT enabled, host, port, encryption, username, password_cipher, "
        "from_email, from_name, reply_to, updated_at, updated_by "
        "FROM email_settings WHERE id = 'default' LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::optional<EmailSettings> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = [&](int index) -> std::string {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
            return value ? value : "";
        };
        EmailSettings settings;
        settings.enabled = sqlite3_column_int(stmt, 0) != 0;
        settings.host = text(1);
        settings.port = sqlite3_column_int(stmt, 2);
        settings.encryption = text(3);
        settings.username = text(4);
        settings.password_cipher = text(5);
        settings.from_email = text(6);
        settings.from_name = text(7);
        settings.reply_to = text(8);
        settings.updated_at = text(9);
        settings.updated_by = text(10);
        result = settings;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::save_email_settings(const EmailSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO email_settings
            (id, enabled, host, port, encryption, username, password_cipher,
             from_email, from_name, reply_to, updated_at, updated_by)
        VALUES ('default', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            enabled = excluded.enabled,
            host = excluded.host,
            port = excluded.port,
            encryption = excluded.encryption,
            username = excluded.username,
            password_cipher = excluded.password_cipher,
            from_email = excluded.from_email,
            from_name = excluded.from_name,
            reply_to = excluded.reply_to,
            updated_at = excluded.updated_at,
            updated_by = excluded.updated_by;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, settings.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, settings.host.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, settings.port);
    sqlite3_bind_text(stmt, 4, settings.encryption.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, settings.username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, settings.password_cipher.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, settings.from_email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, settings.from_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, settings.reply_to.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, settings.updated_by.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// Affiliate offers
namespace {
AffiliateOffer offer_from_row(sqlite3_stmt* stmt) {
    AffiliateOffer o;
    auto text = [&](int i) -> std::string {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return v ? v : "";
    };
    o.id = text(0);
    o.publisher_id = text(1);
    o.provider = text(2);
    o.offer_name = text(3);
    o.product_title = text(4);
    o.product_image_url = text(5);
    o.product_description = text(6);
    o.merchant_name = text(7);
    o.affiliate_url = text(8);
    o.canonical_product_url = text(9);
    o.price_text = text(10);
    o.currency = text(11);
    o.availability_checked_at = text(12);
    o.disclosure_text = text(13);
    o.status = text(14);
    o.validation_status = text(15);
    o.validation_reason = text(16);
    o.last_link_validation_at = text(17);
    o.created_at = text(18);
    o.updated_at = text(19);
    return o;
}

bool bind_offer_insert(sqlite3_stmt* stmt, const AffiliateOffer& o, bool include_id) {
    int i = 1;
    if (include_id) sqlite3_bind_text(stmt, i++, o.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.publisher_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.provider.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.offer_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.product_title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.product_image_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.product_description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.merchant_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.affiliate_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.canonical_product_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.price_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.currency.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.availability_checked_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.disclosure_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.validation_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.validation_reason.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.last_link_validation_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, o.updated_at.c_str(), -1, SQLITE_STATIC);
    return true;
}
} // namespace

std::vector<AffiliateOffer> Database::list_affiliate_offers(const std::optional<std::string>& publisher_id,
                                                            const std::optional<std::string>& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AffiliateOffer> list;
    std::string sql = R"(
        SELECT id, publisher_id, provider, offer_name, product_title, product_image_url,
               product_description, merchant_name, affiliate_url, canonical_product_url,
               price_text, currency, availability_checked_at, disclosure_text, status,
               validation_status, validation_reason, last_link_validation_at, created_at, updated_at
        FROM affiliate_offers WHERE 1=1
    )";
    if (publisher_id.has_value()) sql += " AND publisher_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;
    int idx = 1;
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);
    if (status.has_value()) sqlite3_bind_text(stmt, idx++, status->c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(offer_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<AffiliateOffer> Database::get_affiliate_offer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, publisher_id, provider, offer_name, product_title, product_image_url,
               product_description, merchant_name, affiliate_url, canonical_product_url,
               price_text, currency, availability_checked_at, disclosure_text, status,
               validation_status, validation_reason, last_link_validation_at, created_at, updated_at
        FROM affiliate_offers WHERE id = ? LIMIT 1;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<AffiliateOffer> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = offer_from_row(stmt);
    sqlite3_finalize(stmt);
    return result;
}

bool Database::create_affiliate_offer(AffiliateOffer& offer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (offer.id.empty()) offer.id = utils::generate_uuid();
    offer.created_at = utils::get_current_iso8601();
    offer.updated_at = offer.created_at;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO affiliate_offers
        (id, publisher_id, provider, offer_name, product_title, product_image_url,
         product_description, merchant_name, affiliate_url, canonical_product_url,
         price_text, currency, availability_checked_at, disclosure_text, status,
         validation_status, validation_reason, last_link_validation_at, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    bind_offer_insert(stmt, offer, true);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::update_affiliate_offer(const AffiliateOffer& offer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE affiliate_offers SET
            provider = ?, offer_name = ?, product_title = ?, product_image_url = ?,
            product_description = ?, merchant_name = ?, affiliate_url = ?,
            canonical_product_url = ?, price_text = ?, currency = ?,
            availability_checked_at = ?, disclosure_text = ?, status = ?,
            validation_status = ?, validation_reason = ?, last_link_validation_at = ?,
            updated_at = ?
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int i = 1;
    sqlite3_bind_text(stmt, i++, offer.provider.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.offer_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.product_title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.product_image_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.product_description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.merchant_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.affiliate_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.canonical_product_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.price_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.currency.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.availability_checked_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.disclosure_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.validation_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.validation_reason.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.last_link_validation_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, offer.id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || sqlite3_changes(db_) == 0) return false;

    // The offer is the source of truth for affiliate creatives. Keep linked
    // campaign rows aligned so dashboards and exports do not show stale copy.
    const char* sync = R"(
        UPDATE campaigns SET destination_url = ?, title = ?, body = ?, image_url = ?,
            alt_text = ?, updated_at = ?
        WHERE affiliate_offer_id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sync, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, offer.affiliate_url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, offer.product_title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, offer.product_description.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, offer.product_image_url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, offer.product_title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, offer.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* bump = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id IN (SELECT site_id FROM campaigns WHERE affiliate_offer_id = ?);";
    if (sqlite3_prepare_v2(db_, bump, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, offer.id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
}

bool Database::set_affiliate_offer_status(const std::string& id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE affiliate_offers SET status = ?, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || sqlite3_changes(db_) == 0) return false;

    const char* bump = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id IN (SELECT site_id FROM campaigns WHERE affiliate_offer_id = ?);";
    if (sqlite3_prepare_v2(db_, bump, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
}

bool Database::delete_affiliate_offer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM affiliate_offers WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::affiliate_offer_has_campaigns(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM campaigns WHERE affiliate_offer_id = ? LIMIT 1;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

std::vector<Campaign> Database::list_campaigns_by_offer(const std::string& offer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Campaign> list;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, site_id, placement_id, name, destination_url, format, title, body,
               image_url, alt_text, cta_label, status, source_type, affiliate_offer_id,
               disclosure_override, starts_at, ends_at, priority, weight, created_at,
               updated_at, published_at
        FROM campaigns WHERE affiliate_offer_id = ? ORDER BY created_at DESC;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    sqlite3_bind_text(stmt, 1, offer_id.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = [&](int i) -> std::string {
            const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return v ? v : "";
        };
        Campaign c;
        c.id = text(0);
        c.site_id = text(1);
        c.placement_id = text(2);
        c.name = text(3);
        c.destination_url = text(4);
        c.format = text(5);
        c.title = text(6);
        c.body = text(7);
        c.image_url = text(8);
        c.alt_text = text(9);
        c.cta_label = text(10);
        c.status = text(11);
        c.source_type = text(12).empty() ? "house" : text(12);
        c.affiliate_offer_id = text(13);
        c.disclosure_override = text(14);
        c.starts_at = text(15);
        c.ends_at = text(16);
        c.priority = sqlite3_column_int(stmt, 17);
        c.weight = sqlite3_column_int(stmt, 18);
        c.created_at = text(19);
        c.updated_at = text(20);
        c.published_at = text(21);
        list.push_back(c);
    }
    sqlite3_finalize(stmt);
    return list;
}

// Sites
namespace {
const char* SITE_SELECT_COLUMNS =
    "id, workspace_id, COALESCE(publisher_id, ''), name, primary_domain, allowed_domains, "
    "public_site_key, status, config_version, verification_status, COALESCE(verification_token, ''), "
    "COALESCE(verified_at, ''), monetization_status, COALESCE(default_disclosure_text, ''), "
    "allowed_ad_sources, created_at, updated_at";

Site site_from_row(sqlite3_stmt* stmt) {
    auto text = [&](int i) -> std::string {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return v ? v : "";
    };
    Site s;
    s.id = text(0);
    s.workspace_id = text(1);
    s.publisher_id = text(2);
    s.name = text(3);
    s.primary_domain = text(4);
    s.allowed_domains = parse_string_array(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)), {});
    s.public_site_key = text(6);
    s.status = text(7);
    s.config_version = sqlite3_column_int(stmt, 8);
    s.verification_status = text(9);
    s.verification_token = text(10);
    s.verified_at = text(11);
    s.monetization_status = text(12);
    s.default_disclosure_text = text(13);
    s.allowed_ad_sources = parse_string_array(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)),
        {"house", "direct", "affiliate"});
    s.created_at = text(15);
    s.updated_at = text(16);
    return s;
}
} // namespace

std::vector<Site> Database::list_sites(const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Site> list;
    std::string sql = std::string("SELECT ") + SITE_SELECT_COLUMNS + " FROM sites";
    if (publisher_id.has_value()) sql += " WHERE publisher_id = ?";
    sql += " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;
    if (publisher_id.has_value()) {
        sqlite3_bind_text(stmt, 1, publisher_id->c_str(), -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(site_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<Site> Database::get_site(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + SITE_SELECT_COLUMNS + " FROM sites WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<Site> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) res = site_from_row(stmt);
    sqlite3_finalize(stmt);
    return res;
}

std::optional<Site> Database::get_site_by_public_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + SITE_SELECT_COLUMNS + " FROM sites WHERE public_site_key = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    std::optional<Site> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) res = site_from_row(stmt);
    sqlite3_finalize(stmt);
    return res;
}

bool Database::create_site(Site& site) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (site.id.empty()) site.id = utils::generate_uuid();
    if (site.public_site_key.empty()) site.public_site_key = "site_pub_" + utils::random_hex(12);
    if (site.workspace_id.empty()) site.workspace_id = "default";
    if (site.allowed_ad_sources.empty()) {
        site.allowed_ad_sources = {"house", "direct", "affiliate"};
    }

    // A publisher-owned site is never deliverable until domain ownership is
    // verified. Platform-owned (legacy) sites remain trusted and verified.
    if (!site.publisher_id.empty()) {
        if (site.verified_at.empty()) {
            site.verification_status = "unverified";
            site.monetization_status = "pending";
            if (site.verification_token.empty()) {
                site.verification_token = "verify_" + utils::random_hex(16);
            }
        }
    } else {
        site.verification_status = "verified";
        site.monetization_status = "enabled";
        site.verification_token.clear();
    }

    site.config_version = 1;
    site.created_at = utils::get_current_iso8601();
    site.updated_at = site.created_at;

    const std::string allowed_str = json_string_array(site.allowed_domains);
    const std::string sources_str = json_string_array(site.allowed_ad_sources);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO sites
        (id, workspace_id, publisher_id, name, primary_domain, allowed_domains,
         public_site_key, status, config_version, verification_status,
         verification_token, verified_at, monetization_status,
         default_disclosure_text, allowed_ad_sources, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    int i = 1;
    sqlite3_bind_text(stmt, i++, site.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.workspace_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.publisher_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.primary_domain.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, allowed_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.public_site_key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, i++, site.config_version);
    sqlite3_bind_text(stmt, i++, site.verification_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.verification_token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.verified_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.monetization_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.default_disclosure_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, sources_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, site.updated_at.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::update_site(const Site& site) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string allowed_str = json_string_array(site.allowed_domains);
    const std::string sources_str = json_string_array(site.allowed_ad_sources);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE sites SET name = ?, primary_domain = ?, allowed_domains = ?, status = ?,
            default_disclosure_text = ?, allowed_ad_sources = ?,
            config_version = config_version + 1, updated_at = ?
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, site.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, site.primary_domain.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, allowed_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, site.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, site.default_disclosure_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, sources_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, site.id.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::mark_site_verified(const std::string& site_id, const std::string& method) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    (void)method;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE sites SET verification_status = 'verified', verified_at = ?,
            monetization_status = 'enabled', verification_token = '',
            config_version = config_version + 1, updated_at = ?
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, site_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::reset_site_verification(const std::string& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    const std::string token = "verify_" + utils::random_hex(16);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE sites SET verification_status = 'unverified', verified_at = '',
            monetization_status = 'pending', verification_token = ?,
            config_version = config_version + 1, updated_at = ?
        WHERE id = ? AND publisher_id != '';
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, site_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::rotate_site_key(const std::string& site_id, std::string& new_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    new_key = "site_pub_" + utils::random_hex(12);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE sites SET public_site_key = ?, config_version = config_version + 1, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, new_key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, site_id.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);
}

bool Database::pause_all_campaigns_for_site(const std::string& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    // Pause scheduled campaigns too. Otherwise the site-level kill switch would
    // leave a future campaign ready to activate as soon as its window opens.
    const char* sql = "UPDATE campaigns SET status = 'paused', updated_at = ? WHERE site_id = ? AND status IN ('active', 'scheduled');";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, site_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Bump site config version
    const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, site_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
}

bool Database::delete_site(const std::string& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM sites WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, site_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);
}

bool Database::site_has_events(const std::string& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM ad_events WHERE site_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, site_id.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool Database::bump_site_version(const std::string& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, site_id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

// Placements
namespace {
const char* PLACEMENT_SELECT_COLUMNS =
    "id, site_id, placement_key, name, description, format, width, height, "
    "fallback_behavior, allowed_source_types, max_items, layout_config, "
    "disclosure_mode, status, created_at, updated_at";

Placement placement_from_row(sqlite3_stmt* stmt, int base = 0) {
    auto text = [&](int i) -> std::string {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, base + i));
        return v ? v : "";
    };
    Placement p;
    p.id = text(0);
    p.site_id = text(1);
    p.placement_key = text(2);
    p.name = text(3);
    p.description = text(4);
    p.format = text(5);
    p.width = sqlite3_column_int(stmt, base + 6);
    p.height = sqlite3_column_int(stmt, base + 7);
    p.fallback_behavior = text(8);
    p.allowed_source_types = parse_string_array(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, base + 9)),
        {"house", "direct", "affiliate"});
    p.max_items = sqlite3_column_int(stmt, base + 10);
    p.layout_config = text(11);
    p.disclosure_mode = text(12);
    p.status = text(13);
    p.created_at = text(14);
    p.updated_at = text(15);
    return p;
}
} // namespace

std::vector<Placement> Database::list_placements(const std::optional<std::string>& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Placement> list;
    std::string sql = std::string("SELECT ") + PLACEMENT_SELECT_COLUMNS + " FROM placements";
    if (site_id.has_value()) {
        sql += " WHERE site_id = ?";
    }
    sql += " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;
    if (site_id.has_value()) {
        sqlite3_bind_text(stmt, 1, site_id->c_str(), -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(placement_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<Placement> Database::get_placement(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + PLACEMENT_SELECT_COLUMNS + " FROM placements WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<Placement> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) res = placement_from_row(stmt);
    sqlite3_finalize(stmt);
    return res;
}

std::optional<Placement> Database::get_placement_by_site_and_key(const std::string& site_id, const std::string& placement_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + PLACEMENT_SELECT_COLUMNS + " FROM placements WHERE site_id = ? AND placement_key = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, site_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, placement_key.c_str(), -1, SQLITE_STATIC);
    std::optional<Placement> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) res = placement_from_row(stmt);
    sqlite3_finalize(stmt);
    return res;
}

bool Database::create_placement(Placement& placement) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (placement.id.empty()) placement.id = utils::generate_uuid();
    if (placement.allowed_source_types.empty()) {
        placement.allowed_source_types = {"house", "direct", "affiliate"};
    }
    if (placement.layout_config.empty()) placement.layout_config = "{}";
    placement.created_at = utils::get_current_iso8601();
    placement.updated_at = placement.created_at;
    const std::string sources_str = json_string_array(placement.allowed_source_types);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO placements
        (id, site_id, placement_key, name, description, format, width, height,
         fallback_behavior, allowed_source_types, max_items, layout_config,
         disclosure_mode, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    int i = 1;
    sqlite3_bind_text(stmt, i++, placement.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.site_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.placement_key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.format.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, i++, placement.width);
    sqlite3_bind_int(stmt, i++, placement.height);
    sqlite3_bind_text(stmt, i++, placement.fallback_behavior.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, sources_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, i++, placement.max_items);
    sqlite3_bind_text(stmt, i++, placement.layout_config.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.disclosure_mode.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, placement.updated_at.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        // bump site version
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string now = utils::get_current_iso8601();
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, placement.site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::update_placement(const Placement& placement) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();
    const std::string sources_str = json_string_array(placement.allowed_source_types);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE placements SET name = ?, description = ?, format = ?, width = ?,
            height = ?, fallback_behavior = ?, allowed_source_types = ?, max_items = ?,
            layout_config = ?, disclosure_mode = ?, status = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, placement.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, placement.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, placement.format.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, placement.width);
    sqlite3_bind_int(stmt, 5, placement.height);
    sqlite3_bind_text(stmt, 6, placement.fallback_behavior.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, sources_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, placement.max_items);
    sqlite3_bind_text(stmt, 9, placement.layout_config.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, placement.disclosure_mode.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, placement.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, placement.id.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        // Bump site config version
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, placement.site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::delete_placement(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string site_id;
    {
        sqlite3_stmt* lookup = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT site_id FROM placements WHERE id = ?;", -1, &lookup, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(lookup, 1, id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(lookup) == SQLITE_ROW) {
            const char* sid = reinterpret_cast<const char*>(sqlite3_column_text(lookup, 0));
            if (sid) site_id = sid;
        }
        sqlite3_finalize(lookup);
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM placements WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    bool deleted = (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);

    if (deleted && !site_id.empty()) {
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string now = utils::get_current_iso8601();
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return deleted;
}

bool Database::archive_placement(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE placements SET status = 'disabled', updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = (SELECT site_id FROM placements WHERE id = ?);";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::placement_has_events(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM ad_events WHERE placement_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

// Campaigns
namespace {
const char* CAMPAIGN_SELECT_COLUMNS =
    "id, site_id, placement_id, name, destination_url, format, title, body, "
    "image_url, alt_text, cta_label, status, source_type, affiliate_offer_id, "
    "disclosure_override, starts_at, ends_at, priority, weight, created_at, "
    "updated_at, published_at";

Campaign campaign_from_row(sqlite3_stmt* stmt, int base = 0) {
    auto text = [&](int i) -> std::string {
        const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, base + i));
        return v ? v : "";
    };
    Campaign c;
    c.id = text(0);
    c.site_id = text(1);
    c.placement_id = text(2);
    c.name = text(3);
    c.destination_url = text(4);
    c.format = text(5);
    c.title = text(6);
    c.body = text(7);
    c.image_url = text(8);
    c.alt_text = text(9);
    c.cta_label = text(10);
    c.status = text(11);
    c.source_type = text(12).empty() ? "house" : text(12);
    c.affiliate_offer_id = text(13);
    c.disclosure_override = text(14);
    c.starts_at = text(15);
    c.ends_at = text(16);
    c.priority = sqlite3_column_int(stmt, base + 17);
    c.weight = sqlite3_column_int(stmt, base + 18);
    c.created_at = text(19);
    c.updated_at = text(20);
    c.published_at = text(21);
    return c;
}
} // namespace

std::vector<Campaign> Database::list_campaigns(const std::optional<std::string>& site_id,
                                             const std::optional<std::string>& placement_id,
                                             const std::optional<std::string>& status,
                                             const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Campaign> list;
    std::string sql = std::string("SELECT ") + CAMPAIGN_SELECT_COLUMNS + " FROM campaigns WHERE 1=1";

    if (site_id.has_value()) sql += " AND site_id = ?";
    if (placement_id.has_value()) sql += " AND placement_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    if (publisher_id.has_value()) {
        sql += " AND site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    }
    sql += " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;

    int idx = 1;
    if (site_id.has_value()) sqlite3_bind_text(stmt, idx++, site_id->c_str(), -1, SQLITE_STATIC);
    if (placement_id.has_value()) sqlite3_bind_text(stmt, idx++, placement_id->c_str(), -1, SQLITE_STATIC);
    if (status.has_value()) sqlite3_bind_text(stmt, idx++, status->c_str(), -1, SQLITE_STATIC);
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) list.push_back(campaign_from_row(stmt));
    sqlite3_finalize(stmt);
    return list;
}

std::optional<Campaign> Database::get_campaign(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + CAMPAIGN_SELECT_COLUMNS + " FROM campaigns WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    std::optional<Campaign> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) res = campaign_from_row(stmt);
    sqlite3_finalize(stmt);
    return res;
}

bool Database::create_campaign(Campaign& campaign) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (campaign.id.empty()) campaign.id = utils::generate_uuid();
    if (campaign.source_type.empty()) campaign.source_type = "house";
    campaign.created_at = utils::get_current_iso8601();
    campaign.updated_at = campaign.created_at;
    if (campaign.status == "active" && campaign.published_at.empty()) {
        campaign.published_at = campaign.created_at;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO campaigns
        (id, site_id, placement_id, name, destination_url, format, title, body,
         image_url, alt_text, cta_label, source_type, affiliate_offer_id,
         disclosure_override, status, starts_at, ends_at, priority, weight,
         created_at, updated_at, published_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    int i = 1;
    sqlite3_bind_text(stmt, i++, campaign.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.site_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.placement_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.destination_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.format.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.body.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.image_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.alt_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.cta_label.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.source_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.affiliate_offer_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.disclosure_override.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.starts_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.ends_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, i++, campaign.priority);
    sqlite3_bind_int(stmt, i++, campaign.weight);
    sqlite3_bind_text(stmt, i++, campaign.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.updated_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, i++, campaign.published_at.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        // Bump site version
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string now = utils::get_current_iso8601();
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, campaign.site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::update_campaign(const Campaign& campaign) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE campaigns SET
            placement_id = ?, name = ?, destination_url = ?, format = ?,
            title = ?, body = ?, image_url = ?, alt_text = ?, cta_label = ?,
            source_type = ?, affiliate_offer_id = ?, disclosure_override = ?,
            status = ?, starts_at = ?, ends_at = ?, priority = ?, weight = ?,
            updated_at = ?, published_at = CASE WHEN ? = 'active' AND (published_at IS NULL OR published_at = '') THEN ? ELSE published_at END
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, campaign.placement_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, campaign.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, campaign.destination_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, campaign.format.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, campaign.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, campaign.body.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, campaign.image_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, campaign.alt_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, campaign.cta_label.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, campaign.source_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, campaign.affiliate_offer_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, campaign.disclosure_override.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, campaign.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 14, campaign.starts_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 15, campaign.ends_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 16, campaign.priority);
    sqlite3_bind_int(stmt, 17, campaign.weight);
    sqlite3_bind_text(stmt, 18, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 19, campaign.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 20, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 21, campaign.id.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        // Bump site version
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, campaign.site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::publish_campaign(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE campaigns SET
            status = CASE
                WHEN starts_at IS NOT NULL AND starts_at <> '' AND starts_at > ? THEN 'scheduled'
                WHEN ends_at IS NOT NULL AND ends_at <> '' AND ends_at < ? THEN 'expired'
                ELSE 'active'
            END,
            published_at = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
        // Bump site config
        const char* site_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = (SELECT site_id FROM campaigns WHERE id = ?);";
        if (sqlite3_prepare_v2(db_, site_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::pause_campaign(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE campaigns SET status = 'paused', updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
        // Bump site config
        const char* site_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = (SELECT site_id FROM campaigns WHERE id = ?);";
        if (sqlite3_prepare_v2(db_, site_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::delete_campaign(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string site_id;
    {
        sqlite3_stmt* lookup = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT site_id FROM campaigns WHERE id = ?;", -1, &lookup, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(lookup, 1, id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(lookup) == SQLITE_ROW) {
            const char* sid = reinterpret_cast<const char*>(sqlite3_column_text(lookup, 0));
            if (sid) site_id = sid;
        }
        sqlite3_finalize(lookup);
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM campaigns WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    bool deleted = (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);

    if (deleted && !site_id.empty()) {
        const char* bump_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = ?;";
        if (sqlite3_prepare_v2(db_, bump_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string now = utils::get_current_iso8601();
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, site_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return deleted;
}

bool Database::archive_campaign(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE campaigns SET status = 'archived', updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string now = utils::get_current_iso8601();
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
        const char* site_sql = "UPDATE sites SET config_version = config_version + 1, updated_at = ? WHERE id = (SELECT site_id FROM campaigns WHERE id = ?);";
        if (sqlite3_prepare_v2(db_, site_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    }
    return false;
}

bool Database::campaign_has_events(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM ad_events WHERE campaign_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

int Database::advance_campaign_states(const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    int changed = 0;

    // Scheduled campaigns whose start time has arrived become active.
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            UPDATE campaigns SET status = 'active', updated_at = ?
            WHERE status = 'scheduled'
              AND (starts_at IS NULL OR starts_at = '' OR starts_at <= ?)
              AND (ends_at IS NULL OR ends_at = '' OR ends_at >= ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now_iso.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, now_iso.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, now_iso.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_DONE) changed += sqlite3_changes(db_);
            sqlite3_finalize(stmt);
        }
    }

    // Campaigns past their end time become expired.
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            UPDATE campaigns SET status = 'expired', updated_at = ?
            WHERE status IN ('active', 'scheduled')
              AND ends_at IS NOT NULL AND ends_at <> '' AND ends_at < ?;
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, now_iso.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, now_iso.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_DONE) changed += sqlite3_changes(db_);
            sqlite3_finalize(stmt);
        }
    }

    return changed;
}

std::vector<std::pair<Placement, Campaign>> Database::get_eligible_campaigns_for_delivery(const std::string& site_id, const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<Placement, Campaign>> results;

    // Join placement and campaigns where status='active' and placement status='active'
    // Order by placement_id, priority DESC, weight DESC.
    const char* sql = R"(
        SELECT
            p.id, p.site_id, p.placement_key, p.name, p.description, p.format,
            p.width, p.height, p.fallback_behavior, p.allowed_source_types,
            p.max_items, p.layout_config, p.disclosure_mode, p.status, p.created_at, p.updated_at,
            c.id, c.site_id, c.placement_id, c.name, c.destination_url, c.format,
            c.title, c.body, c.image_url, c.alt_text, c.cta_label, c.status,
            c.source_type, c.affiliate_offer_id, c.disclosure_override, c.starts_at,
            c.ends_at, c.priority, c.weight, c.created_at, c.updated_at, c.published_at
        FROM campaigns c
        JOIN placements p ON c.placement_id = p.id AND p.site_id = c.site_id
        WHERE c.site_id = ? AND c.status IN ('active', 'scheduled') AND p.status = 'active'
        ORDER BY p.id, c.priority DESC, c.weight DESC;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;
    sqlite3_bind_text(stmt, 1, site_id.c_str(), -1, SQLITE_STATIC);

    std::string last_placement_id = "";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Placement p = placement_from_row(stmt, 0);
        Campaign c = campaign_from_row(stmt, 16);

        // A campaign whose destination was tampered with at rest must never be delivered.
        if (!utils::is_safe_url(c.destination_url)) {
            continue;
        }
        if (c.format.empty()) c.format = p.format;
        if (c.source_type.empty()) c.source_type = "house";
        if (std::find(p.allowed_source_types.begin(), p.allowed_source_types.end(), c.source_type) ==
            p.allowed_source_types.end()) {
            continue;
        }

        // Check schedule eligibility
        if (!utils::is_campaign_active_schedule(c.starts_at, c.ends_at, now_iso)) {
            continue;
        }

        // MVP rule: one active ad per placement
        if (p.id == last_placement_id) {
            continue;
        }
        last_placement_id = p.id;
        results.push_back({p, c});
    }

    sqlite3_finalize(stmt);
    return results;
}

// Events
bool Database::record_event(const AdEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    // INSERT OR IGNORE for deduplication on event_id!
    const char* sql = R"(
        INSERT OR IGNORE INTO ad_events
        (id, site_id, placement_id, campaign_id, source_type, affiliate_offer_id,
         event_type, event_id, occurred_at, page_origin, referrer,
         user_agent_category, ip_hash)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string id = event.id.empty() ? utils::generate_uuid() : event.id;
    std::string now = event.occurred_at.empty() ? utils::get_current_iso8601() : event.occurred_at;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, event.site_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, event.placement_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, event.campaign_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, event.source_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, event.affiliate_offer_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, event.event_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, event.event_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, now.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, event.page_origin.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, event.referrer.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, event.user_agent_category.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, event.ip_hash.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Check if a row was actually inserted
    return (rc == SQLITE_DONE && sqlite3_changes(db_) > 0);
}

int Database::purge_events_older_than(int days) {
    if (days <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cutoff = utils::iso8601_days_ago(days);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM ad_events WHERE occurred_at < ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, cutoff.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? sqlite3_changes(db_) : 0;
}

// Analytics
namespace {
std::string normalize_range_start(const std::string& value) {
    if (value.size() == 10) return value + "T00:00:00Z";
    return utils::normalize_iso8601_utc(value);
}
std::string normalize_range_end(const std::string& value) {
    if (value.size() == 10) return value + "T23:59:59Z";
    return utils::normalize_iso8601_utc(value);
}
} // namespace

nlohmann::json Database::get_analytics_summary(const std::optional<std::string>& site_id,
                                             const std::optional<std::string>& placement_id,
                                             const std::optional<std::string>& campaign_id,
                                             const std::string& start_date,
                                             const std::string& end_date,
                                             const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string range_start = normalize_range_start(start_date);
    const std::string range_end = normalize_range_end(end_date);
    std::string sql = R"(
        SELECT
            COUNT(CASE WHEN event_type = 'impression' THEN 1 END) as impressions,
            COUNT(CASE WHEN event_type = 'click' THEN 1 END) as clicks,
            COUNT(CASE WHEN event_type = 'outbound_redirect' THEN 1 END) as outbound_redirects,
            COUNT(CASE WHEN event_type = 'click' AND
                (affiliate_offer_id IS NOT NULL AND affiliate_offer_id <> '') THEN 1 END) as affiliate_clicks
        FROM ad_events
        WHERE 1=1
    )";

    if (site_id.has_value()) sql += " AND site_id = ?";
    if (placement_id.has_value()) sql += " AND placement_id = ?";
    if (campaign_id.has_value()) sql += " AND campaign_id = ?";
    if (publisher_id.has_value()) {
        sql += " AND site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    }
    if (!range_start.empty()) sql += " AND occurred_at >= ?";
    if (!range_end.empty()) sql += " AND occurred_at <= ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {{"impressions", 0}, {"clicks", 0}, {"outbound_redirects", 0},
                {"affiliate_clicks", 0}, {"ctr", 0.0},
                {"confirmed_conversions", 0}, {"confirmed_commission", 0},
                {"provenance", {
                    {"impressions", "adcontrol_observed"},
                    {"clicks", "adcontrol_observed"},
                    {"outbound_redirects", "adcontrol_observed"},
                    {"conversions", "not_connected"},
                    {"commission", "not_connected"}
                }}};
    }

    int idx = 1;
    if (site_id.has_value()) sqlite3_bind_text(stmt, idx++, site_id->c_str(), -1, SQLITE_STATIC);
    if (placement_id.has_value()) sqlite3_bind_text(stmt, idx++, placement_id->c_str(), -1, SQLITE_STATIC);
    if (campaign_id.has_value()) sqlite3_bind_text(stmt, idx++, campaign_id->c_str(), -1, SQLITE_STATIC);
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);
    if (!range_start.empty()) sqlite3_bind_text(stmt, idx++, range_start.c_str(), -1, SQLITE_STATIC);
    if (!range_end.empty()) sqlite3_bind_text(stmt, idx++, range_end.c_str(), -1, SQLITE_STATIC);

    int64_t impressions = 0;
    int64_t clicks = 0;
    int64_t outbound_redirects = 0;
    int64_t affiliate_clicks = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        impressions = sqlite3_column_int64(stmt, 0);
        clicks = sqlite3_column_int64(stmt, 1);
        outbound_redirects = sqlite3_column_int64(stmt, 2);
        affiliate_clicks = sqlite3_column_int64(stmt, 3);
    }
    sqlite3_finalize(stmt);

    double ctr = 0.0;
    if (impressions > 0) {
        ctr = (static_cast<double>(clicks) / static_cast<double>(impressions)) * 100.0;
    }

    return {
        {"impressions", impressions},
        {"clicks", clicks},
        {"outbound_redirects", outbound_redirects},
        {"affiliate_clicks", affiliate_clicks},
        {"ctr", ctr},
        // Only AdControl-observed counts are returned. External conversion and
        // commission data is not connected in the MVP and must never be inferred
        // from redirect events.
        {"confirmed_conversions", 0},
        {"confirmed_commission", 0},
        {"provenance", {
            {"impressions", "adcontrol_observed"},
            {"clicks", "adcontrol_observed"},
            {"outbound_redirects", "adcontrol_observed"},
            {"conversions", "not_connected"},
            {"commission", "not_connected"}
        }}
    };
}

nlohmann::json Database::get_analytics_timeseries(const std::optional<std::string>& site_id,
                                                const std::optional<std::string>& placement_id,
                                                const std::optional<std::string>& campaign_id,
                                                int days,
                                                const std::string& start_date,
                                                const std::string& end_date,
                                                const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (days < 1) days = 1;
    if (days > 365) days = 365;

    std::string range_start = normalize_range_start(start_date);
    std::string range_end = normalize_range_end(end_date);
    if (range_start.empty()) range_start = utils::iso8601_days_ago(days);
    if (range_end.empty()) range_end = utils::get_current_iso8601();

    std::string sql = R"(
        SELECT
            substr(occurred_at, 1, 10) as day,
            COUNT(CASE WHEN event_type = 'impression' THEN 1 END) as impressions,
            COUNT(CASE WHEN event_type = 'click' THEN 1 END) as clicks,
            COUNT(CASE WHEN event_type = 'outbound_redirect' THEN 1 END) as outbound_redirects
        FROM ad_events
        WHERE occurred_at >= ? AND occurred_at <= ?
    )";

    if (site_id.has_value()) sql += " AND site_id = ?";
    if (placement_id.has_value()) sql += " AND placement_id = ?";
    if (campaign_id.has_value()) sql += " AND campaign_id = ?";
    if (publisher_id.has_value()) {
        sql += " AND site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    }
    sql += " GROUP BY day ORDER BY day ASC;";

    sqlite3_stmt* stmt = nullptr;
    nlohmann::json list = nlohmann::json::array();
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    int idx = 1;
    sqlite3_bind_text(stmt, idx++, range_start.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, idx++, range_end.c_str(), -1, SQLITE_STATIC);
    if (site_id.has_value()) sqlite3_bind_text(stmt, idx++, site_id->c_str(), -1, SQLITE_STATIC);
    if (placement_id.has_value()) sqlite3_bind_text(stmt, idx++, placement_id->c_str(), -1, SQLITE_STATIC);
    if (campaign_id.has_value()) sqlite3_bind_text(stmt, idx++, campaign_id->c_str(), -1, SQLITE_STATIC);
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* day_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int64_t imps = sqlite3_column_int64(stmt, 1);
        int64_t clks = sqlite3_column_int64(stmt, 2);
        int64_t outbound = sqlite3_column_int64(stmt, 3);
        double ctr = (imps > 0) ? (static_cast<double>(clks) / static_cast<double>(imps)) * 100.0 : 0.0;

        list.push_back({
            {"date", day_str ? day_str : ""},
            {"impressions", imps},
            {"clicks", clks},
            {"outbound_redirects", outbound},
            {"ctr", ctr}
        });
    }
    sqlite3_finalize(stmt);
    return list;
}

nlohmann::json Database::get_analytics_breakdown(const std::string& dimension,
                                                const std::optional<std::string>& site_id,
                                                const std::optional<std::string>& placement_id,
                                                const std::optional<std::string>& campaign_id,
                                                const std::string& start_date,
                                                const std::string& end_date,
                                                const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string select_key;
    std::string join_clause;
    std::string name_expr;
    if (dimension == "site") {
        select_key = "e.site_id";
        join_clause = "LEFT JOIN sites s ON s.id = e.site_id";
        name_expr = "COALESCE(s.name, e.site_id)";
    } else if (dimension == "placement") {
        select_key = "e.placement_id";
        join_clause = "LEFT JOIN placements p ON p.id = e.placement_id";
        name_expr = "COALESCE(p.name, e.placement_id)";
    } else if (dimension == "campaign") {
        select_key = "e.campaign_id";
        join_clause = "LEFT JOIN campaigns c ON c.id = e.campaign_id";
        name_expr = "COALESCE(c.name, e.campaign_id)";
    } else if (dimension == "source") {
        select_key = "COALESCE(NULLIF(e.source_type, ''), 'legacy')";
        name_expr = "COALESCE(NULLIF(e.source_type, ''), 'legacy')";
    } else {
        return nlohmann::json::array();
    }

    const std::string range_start = normalize_range_start(start_date);
    const std::string range_end = normalize_range_end(end_date);

    std::string sql = "SELECT " + select_key + " AS bucket, " + name_expr + " AS bucket_name, "
        "COUNT(CASE WHEN e.event_type = 'impression' THEN 1 END) AS impressions, "
        "COUNT(CASE WHEN e.event_type = 'click' THEN 1 END) AS clicks, "
        "COUNT(CASE WHEN e.event_type = 'outbound_redirect' THEN 1 END) AS outbound_redirects "
        "FROM ad_events e " + join_clause + " WHERE 1=1";

    if (site_id.has_value()) sql += " AND e.site_id = ?";
    if (placement_id.has_value()) sql += " AND e.placement_id = ?";
    if (campaign_id.has_value()) sql += " AND e.campaign_id = ?";
    if (publisher_id.has_value()) {
        sql += " AND e.site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    }
    if (!range_start.empty()) sql += " AND e.occurred_at >= ?";
    if (!range_end.empty()) sql += " AND e.occurred_at <= ?";
    sql += " GROUP BY bucket, bucket_name ORDER BY impressions DESC, clicks DESC LIMIT 200;";

    sqlite3_stmt* stmt = nullptr;
    nlohmann::json rows = nlohmann::json::array();
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return rows;
    }

    int idx = 1;
    if (site_id.has_value()) sqlite3_bind_text(stmt, idx++, site_id->c_str(), -1, SQLITE_STATIC);
    if (placement_id.has_value()) sqlite3_bind_text(stmt, idx++, placement_id->c_str(), -1, SQLITE_STATIC);
    if (campaign_id.has_value()) sqlite3_bind_text(stmt, idx++, campaign_id->c_str(), -1, SQLITE_STATIC);
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);
    if (!range_start.empty()) sqlite3_bind_text(stmt, idx++, range_start.c_str(), -1, SQLITE_STATIC);
    if (!range_end.empty()) sqlite3_bind_text(stmt, idx++, range_end.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int64_t imps = sqlite3_column_int64(stmt, 2);
        int64_t clks = sqlite3_column_int64(stmt, 3);
        int64_t outbound = sqlite3_column_int64(stmt, 4);
        double ctr = (imps > 0) ? (static_cast<double>(clks) / static_cast<double>(imps)) * 100.0 : 0.0;
        rows.push_back({
            {"key", key ? key : ""},
            {"name", name ? name : ""},
            {"impressions", imps},
            {"clicks", clks},
            {"outbound_redirects", outbound},
            {"ctr", ctr}
        });
    }
    sqlite3_finalize(stmt);
    return rows;
}

std::vector<AdEvent> Database::get_events_export(const std::optional<std::string>& site_id,
                                                const std::optional<std::string>& placement_id,
                                                const std::optional<std::string>& campaign_id,
                                                const std::string& start_date,
                                                const std::string& end_date,
                                                const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AdEvent> list;
    const std::string range_start = normalize_range_start(start_date);
    const std::string range_end = normalize_range_end(end_date);
    std::string sql = "SELECT id, site_id, placement_id, campaign_id, COALESCE(source_type, ''), COALESCE(affiliate_offer_id, ''), event_type, event_id, occurred_at, page_origin, referrer, user_agent_category FROM ad_events WHERE 1=1";

    if (site_id.has_value()) sql += " AND site_id = ?";
    if (placement_id.has_value()) sql += " AND placement_id = ?";
    if (campaign_id.has_value()) sql += " AND campaign_id = ?";
    if (publisher_id.has_value()) {
        sql += " AND site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    }
    if (!range_start.empty()) sql += " AND occurred_at >= ?";
    if (!range_end.empty()) sql += " AND occurred_at <= ?";
    sql += " ORDER BY occurred_at DESC LIMIT 5000;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return list;

    int idx = 1;
    if (site_id.has_value()) sqlite3_bind_text(stmt, idx++, site_id->c_str(), -1, SQLITE_STATIC);
    if (placement_id.has_value()) sqlite3_bind_text(stmt, idx++, placement_id->c_str(), -1, SQLITE_STATIC);
    if (campaign_id.has_value()) sqlite3_bind_text(stmt, idx++, campaign_id->c_str(), -1, SQLITE_STATIC);
    if (publisher_id.has_value()) sqlite3_bind_text(stmt, idx++, publisher_id->c_str(), -1, SQLITE_STATIC);
    if (!range_start.empty()) sqlite3_bind_text(stmt, idx++, range_start.c_str(), -1, SQLITE_STATIC);
    if (!range_end.empty()) sqlite3_bind_text(stmt, idx++, range_end.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AdEvent e;
        e.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.site_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (p) e.placement_id = p;
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (c) e.campaign_id = c;
        const char* source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (source) e.source_type = source;
        const char* offer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (offer) e.affiliate_offer_id = offer;
        e.event_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        e.event_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        e.occurred_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        const char* po = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (po) e.page_origin = po;
        const char* ref = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        if (ref) e.referrer = ref;
        const char* ua = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        if (ua) e.user_agent_category = ua;
        list.push_back(e);
    }
    sqlite3_finalize(stmt);
    return list;
}

// Audit Logs
bool Database::record_audit_log(const AuditLog& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO audit_logs (id, actor, action, entity_type, entity_id, details, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string id = log.id.empty() ? utils::generate_uuid() : log.id;
    std::string now = log.created_at.empty() ? utils::get_current_iso8601() : log.created_at;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, log.actor.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, log.action.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, log.entity_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, log.entity_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, log.details.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::vector<AuditLog> Database::list_audit_logs(int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuditLog> list;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, actor, action, entity_type, entity_id, details, created_at FROM audit_logs ORDER BY created_at DESC LIMIT ? OFFSET ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AuditLog a;
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.actor = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.action = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.entity_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        a.entity_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const char* det = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (det) a.details = det;
        a.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        list.push_back(a);
    }
    sqlite3_finalize(stmt);
    return list;
}

} // namespace adcontrol
