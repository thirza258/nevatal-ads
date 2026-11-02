#include "db.hpp"
#include "utils.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace adcontrol {

namespace {

std::string text_col(sqlite3_stmt* stmt, int index) {
    const unsigned char* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

bool prepare(sqlite3* db, const std::string& sql, sqlite3_stmt** stmt) {
    return sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr) == SQLITE_OK;
}

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void finalize(sqlite3_stmt* stmt) {
    if (stmt) sqlite3_finalize(stmt);
}

std::string range_start(const std::string& value) {
    if (value.empty()) return "";
    if (value.size() == 10) return !utils::normalize_iso8601_utc(value + "T00:00:00Z").empty()
        ? value + "T00:00:00Z" : "";
    return utils::normalize_iso8601_utc(value);
}

std::string range_end(const std::string& value) {
    if (value.empty()) return "";
    if (value.size() == 10) return !utils::normalize_iso8601_utc(value + "T00:00:00Z").empty()
        ? value + "T23:59:59Z" : "";
    return utils::normalize_iso8601_utc(value);
}

ConversionSource source_from_row(sqlite3_stmt* stmt) {
    ConversionSource source;
    source.id = text_col(stmt, 0);
    source.site_id = text_col(stmt, 1);
    source.publisher_id = text_col(stmt, 2);
    source.name = text_col(stmt, 3);
    source.public_key = text_col(stmt, 4);
    source.encrypted_secret = text_col(stmt, 5);
    source.default_campaign_id = text_col(stmt, 6);
    source.default_offer_id = text_col(stmt, 7);
    source.status = text_col(stmt, 8);
    source.last_event_at = text_col(stmt, 9);
    source.created_at = text_col(stmt, 10);
    source.updated_at = text_col(stmt, 11);
    return source;
}

const char* SOURCE_SELECT =
    "SELECT id, site_id, COALESCE(publisher_id, ''), name, public_key, "
    "signing_secret_enc, COALESCE(default_campaign_id, ''), COALESCE(default_offer_id, ''), "
    "status, COALESCE(last_event_at, ''), created_at, updated_at FROM conversion_sources ";

ConversionEvent event_from_row(sqlite3_stmt* stmt) {
    ConversionEvent event;
    int i = 0;
    event.id = text_col(stmt, i++);
    event.source_id = text_col(stmt, i++);
    event.site_id = text_col(stmt, i++);
    event.publisher_id = text_col(stmt, i++);
    event.campaign_id = text_col(stmt, i++);
    event.offer_id = text_col(stmt, i++);
    event.external_id = text_col(stmt, i++);
    event.event_name = text_col(stmt, i++);
    event.order_id = text_col(stmt, i++);
    event.value_cents = sqlite3_column_int(stmt, i++);
    event.commission_cents = sqlite3_column_int(stmt, i++);
    event.currency = text_col(stmt, i++);
    event.status = text_col(stmt, i++);
    event.ingestion = text_col(stmt, i++);
    event.visitor_id = text_col(stmt, i++);
    event.page_url = text_col(stmt, i++);
    event.referrer = text_col(stmt, i++);
    event.utm_source = text_col(stmt, i++);
    event.utm_medium = text_col(stmt, i++);
    event.utm_campaign = text_col(stmt, i++);
    event.utm_term = text_col(stmt, i++);
    event.utm_content = text_col(stmt, i++);
    event.first_touch = text_col(stmt, i++);
    event.last_touch = text_col(stmt, i++);
    event.clicked_campaign_id = text_col(stmt, i++);
    event.occurred_at = text_col(stmt, i++);
    event.reconciled_at = text_col(stmt, i++);
    event.notes = text_col(stmt, i++);
    event.created_at = text_col(stmt, i++);
    event.updated_at = text_col(stmt, i++);
    return event;
}

const char* EVENT_COLUMNS =
    "id, source_id, site_id, COALESCE(publisher_id, ''), COALESCE(campaign_id, ''), "
    "COALESCE(offer_id, ''), external_id, event_name, order_id, value_cents, "
    "commission_cents, currency, status, ingestion, visitor_id, page_url, referrer, "
    "utm_source, utm_medium, utm_campaign, utm_term, utm_content, first_touch, "
    "last_touch, clicked_campaign_id, occurred_at, COALESCE(reconciled_at, ''), notes, "
    "created_at, updated_at";

std::string conversion_where(const std::optional<std::string>& publisher_id,
                             const std::optional<std::string>& site_id,
                             const std::optional<std::string>& campaign_id,
                             const std::optional<std::string>& status,
                             const std::string& start,
                             const std::string& end) {
    std::string where = " WHERE 1=1";
    if (publisher_id.has_value()) where += " AND publisher_id = ?";
    if (site_id.has_value()) where += " AND site_id = ?";
    if (campaign_id.has_value()) where += " AND campaign_id = ?";
    if (status.has_value()) where += " AND status = ?";
    if (!start.empty()) where += " AND occurred_at >= ?";
    if (!end.empty()) where += " AND occurred_at <= ?";
    return where;
}

int bind_conversion_where(sqlite3_stmt* stmt, int index,
                          const std::optional<std::string>& publisher_id,
                          const std::optional<std::string>& site_id,
                          const std::optional<std::string>& campaign_id,
                          const std::optional<std::string>& status,
                          const std::string& start,
                          const std::string& end) {
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (campaign_id.has_value()) bind_text(stmt, index++, *campaign_id);
    if (status.has_value()) bind_text(stmt, index++, *status);
    if (!start.empty()) bind_text(stmt, index++, start);
    if (!end.empty()) bind_text(stmt, index++, end);
    return index;
}

} // namespace

