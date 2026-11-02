#include "db.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
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

AudienceSegment segment_from_row(sqlite3_stmt* stmt) {
    AudienceSegment segment;
    segment.id = text_col(stmt, 0);
    segment.site_id = text_col(stmt, 1);
    segment.publisher_id = text_col(stmt, 2);
    segment.name = text_col(stmt, 3);
    segment.description = text_col(stmt, 4);
    segment.rules = text_col(stmt, 5);
    segment.status = text_col(stmt, 6);
    segment.created_at = text_col(stmt, 7);
    segment.updated_at = text_col(stmt, 8);
    return segment;
}

const char* SEGMENT_COLUMNS =
    "id, site_id, COALESCE(publisher_id, ''), name, description, rules, status, "
    "created_at, updated_at";

ExperimentVariant variant_from_row(sqlite3_stmt* stmt) {
    ExperimentVariant variant;
    variant.id = text_col(stmt, 0);
    variant.experiment_id = text_col(stmt, 1);
    variant.campaign_id = text_col(stmt, 2);
    variant.name = text_col(stmt, 3);
    variant.weight = sqlite3_column_int(stmt, 4);
    variant.is_control = sqlite3_column_int(stmt, 5) != 0;
    variant.created_at = text_col(stmt, 6);
    return variant;
}

const char* VARIANT_COLUMNS =
    "id, experiment_id, campaign_id, name, weight, is_control, created_at";

Experiment experiment_from_row(sqlite3_stmt* stmt) {
    Experiment experiment;
    experiment.id = text_col(stmt, 0);
    experiment.site_id = text_col(stmt, 1);
    experiment.placement_id = text_col(stmt, 2);
    experiment.name = text_col(stmt, 3);
    experiment.description = text_col(stmt, 4);
    experiment.status = text_col(stmt, 5);
    experiment.objective = text_col(stmt, 6);
    experiment.min_impressions = sqlite3_column_int(stmt, 7);
    experiment.min_lift_percent = sqlite3_column_int(stmt, 8);
    experiment.auto_winner = sqlite3_column_int(stmt, 9) != 0;
    experiment.winner_campaign_id = text_col(stmt, 10);
    experiment.started_at = text_col(stmt, 11);
    experiment.ended_at = text_col(stmt, 12);
    experiment.created_at = text_col(stmt, 13);
    experiment.updated_at = text_col(stmt, 14);
    return experiment;
}

const char* EXPERIMENT_COLUMNS =
    "id, site_id, placement_id, name, description, status, objective, "
    "min_impressions, min_lift_percent, auto_winner, COALESCE(winner_campaign_id, ''), "
    "COALESCE(started_at, ''), COALESCE(ended_at, ''), created_at, updated_at";

std::vector<ExperimentVariant> load_variants_locked(sqlite3* db, const std::string& experiment_id) {
    std::vector<ExperimentVariant> variants;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + VARIANT_COLUMNS +
        " FROM experiment_variants WHERE experiment_id = ? ORDER BY created_at, id;";
    if (!prepare(db, sql, &stmt)) return variants;
    bind_text(stmt, 1, experiment_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) variants.push_back(variant_from_row(stmt));
    finalize(stmt);
    return variants;
}

std::optional<Experiment> experiment_by_id_locked(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + EXPERIMENT_COLUMNS +
        " FROM experiments WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<Experiment> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = experiment_from_row(stmt);
        result->variants = load_variants_locked(db, id);
    }
    finalize(stmt);
    return result;
}

struct VariantMetrics {
    std::string campaign_id;
    std::string campaign_name;
    std::string campaign_status;
    long long impressions = 0;
    long long clicks = 0;
    long long conversions = 0;
    long long value_cents = 0;
    long long commission_cents = 0;
    double ctr = 0.0;
    double conversion_rate = 0.0;
    double revenue_per_mille = 0.0;
};