std::vector<ConversionSource> Database::list_conversion_sources(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConversionSource> sources;
    std::string sql = std::string(SOURCE_SELECT) + "WHERE 1=1";
    if (publisher_id.has_value()) sql += " AND publisher_id = ?";
    if (site_id.has_value()) sql += " AND site_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC, id DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return sources;
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (status.has_value()) bind_text(stmt, index++, *status);
    while (sqlite3_step(stmt) == SQLITE_ROW) sources.push_back(source_from_row(stmt));
    finalize(stmt);
    return sources;
}

std::optional<ConversionSource> Database::get_conversion_source(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string(SOURCE_SELECT) + "WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<ConversionSource> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = source_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<ConversionSource> Database::get_conversion_source_by_public_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string(SOURCE_SELECT) + "WHERE public_key = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, key);
    std::optional<ConversionSource> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = source_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_conversion_source(ConversionSource& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (source.id.empty()) source.id = utils::generate_uuid();
    if (source.public_key.empty()) source.public_key = "csrc_" + utils::random_hex(18);
    if (source.status.empty()) source.status = "active";
    const std::string now = utils::get_current_iso8601();
    source.created_at = now;
    source.updated_at = now;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO conversion_sources
            (id, site_id, publisher_id, name, public_key, signing_secret_enc,
             default_campaign_id, default_offer_id, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, source.id);
    bind_text(stmt, 2, source.site_id);
    if (source.publisher_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, source.publisher_id);
    bind_text(stmt, 4, source.name);
    bind_text(stmt, 5, source.public_key);
    bind_text(stmt, 6, source.encrypted_secret);
    bind_text(stmt, 7, source.default_campaign_id);
    bind_text(stmt, 8, source.default_offer_id);
    bind_text(stmt, 9, source.status);
    bind_text(stmt, 10, source.created_at);
    bind_text(stmt, 11, source.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    return ok;
}

bool Database::update_conversion_source(const ConversionSource& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE conversion_sources
        SET name = ?, default_campaign_id = ?, default_offer_id = ?, status = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, source.name);
    bind_text(stmt, 2, source.default_campaign_id);
    bind_text(stmt, 3, source.default_offer_id);
    bind_text(stmt, 4, source.status);
    bind_text(stmt, 5, utils::get_current_iso8601());
    bind_text(stmt, 6, source.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::record_conversion(ConversionEvent& event, bool& inserted, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    inserted = false;
    error.clear();
    if (event.id.empty()) event.id = utils::generate_uuid();
    if (event.event_name.empty()) event.event_name = "purchase";
    if (event.currency.empty()) event.currency = "USD";
    if (event.status.empty()) event.status = "pending";
    if (event.ingestion.empty()) event.ingestion = "sdk";
    if (event.occurred_at.empty()) event.occurred_at = utils::get_current_iso8601();
    event.created_at = utils::get_current_iso8601();
    event.updated_at = event.created_at;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO conversion_events
            (id, source_id, site_id, publisher_id, campaign_id, offer_id, external_id,
             event_name, order_id, value_cents, commission_cents, currency, status,
             ingestion, visitor_id, page_url, referrer, utm_source, utm_medium,
             utm_campaign, utm_term, utm_content, first_touch, last_touch,
             clicked_campaign_id, occurred_at, notes, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "conversion insert failed";
        return false;
    }
    int i = 1;
    bind_text(stmt, i++, event.id);
    bind_text(stmt, i++, event.source_id);
    bind_text(stmt, i++, event.site_id);
    bind_text(stmt, i++, event.publisher_id);
    bind_text(stmt, i++, event.campaign_id);
    bind_text(stmt, i++, event.offer_id);
    bind_text(stmt, i++, event.external_id);
    bind_text(stmt, i++, event.event_name);
    bind_text(stmt, i++, event.order_id);
    sqlite3_bind_int(stmt, i++, event.value_cents);
    sqlite3_bind_int(stmt, i++, event.commission_cents);
    bind_text(stmt, i++, event.currency);
    bind_text(stmt, i++, event.status);
    bind_text(stmt, i++, event.ingestion);
    bind_text(stmt, i++, event.visitor_id);
    bind_text(stmt, i++, event.page_url);
    bind_text(stmt, i++, event.referrer);
    bind_text(stmt, i++, event.utm_source);
    bind_text(stmt, i++, event.utm_medium);
    bind_text(stmt, i++, event.utm_campaign);
    bind_text(stmt, i++, event.utm_term);
    bind_text(stmt, i++, event.utm_content);
    bind_text(stmt, i++, event.first_touch);
    bind_text(stmt, i++, event.last_touch);
    bind_text(stmt, i++, event.clicked_campaign_id);
    bind_text(stmt, i++, event.occurred_at);
    bind_text(stmt, i++, event.notes);
    bind_text(stmt, i++, event.created_at);
    bind_text(stmt, i, event.updated_at);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "conversion insert failed";
        return false;
    }
    if (sqlite3_changes(db_) == 0) {
        // Idempotent replay: return the existing row identity.
        sqlite3_stmt* existing = nullptr;
        const char* lookup = "SELECT id FROM conversion_events WHERE source_id = ? AND external_id = ? LIMIT 1;";
        if (prepare(db_, lookup, &existing)) {
            bind_text(existing, 1, event.source_id);
            bind_text(existing, 2, event.external_id);
            if (sqlite3_step(existing) == SQLITE_ROW) event.id = text_col(existing, 0);
            finalize(existing);
        }
        return true;
    }

    sqlite3_stmt* touch = nullptr;
    if (prepare(db_, "UPDATE conversion_sources SET last_event_at = ? WHERE id = ?;", &touch)) {
        bind_text(touch, 1, event.occurred_at);
        bind_text(touch, 2, event.source_id);
        sqlite3_step(touch);
        finalize(touch);
    }
    inserted = true;
    return true;
}

std::optional<ConversionEvent> Database::get_conversion_event(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + EVENT_COLUMNS +
        " FROM conversion_events WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<ConversionEvent> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = event_from_row(stmt);
    finalize(stmt);
    return result;
}

std::vector<ConversionEvent> Database::list_conversion_events(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& campaign_id,
    const std::optional<std::string>& status,
    const std::string& start_date,
    const std::string& end_date,
    int limit,
    int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConversionEvent> events;
    const std::string start = range_start(start_date);
    const std::string end = range_end(end_date);
    std::string sql = std::string("SELECT ") + EVENT_COLUMNS + " FROM conversion_events" +
        conversion_where(publisher_id, site_id, campaign_id, status, start, end) +
        " ORDER BY occurred_at DESC, id DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return events;
    int index = bind_conversion_where(stmt, 1, publisher_id, site_id, campaign_id, status, start, end);
    sqlite3_bind_int(stmt, index++, std::max(1, std::min(limit, 500)));
    sqlite3_bind_int(stmt, index, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) events.push_back(event_from_row(stmt));
    finalize(stmt);
    return events;
}

bool Database::update_conversion_reconciliation(const std::string& id,
                                                 const std::string& status,
                                                 const std::string& notes) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE conversion_events
        SET status = ?, notes = ?, reconciled_at = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    const std::string now = utils::get_current_iso8601();
    const bool terminal = status == "approved" || status == "reconciled" || status == "rejected";
    bind_text(stmt, 1, status);
    bind_text(stmt, 2, notes);
    if (terminal) bind_text(stmt, 3, now);
    else sqlite3_bind_null(stmt, 3);
    bind_text(stmt, 4, now);
    bind_text(stmt, 5, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

nlohmann::json Database::get_conversion_summary(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& campaign_id,
    const std::string& start_date,
    const std::string& end_date) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string start = range_start(start_date);
    const std::string end = range_end(end_date);

    long long total = 0, pending = 0, approved = 0, rejected = 0, reconciled = 0;
    long long gross_value = 0, commission = 0, approved_value = 0, approved_commission = 0;

    std::string sql = R"(
        SELECT COUNT(*),
               COUNT(CASE WHEN status = 'pending' THEN 1 END),
               COUNT(CASE WHEN status = 'approved' THEN 1 END),
               COUNT(CASE WHEN status = 'rejected' THEN 1 END),
               COUNT(CASE WHEN status = 'reconciled' THEN 1 END),
               COALESCE(SUM(value_cents), 0),
               COALESCE(SUM(commission_cents), 0),
               COALESCE(SUM(CASE WHEN status IN ('approved','reconciled') THEN value_cents ELSE 0 END), 0),
               COALESCE(SUM(CASE WHEN status IN ('approved','reconciled') THEN commission_cents ELSE 0 END), 0)
        FROM conversion_events
    )" + conversion_where(publisher_id, site_id, campaign_id, std::nullopt, start, end) + ";";

    sqlite3_stmt* stmt = nullptr;
    if (prepare(db_, sql, &stmt)) {
        bind_conversion_where(stmt, 1, publisher_id, site_id, campaign_id, std::nullopt, start, end);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = sqlite3_column_int64(stmt, 0);
            pending = sqlite3_column_int64(stmt, 1);
            approved = sqlite3_column_int64(stmt, 2);
            rejected = sqlite3_column_int64(stmt, 3);
            reconciled = sqlite3_column_int64(stmt, 4);
            gross_value = sqlite3_column_int64(stmt, 5);
            commission = sqlite3_column_int64(stmt, 6);
            approved_value = sqlite3_column_int64(stmt, 7);
            approved_commission = sqlite3_column_int64(stmt, 8);
        }
        finalize(stmt);
    }

    long long impressions = 0, clicks = 0;
    std::string ad_sql = R"(
        SELECT COUNT(CASE WHEN event_type = 'impression' THEN 1 END),
               COUNT(CASE WHEN event_type = 'click' THEN 1 END)
        FROM ad_events WHERE 1=1
    )";
    if (site_id.has_value()) ad_sql += " AND site_id = ?";
    if (campaign_id.has_value()) ad_sql += " AND campaign_id = ?";
    if (publisher_id.has_value()) ad_sql += " AND site_id IN (SELECT id FROM sites WHERE publisher_id = ?)";
    if (!start.empty()) ad_sql += " AND occurred_at >= ?";
    if (!end.empty()) ad_sql += " AND occurred_at <= ?";
    stmt = nullptr;
    if (prepare(db_, ad_sql, &stmt)) {
        int index = 1;
        if (site_id.has_value()) bind_text(stmt, index++, *site_id);
        if (campaign_id.has_value()) bind_text(stmt, index++, *campaign_id);
        if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
        if (!start.empty()) bind_text(stmt, index++, start);
        if (!end.empty()) bind_text(stmt, index++, end);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            impressions = sqlite3_column_int64(stmt, 0);
            clicks = sqlite3_column_int64(stmt, 1);
        }
        finalize(stmt);
    }

    const double ctr = impressions > 0 ? (static_cast<double>(clicks) / impressions) * 100.0 : 0.0;
    const double conversion_rate = clicks > 0 ? (static_cast<double>(total) / clicks) * 100.0 : 0.0;

    return {
        {"conversions", total},
        {"pending_conversions", pending},
        {"approved_conversions", approved},
        {"rejected_conversions", rejected},
        {"reconciled_conversions", reconciled},
        {"gross_value_cents", gross_value},
        {"commission_cents", commission},
        {"approved_value_cents", approved_value},
        {"approved_commission_cents", approved_commission},
        {"impressions", impressions},
        {"clicks", clicks},
        {"ctr", ctr},
        {"conversion_rate", conversion_rate},
        {"funnel", nlohmann::json::array({
            {{"stage", "impressions"}, {"count", impressions}},
            {{"stage", "clicks"}, {"count", clicks}},
            {{"stage", "conversions"}, {"count", total}},
            {{"stage", "approved"}, {"count", approved + reconciled}}
        })},
        {"provenance", {
            {"impressions", "adcontrol_observed"},
            {"clicks", "adcontrol_observed"},
            {"conversions", "first_party_pixel_sdk_or_signed_webhook"},
            {"commission", "reported_by_conversion_source"}
        }}
    };
}

nlohmann::json Database::get_conversion_reconciliation(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id,
    const std::string& start_date,
    const std::string& end_date) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string start = range_start(start_date);
    const std::string end = range_end(end_date);

    std::string sql = R"(
        SELECT COALESCE(ce.offer_id, ''), COALESCE(ce.campaign_id, ''), ce.status,
               COUNT(*), COALESCE(SUM(ce.value_cents), 0),
               COALESCE(SUM(ce.commission_cents), 0),
               COALESCE(MAX(o.product_title), ''), COALESCE(MAX(c.name), '')
        FROM conversion_events ce
        LEFT JOIN affiliate_offers o ON ce.offer_id = o.id
        LEFT JOIN campaigns c ON ce.campaign_id = c.id
        WHERE 1=1
    )";
    if (publisher_id.has_value()) sql += " AND ce.publisher_id = ?";
    if (site_id.has_value()) sql += " AND ce.site_id = ?";
    if (!start.empty()) sql += " AND ce.occurred_at >= ?";
    if (!end.empty()) sql += " AND ce.occurred_at <= ?";
    sql += " GROUP BY ce.offer_id, ce.campaign_id, ce.status ORDER BY ce.offer_id, ce.campaign_id, ce.status;";

    nlohmann::json rows = nlohmann::json::array();
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return {{"rows", rows}};
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (!start.empty()) bind_text(stmt, index++, start);
    if (!end.empty()) bind_text(stmt, index, end);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rows.push_back({
            {"offer_id", text_col(stmt, 0)},
            {"campaign_id", text_col(stmt, 1)},
            {"status", text_col(stmt, 2)},
            {"conversions", sqlite3_column_int64(stmt, 3)},
            {"value_cents", sqlite3_column_int64(stmt, 4)},
            {"commission_cents", sqlite3_column_int64(stmt, 5)},
            {"offer_title", text_col(stmt, 6)},
            {"campaign_name", text_col(stmt, 7)}
        });
    }
    finalize(stmt);
    return {{"rows", rows}};
}

} // namespace adcontrol