VariantMetrics metrics_for_campaign_locked(sqlite3* db, const std::string& campaign_id,
                                           const std::string& start_iso,
                                           const std::string& end_iso) {
    VariantMetrics metrics;
    metrics.campaign_id = campaign_id;
    sqlite3_stmt* stmt = nullptr;
    const char* campaign_sql = "SELECT name, status FROM campaigns WHERE id = ? LIMIT 1;";
    if (prepare(db, campaign_sql, &stmt)) {
        bind_text(stmt, 1, campaign_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            metrics.campaign_name = text_col(stmt, 0);
            metrics.campaign_status = text_col(stmt, 1);
        }
        finalize(stmt);
    }
    const char* event_sql = R"(
        SELECT COUNT(CASE WHEN event_type = 'impression' THEN 1 END),
               COUNT(CASE WHEN event_type = 'click' THEN 1 END)
        FROM ad_events WHERE campaign_id = ? AND occurred_at >= ? AND occurred_at <= ?;
    )";
    stmt = nullptr;
    if (prepare(db, event_sql, &stmt)) {
        bind_text(stmt, 1, campaign_id);
        bind_text(stmt, 2, start_iso);
        bind_text(stmt, 3, end_iso);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            metrics.impressions = sqlite3_column_int64(stmt, 0);
            metrics.clicks = sqlite3_column_int64(stmt, 1);
        }
        finalize(stmt);
    }
    const char* conversion_sql = R"(
        SELECT COUNT(*), COALESCE(SUM(value_cents), 0), COALESCE(SUM(commission_cents), 0)
        FROM conversion_events WHERE campaign_id = ? AND occurred_at >= ? AND occurred_at <= ?;
    )";
    stmt = nullptr;
    if (prepare(db, conversion_sql, &stmt)) {
        bind_text(stmt, 1, campaign_id);
        bind_text(stmt, 2, start_iso);
        bind_text(stmt, 3, end_iso);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            metrics.conversions = sqlite3_column_int64(stmt, 0);
            metrics.value_cents = sqlite3_column_int64(stmt, 1);
            metrics.commission_cents = sqlite3_column_int64(stmt, 2);
        }
        finalize(stmt);
    }
    metrics.ctr = metrics.impressions > 0
        ? (static_cast<double>(metrics.clicks) / metrics.impressions) * 100.0 : 0.0;
    metrics.conversion_rate = metrics.clicks > 0
        ? (static_cast<double>(metrics.conversions) / metrics.clicks) * 100.0 : 0.0;
    metrics.revenue_per_mille = metrics.impressions > 0
        ? (static_cast<double>(metrics.value_cents) * 1000.0 / metrics.impressions) : 0.0;
    return metrics;
}

double objective_score(const VariantMetrics& metrics, const std::string& objective) {
    if (objective == "conversion_rate") return metrics.conversion_rate;
    if (objective == "revenue") return metrics.revenue_per_mille;
    return metrics.ctr;
}

bool update_variant_weights_locked(sqlite3* db, const std::string& experiment_id,
                                   const std::vector<ExperimentVariant>& variants) {
    for (const auto& variant : variants) {
        sqlite3_stmt* stmt = nullptr;
        if (!prepare(db, "UPDATE experiment_variants SET weight = ? WHERE id = ?;", &stmt)) return false;
        sqlite3_bind_int(stmt, 1, variant.weight);
        bind_text(stmt, 2, variant.id);
        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
        if (!ok) return false;
    }
    (void)experiment_id;
    return true;
}

} // namespace

std::vector<AudienceSegment> Database::list_audience_segments(
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AudienceSegment> segments;
    std::string sql = std::string("SELECT ") + SEGMENT_COLUMNS + " FROM audience_segments WHERE 1=1";
    if (site_id.has_value()) sql += " AND site_id = ?";
    if (publisher_id.has_value()) sql += " AND publisher_id = ?";
    sql += " ORDER BY created_at DESC, id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return segments;
    int index = 1;
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) segments.push_back(segment_from_row(stmt));
    finalize(stmt);
    return segments;
}

std::optional<AudienceSegment> Database::get_audience_segment(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + SEGMENT_COLUMNS +
        " FROM audience_segments WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<AudienceSegment> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = segment_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_audience_segment(AudienceSegment& segment, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (segment.id.empty()) segment.id = utils::generate_uuid();
    if (segment.status.empty()) segment.status = "active";
    if (segment.rules.empty()) segment.rules = "{}";
    const std::string now = utils::get_current_iso8601();
    segment.created_at = now;
    segment.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO audience_segments
            (id, site_id, publisher_id, name, description, rules, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create audience segment";
        return false;
    }
    bind_text(stmt, 1, segment.id);
    bind_text(stmt, 2, segment.site_id);
    if (segment.publisher_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, segment.publisher_id);
    bind_text(stmt, 4, segment.name);
    bind_text(stmt, 5, segment.description);
    bind_text(stmt, 6, segment.rules);
    bind_text(stmt, 7, segment.status);
    bind_text(stmt, 8, segment.created_at);
    bind_text(stmt, 9, segment.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create audience segment";
    return ok;
}

bool Database::update_audience_segment(const AudienceSegment& segment) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE audience_segments SET name = ?, description = ?, rules = ?, status = ?,
            updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, segment.name);
    bind_text(stmt, 2, segment.description);
    bind_text(stmt, 3, segment.rules);
    bind_text(stmt, 4, segment.status);
    bind_text(stmt, 5, utils::get_current_iso8601());
    bind_text(stmt, 6, segment.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_audience_segment(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    bool ok = prepare(db_, "UPDATE campaigns SET audience_segment_id = '' WHERE audience_segment_id = ?;", &stmt);
    if (ok) {
        bind_text(stmt, 1, id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
    }
    if (ok) {
        ok = prepare(db_, "DELETE FROM audience_segments WHERE id = ?;", &stmt);
        if (ok) {
            bind_text(stmt, 1, id);
            ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
            finalize(stmt);
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<Experiment> Database::list_experiments(
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& placement_id,
    const std::optional<std::string>& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Experiment> experiments;
    std::string sql = std::string("SELECT ") + EXPERIMENT_COLUMNS + " FROM experiments WHERE 1=1";
    if (site_id.has_value()) sql += " AND site_id = ?";
    if (placement_id.has_value()) sql += " AND placement_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC, id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return experiments;
    int index = 1;
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (placement_id.has_value()) bind_text(stmt, index++, *placement_id);
    if (status.has_value()) bind_text(stmt, index++, *status);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Experiment experiment = experiment_from_row(stmt);
        experiment.variants = load_variants_locked(db_, experiment.id);
        experiments.push_back(experiment);
    }
    finalize(stmt);
    return experiments;
}

std::optional<Experiment> Database::get_experiment(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return experiment_by_id_locked(db_, id);
}

std::optional<Experiment> Database::get_running_experiment_for_placement(const std::string& placement_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + EXPERIMENT_COLUMNS +
        " FROM experiments WHERE placement_id = ? AND status = 'running' LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, placement_id);
    std::optional<Experiment> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = experiment_from_row(stmt);
        result->variants = load_variants_locked(db_, result->id);
    }
    finalize(stmt);
    return result;
}

bool Database::create_experiment(Experiment& experiment, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (experiment.id.empty()) experiment.id = utils::generate_uuid();
    if (experiment.status.empty()) experiment.status = "draft";
    if (experiment.objective.empty()) experiment.objective = "ctr";
    const std::string now = utils::get_current_iso8601();
    experiment.created_at = now;
    experiment.updated_at = now;
    if (experiment.status == "running" && experiment.started_at.empty()) experiment.started_at = now;

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO experiments
            (id, site_id, placement_id, name, description, status, objective,
             min_impressions, min_lift_percent, auto_winner, winner_campaign_id,
             started_at, ended_at, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    bool ok = prepare(db_, sql, &stmt);
    if (ok) {
        bind_text(stmt, 1, experiment.id);
        bind_text(stmt, 2, experiment.site_id);
        bind_text(stmt, 3, experiment.placement_id);
        bind_text(stmt, 4, experiment.name);
        bind_text(stmt, 5, experiment.description);
        bind_text(stmt, 6, experiment.status);
        bind_text(stmt, 7, experiment.objective);
        sqlite3_bind_int(stmt, 8, experiment.min_impressions);
        sqlite3_bind_int(stmt, 9, experiment.min_lift_percent);
        sqlite3_bind_int(stmt, 10, experiment.auto_winner ? 1 : 0);
        if (experiment.winner_campaign_id.empty()) sqlite3_bind_null(stmt, 11);
        else bind_text(stmt, 11, experiment.winner_campaign_id);
        if (experiment.started_at.empty()) sqlite3_bind_null(stmt, 12);
        else bind_text(stmt, 12, experiment.started_at);
        if (experiment.ended_at.empty()) sqlite3_bind_null(stmt, 13);
        else bind_text(stmt, 13, experiment.ended_at);
        bind_text(stmt, 14, experiment.created_at);
        bind_text(stmt, 15, experiment.updated_at);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
    }
    if (ok) {
        for (auto& variant : experiment.variants) {
            if (variant.id.empty()) variant.id = utils::generate_uuid();
            variant.experiment_id = experiment.id;
            variant.created_at = now;
            const char* variant_sql = R"(
                INSERT INTO experiment_variants
                    (id, experiment_id, campaign_id, name, weight, is_control, created_at)
                VALUES (?, ?, ?, ?, ?, ?, ?);
            )";
            ok = prepare(db_, variant_sql, &stmt);
            if (!ok) break;
            bind_text(stmt, 1, variant.id);
            bind_text(stmt, 2, variant.experiment_id);
            bind_text(stmt, 3, variant.campaign_id);
            bind_text(stmt, 4, variant.name);
            sqlite3_bind_int(stmt, 5, variant.weight);
            sqlite3_bind_int(stmt, 6, variant.is_control ? 1 : 0);
            bind_text(stmt, 7, variant.created_at);
            ok = sqlite3_step(stmt) == SQLITE_DONE;
            finalize(stmt);
            if (!ok) break;
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not create experiment";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::update_experiment(const Experiment& experiment, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    const std::string now = utils::get_current_iso8601();
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE experiments SET name = ?, description = ?, status = ?, objective = ?,
            min_impressions = ?, min_lift_percent = ?, auto_winner = ?,
            winner_campaign_id = ?, started_at = ?, ended_at = ?, updated_at = ?
        WHERE id = ?;
    )";
    bool ok = prepare(db_, sql, &stmt);
    if (ok) {
        int i = 1;
        bind_text(stmt, i++, experiment.name);
        bind_text(stmt, i++, experiment.description);
        bind_text(stmt, i++, experiment.status);
        bind_text(stmt, i++, experiment.objective);
        sqlite3_bind_int(stmt, i++, experiment.min_impressions);
        sqlite3_bind_int(stmt, i++, experiment.min_lift_percent);
        sqlite3_bind_int(stmt, i++, experiment.auto_winner ? 1 : 0);
        if (experiment.winner_campaign_id.empty()) sqlite3_bind_null(stmt, i++);
        else bind_text(stmt, i++, experiment.winner_campaign_id);
        if (experiment.started_at.empty()) sqlite3_bind_null(stmt, i++);
        else bind_text(stmt, i++, experiment.started_at);
        if (experiment.ended_at.empty()) sqlite3_bind_null(stmt, i++);
        else bind_text(stmt, i++, experiment.ended_at);
        bind_text(stmt, i++, now);
        bind_text(stmt, i, experiment.id);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
        finalize(stmt);
    }
    if (ok) {
        ok = prepare(db_, "DELETE FROM experiment_variants WHERE experiment_id = ?;", &stmt);
        if (ok) {
            bind_text(stmt, 1, experiment.id);
            ok = sqlite3_step(stmt) == SQLITE_DONE;
            finalize(stmt);
        }
    }
    if (ok) {
        for (const auto& raw : experiment.variants) {
            ExperimentVariant variant = raw;
            if (variant.id.empty()) variant.id = utils::generate_uuid();
            variant.experiment_id = experiment.id;
            variant.created_at = now;
            const char* variant_sql = R"(
                INSERT INTO experiment_variants
                    (id, experiment_id, campaign_id, name, weight, is_control, created_at)
                VALUES (?, ?, ?, ?, ?, ?, ?);
            )";
            ok = prepare(db_, variant_sql, &stmt);
            if (!ok) break;
            bind_text(stmt, 1, variant.id);
            bind_text(stmt, 2, variant.experiment_id);
            bind_text(stmt, 3, variant.campaign_id);
            bind_text(stmt, 4, variant.name);
            sqlite3_bind_int(stmt, 5, variant.weight);
            sqlite3_bind_int(stmt, 6, variant.is_control ? 1 : 0);
            bind_text(stmt, 7, variant.created_at);
            ok = sqlite3_step(stmt) == SQLITE_DONE;
            finalize(stmt);
            if (!ok) break;
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not update experiment";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::delete_experiment(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM experiments WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::set_experiment_status(const std::string& id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE experiments SET status = ?,
            started_at = CASE WHEN ? = 'running' AND (started_at IS NULL OR started_at = '')
                              THEN ? ELSE started_at END,
            ended_at = CASE WHEN ? = 'completed' AND (ended_at IS NULL OR ended_at = '')
                            THEN ? ELSE ended_at END,
            updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    const std::string now = utils::get_current_iso8601();
    bind_text(stmt, 1, status);
    bind_text(stmt, 2, status);
    bind_text(stmt, 3, now);
    bind_text(stmt, 4, status);
    bind_text(stmt, 5, now);
    bind_text(stmt, 6, now);
    bind_text(stmt, 7, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

nlohmann::json Database::get_experiment_metrics(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto experiment = experiment_by_id_locked(db_, id);
    if (!experiment) return {{"error", "Experiment not found"}};
    const std::string end = utils::get_current_iso8601();
    const std::string start = experiment->started_at.empty()
        ? utils::iso8601_days_ago(30) : experiment->started_at;
    nlohmann::json variants = nlohmann::json::array();
    long long total_impressions = 0;
    for (const auto& variant : experiment->variants) {
        VariantMetrics metrics = metrics_for_campaign_locked(db_, variant.campaign_id, start, end);
        total_impressions += metrics.impressions;
        nlohmann::json row = variant.to_json();
        row["campaign_name"] = metrics.campaign_name;
        row["campaign_status"] = metrics.campaign_status;
        row["impressions"] = metrics.impressions;
        row["clicks"] = metrics.clicks;
        row["conversions"] = metrics.conversions;
        row["value_cents"] = metrics.value_cents;
        row["commission_cents"] = metrics.commission_cents;
        row["ctr"] = metrics.ctr;
        row["conversion_rate"] = metrics.conversion_rate;
        row["revenue_per_mille_cents"] = metrics.revenue_per_mille;
        row["score"] = objective_score(metrics, experiment->objective);
        variants.push_back(row);
    }
    return {
        {"experiment", experiment->to_json()},
        {"variants", variants},
        {"total_impressions", total_impressions},
        {"period_start", start},
        {"period_end", end}
    };
}

int Database::optimize_experiments(const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    int updates = 0;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id FROM experiments
        WHERE status = 'running' AND auto_winner = 1;
    )";
    if (!prepare(db_, sql, &stmt)) return 0;
    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) ids.push_back(text_col(stmt, 0));
    finalize(stmt);

    for (const auto& id : ids) {
        auto experiment = experiment_by_id_locked(db_, id);
        if (!experiment || experiment->variants.empty()) continue;
        const std::string start = experiment->started_at.empty()
            ? utils::iso8601_days_ago(30) : experiment->started_at;
        std::vector<VariantMetrics> metrics;
        bool enough_sample = true;
        for (const auto& variant : experiment->variants) {
            VariantMetrics row = metrics_for_campaign_locked(db_, variant.campaign_id, start, now_iso);
            if (row.impressions < experiment->min_impressions) enough_sample = false;
            metrics.push_back(row);
        }
        if (!enough_sample) continue;

        double best_score = -1.0;
        double second_score = -1.0;
        size_t best_index = 0;
        for (size_t i = 0; i < metrics.size(); ++i) {
            double score = objective_score(metrics[i], experiment->objective);
            if (score > best_score) {
                second_score = best_score;
                best_score = score;
                best_index = i;
            } else if (score > second_score) {
                second_score = score;
            }
        }
        if (best_score <= 0.0) continue;
        const double lift_threshold = second_score <= 0.0
            ? 0.0 : second_score * (1.0 + experiment->min_lift_percent / 100.0);
        if (best_score >= lift_threshold && (second_score < 0.0 || best_score > second_score)) {
            const std::string winner = experiment->variants[best_index].campaign_id;
            sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
            sqlite3_stmt* update = nullptr;
            bool ok = prepare(db_, "UPDATE experiments SET status = 'completed', winner_campaign_id = ?, "
                                    "ended_at = ?, updated_at = ? WHERE id = ?;", &update);
            if (ok) {
                bind_text(update, 1, winner);
                bind_text(update, 2, now_iso);
                bind_text(update, 3, now_iso);
                bind_text(update, 4, id);
                ok = sqlite3_step(update) == SQLITE_DONE;
                finalize(update);
            }
            if (ok) {
                for (size_t i = 0; i < experiment->variants.size(); ++i) {
                    const auto& variant = experiment->variants[i];
                    const bool is_winner = variant.campaign_id == winner;
                    if (!prepare(db_, "UPDATE experiment_variants SET weight = ? WHERE id = ?;", &update)) {
                        ok = false;
                        break;
                    }
                    sqlite3_bind_int(update, 1, is_winner ? 100 : 0);
                    bind_text(update, 2, variant.id);
                    ok = sqlite3_step(update) == SQLITE_DONE;
                    finalize(update);
                    if (!ok) break;
                    if (!is_winner) {
                        if (!prepare(db_, "UPDATE campaigns SET status = 'paused', updated_at = ? WHERE id = ?;", &update)) {
                            ok = false;
                            break;
                        }
                        bind_text(update, 1, now_iso);
                        bind_text(update, 2, variant.campaign_id);
                        sqlite3_step(update);
                        finalize(update);
                    }
                }
            }
            if (ok) {
                sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
                ++updates;
            } else {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            }
            continue;
        }

        double total_score = 0.0;
        for (const auto& row : metrics) total_score += std::max(0.0, objective_score(row, experiment->objective));
        if (total_score <= 0.0) continue;
        std::vector<ExperimentVariant> adjusted = experiment->variants;
        int weight_total = 0;
        for (size_t i = 0; i < adjusted.size(); ++i) {
            const double score = std::max(0.0, objective_score(metrics[i], experiment->objective));
            adjusted[i].weight = std::max(5, static_cast<int>(std::lround((score / total_score) * 100.0)));
            weight_total += adjusted[i].weight;
        }
        if (weight_total <= 0) continue;
        if (update_variant_weights_locked(db_, id, adjusted)) {
            sqlite3_stmt* update = nullptr;
            if (prepare(db_, "UPDATE experiments SET updated_at = ? WHERE id = ?;", &update)) {
                bind_text(update, 1, now_iso);
                bind_text(update, 2, id);
                sqlite3_step(update);
                finalize(update);
            }
            ++updates;
        }
    }
    return updates;
}

} // namespace adcontrol
