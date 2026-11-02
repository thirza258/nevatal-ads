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

nlohmann::json parse_json(const std::string& raw) {
    try {
        auto parsed = nlohmann::json::parse(raw.empty() ? "{}" : raw);
        return parsed.is_object() ? parsed : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}

AutomationStep step_from_row(sqlite3_stmt* stmt) {
    AutomationStep step;
    step.id = text_col(stmt, 0);
    step.workflow_id = text_col(stmt, 1);
    step.type = text_col(stmt, 2);
    step.config = text_col(stmt, 3);
    step.position = sqlite3_column_int(stmt, 4);
    step.created_at = text_col(stmt, 5);
    return step;
}

const char* STEP_COLUMNS = "id, workflow_id, type, config, position, created_at";

std::vector<AutomationStep> load_steps_locked(sqlite3* db, const std::string& workflow_id) {
    std::vector<AutomationStep> steps;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + STEP_COLUMNS +
        " FROM automation_steps WHERE workflow_id = ? ORDER BY position, created_at;";
    if (!prepare(db, sql, &stmt)) return steps;
    bind_text(stmt, 1, workflow_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) steps.push_back(step_from_row(stmt));
    finalize(stmt);
    return steps;
}

AutomationWorkflow workflow_from_row(sqlite3_stmt* stmt) {
    AutomationWorkflow workflow;
    workflow.id = text_col(stmt, 0);
    workflow.publisher_id = text_col(stmt, 1);
    workflow.site_id = text_col(stmt, 2);
    workflow.name = text_col(stmt, 3);
    workflow.description = text_col(stmt, 4);
    workflow.status = text_col(stmt, 5);
    workflow.trigger_type = text_col(stmt, 6);
    workflow.trigger_config = text_col(stmt, 7);
    workflow.last_triggered_at = text_col(stmt, 8);
    workflow.next_run_at = text_col(stmt, 9);
    workflow.created_by = text_col(stmt, 10);
    workflow.created_at = text_col(stmt, 11);
    workflow.updated_at = text_col(stmt, 12);
    return workflow;
}

const char* WORKFLOW_COLUMNS =
    "id, COALESCE(publisher_id, ''), COALESCE(site_id, ''), name, description, status, "
    "trigger_type, trigger_config, COALESCE(last_triggered_at, ''), COALESCE(next_run_at, ''), "
    "created_by, created_at, updated_at";

AutomationRun run_from_row(sqlite3_stmt* stmt) {
    AutomationRun run;
    run.id = text_col(stmt, 0);
    run.workflow_id = text_col(stmt, 1);
    run.trigger_key = text_col(stmt, 2);
    run.status = text_col(stmt, 3);
    run.trigger_payload = text_col(stmt, 4);
    run.current_step = sqlite3_column_int(stmt, 5);
    run.resume_at = text_col(stmt, 6);
    run.started_at = text_col(stmt, 7);
    run.completed_at = text_col(stmt, 8);
    run.error = text_col(stmt, 9);
    run.log = text_col(stmt, 10);
    run.created_at = text_col(stmt, 11);
    run.updated_at = text_col(stmt, 12);
    return run;
}

const char* RUN_COLUMNS =
    "id, workflow_id, trigger_key, status, trigger_payload, current_step, "
    "COALESCE(resume_at, ''), COALESCE(started_at, ''), COALESCE(completed_at, ''), "
    "error, log, created_at, updated_at";

AppNotification notification_from_row(sqlite3_stmt* stmt) {
    AppNotification notification;
    notification.id = text_col(stmt, 0);
    notification.user_id = text_col(stmt, 1);
    notification.publisher_id = text_col(stmt, 2);
    notification.kind = text_col(stmt, 3);
    notification.title = text_col(stmt, 4);
    notification.body = text_col(stmt, 5);
    notification.link = text_col(stmt, 6);
    notification.read_at = text_col(stmt, 7);
    notification.created_at = text_col(stmt, 8);
    return notification;
}

const char* NOTIFICATION_COLUMNS =
    "id, COALESCE(user_id, ''), COALESCE(publisher_id, ''), kind, title, body, link, "
    "COALESCE(read_at, ''), created_at";

std::string initial_next_run(const std::string& trigger_type, const std::string& config) {
    if (trigger_type != "schedule") return "";
    auto parsed = parse_json(config);
    std::string run_at = parsed.value("run_at", "");
    if (run_at.empty()) return "";
    return utils::normalize_iso8601_utc(run_at);
}

int insert_step(sqlite3* db, AutomationStep& step) {
    if (step.id.empty()) step.id = utils::generate_uuid();
    if (step.config.empty()) step.config = "{}";
    if (step.created_at.empty()) step.created_at = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO automation_steps (id, workflow_id, type, config, position, created_at)
        VALUES (?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db, sql, &stmt)) return 0;
    bind_text(stmt, 1, step.id);
    bind_text(stmt, 2, step.workflow_id);
    bind_text(stmt, 3, step.type);
    bind_text(stmt, 4, step.config);
    sqlite3_bind_int(stmt, 5, step.position);
    bind_text(stmt, 6, step.created_at);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

bool queue_run_locked(sqlite3* db, const std::string& workflow_id,
                      const std::string& trigger_key, const std::string& payload,
                      std::string& run_id) {
    run_id.clear();
    if (trigger_key.empty()) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO automation_runs
            (id, workflow_id, trigger_key, status, trigger_payload, current_step,
             created_at, updated_at)
        VALUES (?, ?, ?, 'queued', ?, 0, ?, ?);
    )";
    if (!prepare(db, sql, &stmt)) return false;
    const std::string id = utils::generate_uuid();
    const std::string now = utils::get_current_iso8601();
    bind_text(stmt, 1, id);
    bind_text(stmt, 2, workflow_id);
    bind_text(stmt, 3, trigger_key);
    bind_text(stmt, 4, payload.empty() ? "{}" : payload);
    bind_text(stmt, 5, now);
    bind_text(stmt, 6, now);
    const int rc = sqlite3_step(stmt);
    const bool inserted = rc == SQLITE_DONE && sqlite3_changes(db) > 0;
    finalize(stmt);
    if (inserted) run_id = id;
    return inserted;
}

nlohmann::json metrics_locked(sqlite3* db, const std::string& site_id,
                              const std::string& campaign_id, int lookback_hours) {
    nlohmann::json metrics = {
        {"site_id", site_id},
        {"campaign_id", campaign_id},
        {"lookback_hours", lookback_hours},
        {"impressions", 0},
        {"clicks", 0},
        {"conversions", 0},
        {"value_cents", 0},
        {"commission_cents", 0},
        {"ctr", 0.0},
        {"conversion_rate", 0.0}
    };
    if (site_id.empty() && campaign_id.empty()) return metrics;
    const std::time_t now = utils::utc_now_epoch();
    const std::string end = utils::iso8601_from_epoch(now);
    const std::string start = utils::iso8601_from_epoch(
        now - static_cast<std::time_t>(std::max(1, lookback_hours)) * 3600);

    long long impressions = 0;
    long long clicks = 0;
    std::string ad_sql = R"(
        SELECT COUNT(CASE WHEN event_type = 'impression' THEN 1 END),
               COUNT(CASE WHEN event_type = 'click' THEN 1 END)
        FROM ad_events WHERE occurred_at >= ? AND occurred_at <= ?
    )";
    if (!campaign_id.empty()) ad_sql += " AND campaign_id = ?";
    else ad_sql += " AND site_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (prepare(db, ad_sql, &stmt)) {
        bind_text(stmt, 1, start);
        bind_text(stmt, 2, end);
        bind_text(stmt, 3, campaign_id.empty() ? site_id : campaign_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            impressions = sqlite3_column_int64(stmt, 0);
            clicks = sqlite3_column_int64(stmt, 1);
        }
        finalize(stmt);
    }

    long long conversions = 0;
    long long value_cents = 0;
    long long commission_cents = 0;
    std::string conv_sql = R"(
        SELECT COUNT(*), COALESCE(SUM(value_cents), 0), COALESCE(SUM(commission_cents), 0)
        FROM conversion_events WHERE occurred_at >= ? AND occurred_at <= ?
    )";
    if (!campaign_id.empty()) conv_sql += " AND campaign_id = ?";
    else conv_sql += " AND site_id = ?";
    stmt = nullptr;
    if (prepare(db, conv_sql, &stmt)) {
        bind_text(stmt, 1, start);
        bind_text(stmt, 2, end);
        bind_text(stmt, 3, campaign_id.empty() ? site_id : campaign_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            conversions = sqlite3_column_int64(stmt, 0);
            value_cents = sqlite3_column_int64(stmt, 1);
            commission_cents = sqlite3_column_int64(stmt, 2);
        }
        finalize(stmt);
    }

    metrics["impressions"] = impressions;
    metrics["clicks"] = clicks;
    metrics["conversions"] = conversions;
    metrics["value_cents"] = value_cents;
    metrics["commission_cents"] = commission_cents;
    metrics["ctr"] = impressions > 0
        ? (static_cast<double>(clicks) / static_cast<double>(impressions)) * 100.0 : 0.0;
    metrics["conversion_rate"] = clicks > 0
        ? (static_cast<double>(conversions) / static_cast<double>(clicks)) * 100.0 : 0.0;
    return metrics;
}

double metric_value(const nlohmann::json& metrics, const std::string& metric) {
    if (metric == "impressions") return metrics.value("impressions", 0LL);
    if (metric == "clicks") return metrics.value("clicks", 0LL);
    if (metric == "conversions") return metrics.value("conversions", 0LL);
    if (metric == "value_cents") return metrics.value("value_cents", 0LL);
    if (metric == "commission_cents") return metrics.value("commission_cents", 0LL);
    if (metric == "conversion_rate") return metrics.value("conversion_rate", 0.0);
    return metrics.value("ctr", 0.0);
}

bool compare_metric(double actual, const std::string& comparator, double threshold) {
    if (comparator == "gt") return actual > threshold;
    if (comparator == "gte") return actual >= threshold;
    if (comparator == "lte") return actual <= threshold;
    if (comparator == "eq") return std::fabs(actual - threshold) < 1e-9;
    return actual < threshold; // "lt" default
}

std::string add_interval_to_iso(const std::string& iso, int seconds, const std::string& now_iso) {
    std::time_t epoch = 0;
    std::time_t now_epoch = 0;
    if (!utils::parse_iso8601_utc(iso, epoch) || !utils::parse_iso8601_utc(now_iso, now_epoch)) {
        return "";
    }
    epoch += seconds;
    while (epoch <= now_epoch) epoch += seconds;
    return utils::iso8601_from_epoch(epoch);
}

} // namespace

std::vector<AutomationStep> Database::list_automation_steps(const std::string& workflow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_steps_locked(db_, workflow_id);
}

std::vector<AutomationWorkflow> Database::list_automation_workflows(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id,
    const std::optional<std::string>& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AutomationWorkflow> workflows;
    std::string sql = std::string("SELECT ") + WORKFLOW_COLUMNS + " FROM automation_workflows WHERE 1=1";
    if (publisher_id.has_value()) sql += " AND publisher_id = ?";
    if (site_id.has_value()) sql += " AND site_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    sql += " ORDER BY created_at DESC, id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return workflows;
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    if (status.has_value()) bind_text(stmt, index++, *status);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AutomationWorkflow workflow = workflow_from_row(stmt);
        workflow.steps = load_steps_locked(db_, workflow.id);
        workflows.push_back(workflow);
    }
    finalize(stmt);
    return workflows;
}

std::optional<AutomationWorkflow> Database::get_automation_workflow(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + WORKFLOW_COLUMNS +
        " FROM automation_workflows WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<AutomationWorkflow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = workflow_from_row(stmt);
        result->steps = load_steps_locked(db_, id);
    }
    finalize(stmt);
    return result;
}

bool Database::create_automation_workflow(AutomationWorkflow& workflow, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (workflow.id.empty()) workflow.id = utils::generate_uuid();
    if (workflow.status.empty()) workflow.status = "draft";
    if (workflow.trigger_type.empty()) workflow.trigger_type = "manual";
    const std::string now = utils::get_current_iso8601();
    workflow.created_at = now;
    workflow.updated_at = now;
    if (workflow.next_run_at.empty()) {
        workflow.next_run_at = initial_next_run(workflow.trigger_type, workflow.trigger_config);
    }
    for (size_t i = 0; i < workflow.steps.size(); ++i) {
        workflow.steps[i].workflow_id = workflow.id;
        if (workflow.steps[i].position < 0) workflow.steps[i].position = static_cast<int>(i);
    }

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO automation_workflows
            (id, publisher_id, site_id, name, description, status, trigger_type,
             trigger_config, next_run_at, created_by, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not create workflow";
        return false;
    }
    bind_text(stmt, 1, workflow.id);
    if (workflow.publisher_id.empty()) sqlite3_bind_null(stmt, 2);
    else bind_text(stmt, 2, workflow.publisher_id);
    if (workflow.site_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, workflow.site_id);
    bind_text(stmt, 4, workflow.name);
    bind_text(stmt, 5, workflow.description);
    bind_text(stmt, 6, workflow.status);
    bind_text(stmt, 7, workflow.trigger_type);
    bind_text(stmt, 8, workflow.trigger_config);
    if (workflow.next_run_at.empty()) sqlite3_bind_null(stmt, 9);
    else bind_text(stmt, 9, workflow.next_run_at);
    bind_text(stmt, 10, workflow.created_by);
    bind_text(stmt, 11, workflow.created_at);
    bind_text(stmt, 12, workflow.updated_at);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (ok) {
        for (auto& step : workflow.steps) {
            if (!insert_step(db_, step)) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    error = "Could not save workflow steps";
    return false;
}

bool Database::update_automation_workflow(const AutomationWorkflow& workflow, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    const std::string now = utils::get_current_iso8601();
    const std::string next_run = !workflow.next_run_at.empty()
        ? workflow.next_run_at
        : initial_next_run(workflow.trigger_type, workflow.trigger_config);

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE automation_workflows
        SET publisher_id = ?, site_id = ?, name = ?, description = ?, status = ?,
            trigger_type = ?, trigger_config = ?, next_run_at = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not update workflow";
        return false;
    }
    if (workflow.publisher_id.empty()) sqlite3_bind_null(stmt, 1);
    else bind_text(stmt, 1, workflow.publisher_id);
    if (workflow.site_id.empty()) sqlite3_bind_null(stmt, 2);
    else bind_text(stmt, 2, workflow.site_id);
    bind_text(stmt, 3, workflow.name);
    bind_text(stmt, 4, workflow.description);
    bind_text(stmt, 5, workflow.status);
    bind_text(stmt, 6, workflow.trigger_type);
    bind_text(stmt, 7, workflow.trigger_config);
    if (next_run.empty()) sqlite3_bind_null(stmt, 8);
    else bind_text(stmt, 8, next_run);
    bind_text(stmt, 9, now);
    bind_text(stmt, 10, workflow.id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    if (ok) {
        sqlite3_stmt* del = nullptr;
        if (!prepare(db_, "DELETE FROM automation_steps WHERE workflow_id = ?;", &del)) {
            ok = false;
        } else {
            bind_text(del, 1, workflow.id);
            ok = sqlite3_step(del) == SQLITE_DONE;
            finalize(del);
        }
    }
    if (ok) {
        std::vector<AutomationStep> steps = workflow.steps;
        for (size_t i = 0; i < steps.size(); ++i) {
            steps[i].workflow_id = workflow.id;
            steps[i].position = static_cast<int>(i);
            steps[i].created_at = now;
            if (!insert_step(db_, steps[i])) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    error = "Could not save workflow steps";
    return false;
}

bool Database::set_automation_workflow_status(const std::string& id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE automation_workflows
        SET status = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    const std::string now = utils::get_current_iso8601();
    bind_text(stmt, 1, status);
    bind_text(stmt, 2, now);
    bind_text(stmt, 3, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_automation_workflow(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM automation_workflows WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

std::vector<AutomationRun> Database::list_automation_runs(
    const std::optional<std::string>& workflow_id,
    const std::optional<std::string>& publisher_id,
    int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AutomationRun> runs;
    std::string sql = std::string("SELECT r.") + "id, r.workflow_id, r.trigger_key, r.status, "
        "r.trigger_payload, r.current_step, COALESCE(r.resume_at, ''), "
        "COALESCE(r.started_at, ''), COALESCE(r.completed_at, ''), r.error, r.log, "
        "r.created_at, r.updated_at FROM automation_runs r "
        "JOIN automation_workflows w ON r.workflow_id = w.id WHERE 1=1";
    if (workflow_id.has_value()) sql += " AND r.workflow_id = ?";
    if (publisher_id.has_value()) sql += " AND w.publisher_id = ?";
    sql += " ORDER BY r.created_at DESC, r.id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return runs;
    int index = 1;
    if (workflow_id.has_value()) bind_text(stmt, index++, *workflow_id);
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    sqlite3_bind_int(stmt, index, std::max(1, std::min(limit, 500)));
    while (sqlite3_step(stmt) == SQLITE_ROW) runs.push_back(run_from_row(stmt));
    finalize(stmt);
    return runs;
}

std::optional<AutomationRun> Database::get_automation_run(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + RUN_COLUMNS +
        " FROM automation_runs WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<AutomationRun> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = run_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::update_automation_run(const AutomationRun& run) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE automation_runs
        SET status = ?, current_step = ?, resume_at = ?, started_at = ?,
            completed_at = ?, error = ?, log = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, run.status);
    sqlite3_bind_int(stmt, 2, run.current_step);
    if (run.resume_at.empty()) sqlite3_bind_null(stmt, 3); else bind_text(stmt, 3, run.resume_at);
    if (run.started_at.empty()) sqlite3_bind_null(stmt, 4); else bind_text(stmt, 4, run.started_at);
    if (run.completed_at.empty()) sqlite3_bind_null(stmt, 5); else bind_text(stmt, 5, run.completed_at);
    bind_text(stmt, 6, run.error);
    bind_text(stmt, 7, run.log);
    bind_text(stmt, 8, utils::get_current_iso8601());
    bind_text(stmt, 9, run.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::queue_manual_automation_run(const std::string& workflow_id,
                                            const std::string& payload_json,
                                            const std::string& trigger_key,
                                            std::string& run_id,
                                            std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    run_id.clear();
    sqlite3_stmt* check = nullptr;
    const char* check_sql = "SELECT status FROM automation_workflows WHERE id = ? LIMIT 1;";
    if (!prepare(db_, check_sql, &check)) {
        error = "Could not load workflow";
        return false;
    }
    bind_text(check, 1, workflow_id);
    std::string status;
    bool found = false;
    if (sqlite3_step(check) == SQLITE_ROW) {
        status = text_col(check, 0);
        found = true;
    }
    finalize(check);
    if (!found) {
        error = "Workflow not found";
        return false;
    }
    if (status != "active") {
        error = "Workflow must be active before it can run";
        return false;
    }
    if (!queue_run_locked(db_, workflow_id, trigger_key, payload_json, run_id)) {
        error = "Could not queue workflow run";
        return false;
    }
    sqlite3_stmt* update = nullptr;
    if (prepare(db_, "UPDATE automation_workflows SET last_triggered_at = ?, updated_at = ? WHERE id = ?;",
                &update)) {
        const std::string now = utils::get_current_iso8601();
        bind_text(update, 1, now);
        bind_text(update, 2, now);
        bind_text(update, 3, workflow_id);
        sqlite3_step(update);
        finalize(update);
    }
    return true;
}

bool Database::append_automation_run_log(const std::string& run_id, const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE automation_runs
        SET log = substr(log || ? || char(10), -16000), updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, line);
    bind_text(stmt, 2, utils::get_current_iso8601());
    bind_text(stmt, 3, run_id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

int Database::enqueue_automation_event(const std::string& trigger_type,
                                        const std::string& publisher_id,
                                        const std::string& site_id,
                                        const std::string& payload_json,
                                        const std::string& trigger_key,
                                        std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (trigger_type.empty() || trigger_key.empty()) {
        error = "trigger_type and trigger_key are required";
        return 0;
    }
    nlohmann::json payload = parse_json(payload_json);
    int queued = 0;
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "SELECT id, publisher_id, site_id, trigger_config FROM automation_workflows "
                       "WHERE status = 'active' AND trigger_type = ?;", &stmt)) {
        error = "Could not load automation workflows";
        return 0;
    }
    bind_text(stmt, 1, trigger_type);
    struct Candidate { std::string id; std::string publisher_id; std::string site_id; std::string config; };
    std::vector<Candidate> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        candidates.push_back({text_col(stmt, 0), text_col(stmt, 1), text_col(stmt, 2), text_col(stmt, 3)});
    }
    finalize(stmt);

    for (const auto& candidate : candidates) {
        if (!candidate.site_id.empty() && candidate.site_id != site_id) continue;
        if (!candidate.publisher_id.empty() && candidate.publisher_id != publisher_id) continue;
        auto config = parse_json(candidate.config);
        if (config.contains("campaign_id") && config["campaign_id"].is_string()) {
            const std::string wanted = config["campaign_id"].get<std::string>();
            if (!wanted.empty() && wanted != payload.value("campaign_id", "")) continue;
        }
        if (config.contains("event_name") && config["event_name"].is_string()) {
            const std::string wanted = config["event_name"].get<std::string>();
            if (!wanted.empty() && wanted != payload.value("event_name", "")) continue;
        }
        if (config.contains("kind") && config["kind"].is_string() && trigger_type == "inquiry") {
            const std::string wanted = config["kind"].get<std::string>();
            if (!wanted.empty() && wanted != payload.value("kind", "")) continue;
        }
        if (config.contains("min_value_cents")) {
            long long minimum = 0;
            try { minimum = config["min_value_cents"].get<long long>(); } catch (...) {}
            long long value = 0;
            try { value = payload.value("value_cents", 0LL); } catch (...) {}
            if (value < minimum) continue;
        }
        std::string run_id;
        if (queue_run_locked(db_, candidate.id, trigger_key, payload_json, run_id)) {
            ++queued;
            sqlite3_stmt* update = nullptr;
            if (prepare(db_, "UPDATE automation_workflows SET last_triggered_at = ?, updated_at = ? WHERE id = ?;",
                        &update)) {
                const std::string now = utils::get_current_iso8601();
                bind_text(update, 1, now);
                bind_text(update, 2, now);
                bind_text(update, 3, candidate.id);
                sqlite3_step(update);
                finalize(update);
            }
        }
    }
    return queued;
}

int Database::create_due_scheduled_runs(const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    int queued = 0;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, trigger_config, next_run_at FROM automation_workflows
        WHERE status = 'active' AND trigger_type = 'schedule'
          AND next_run_at IS NOT NULL AND next_run_at <> '' AND next_run_at <= ?
        ORDER BY next_run_at LIMIT 100;
    )";
    if (!prepare(db_, sql, &stmt)) return 0;
    bind_text(stmt, 1, now_iso);
    struct Due { std::string id; std::string config; std::string at; };
    std::vector<Due> due;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        due.push_back({text_col(stmt, 0), text_col(stmt, 1), text_col(stmt, 2)});
    }
    finalize(stmt);

    for (const auto& item : due) {
        const std::string trigger_key = "schedule:" + item.at;
        std::string run_id;
        if (queue_run_locked(db_, item.id, trigger_key,
                             nlohmann::json({{"trigger", "schedule"}, {"scheduled_for", item.at}}).dump(),
                             run_id)) {
            ++queued;
        }
        auto config = parse_json(item.config);
        std::string recurrence = config.value("recurrence", "none");
        std::string next;
        if (recurrence == "daily") next = add_interval_to_iso(item.at, 86400, now_iso);
        else if (recurrence == "weekly") next = add_interval_to_iso(item.at, 7 * 86400, now_iso);
        else if (recurrence == "hourly") next = add_interval_to_iso(item.at, 3600, now_iso);

        sqlite3_stmt* update = nullptr;
        const char* update_sql = "UPDATE automation_workflows SET last_triggered_at = ?, "
            "next_run_at = ?, updated_at = ? WHERE id = ?;";
        if (prepare(db_, update_sql, &update)) {
            bind_text(update, 1, now_iso);
            if (next.empty()) sqlite3_bind_null(update, 2); else bind_text(update, 2, next);
            bind_text(update, 3, now_iso);
            bind_text(update, 4, item.id);
            sqlite3_step(update);
            finalize(update);
        }
    }
    return queued;
}

int Database::evaluate_performance_triggers(const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    int queued = 0;
    const std::time_t now_epoch = utils::utc_now_epoch();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, site_id, trigger_config, COALESCE(last_triggered_at, '')
        FROM automation_workflows
        WHERE status = 'active' AND trigger_type = 'performance';
    )";
    if (!prepare(db_, sql, &stmt)) return 0;
    struct Candidate { std::string id; std::string site_id; std::string config; std::string last; };
    std::vector<Candidate> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        candidates.push_back({text_col(stmt, 0), text_col(stmt, 1), text_col(stmt, 2), text_col(stmt, 3)});
    }
    finalize(stmt);

    for (const auto& candidate : candidates) {
        auto config = parse_json(candidate.config);
        int cooldown_minutes = 60;
        try { cooldown_minutes = std::max(1, config.value("cooldown_minutes", 60)); } catch (...) {}
        std::time_t last_epoch = 0;
        if (!candidate.last.empty() && utils::parse_iso8601_utc(candidate.last, last_epoch)) {
            if (now_epoch - last_epoch < static_cast<std::time_t>(cooldown_minutes) * 60) continue;
        }
        std::string site_id = candidate.site_id;
        if (config.contains("site_id") && config["site_id"].is_string()) {
            site_id = config["site_id"].get<std::string>();
        }
        std::string campaign_id;
        if (config.contains("campaign_id") && config["campaign_id"].is_string()) {
            campaign_id = config["campaign_id"].get<std::string>();
        }
        if (site_id.empty() && campaign_id.empty()) continue;
        int lookback_hours = 24;
        try { lookback_hours = std::max(1, std::min(config.value("lookback_hours", 24), 720)); } catch (...) {}
        auto metrics = metrics_locked(db_, site_id, campaign_id, lookback_hours);
        long long impressions = metrics.value("impressions", 0LL);
        int min_impressions = 100;
        try { min_impressions = std::max(0, config.value("min_impressions", 100)); } catch (...) {}
        if (impressions < min_impressions) continue;
        const std::string metric = config.value("metric", "ctr");
        const std::string comparator = config.value("comparator", "lt");
        double threshold = 0.0;
        try { threshold = config.value("threshold", 0.0); } catch (...) {}
        const double actual = metric_value(metrics, metric);
        if (!compare_metric(actual, comparator, threshold)) continue;

        const std::string trigger_key = "performance:" + candidate.id + ":" +
            std::to_string(now_epoch / (static_cast<long long>(cooldown_minutes) * 60));
        nlohmann::json payload = {
            {"trigger", "performance"},
            {"workflow_id", candidate.id},
            {"site_id", site_id},
            {"campaign_id", campaign_id},
            {"metric", metric},
            {"comparator", comparator},
            {"threshold", threshold},
            {"actual", actual},
            {"lookback_hours", lookback_hours},
            {"metrics", metrics}
        };
        std::string run_id;
        if (queue_run_locked(db_, candidate.id, trigger_key, payload.dump(), run_id)) {
            ++queued;
            sqlite3_stmt* update = nullptr;
            if (prepare(db_, "UPDATE automation_workflows SET last_triggered_at = ?, updated_at = ? WHERE id = ?;",
                        &update)) {
                bind_text(update, 1, now_iso);
                bind_text(update, 2, now_iso);
                bind_text(update, 3, candidate.id);
                sqlite3_step(update);
                finalize(update);
            }
        }
    }
    return queued;
}

std::vector<AutomationRun> Database::list_due_automation_runs(const std::string& now_iso, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AutomationRun> runs;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, workflow_id, trigger_key, status, trigger_payload, current_step,
               COALESCE(resume_at, ''), COALESCE(started_at, ''), COALESCE(completed_at, ''),
               error, log, created_at, updated_at
        FROM automation_runs
        WHERE status IN ('queued', 'waiting')
          AND (resume_at IS NULL OR resume_at = '' OR resume_at <= ?)
        ORDER BY created_at LIMIT ?;
    )";
    if (!prepare(db_, sql, &stmt)) return runs;
    bind_text(stmt, 1, now_iso);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 100)));
    while (sqlite3_step(stmt) == SQLITE_ROW) runs.push_back(run_from_row(stmt));
    finalize(stmt);
    return runs;
}

nlohmann::json Database::get_automation_metrics(const std::string& site_id,
                                                 const std::string& campaign_id,
                                                 int lookback_hours) {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_locked(db_, site_id, campaign_id, lookback_hours);
}

bool Database::create_notification(AppNotification& notification) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (notification.id.empty()) notification.id = utils::generate_uuid();
    if (notification.kind.empty()) notification.kind = "info";
    notification.created_at = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO notifications
            (id, user_id, publisher_id, kind, title, body, link, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, notification.id);
    if (notification.user_id.empty()) sqlite3_bind_null(stmt, 2);
    else bind_text(stmt, 2, notification.user_id);
    if (notification.publisher_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, notification.publisher_id);
    bind_text(stmt, 4, notification.kind);
    bind_text(stmt, 5, notification.title);
    bind_text(stmt, 6, notification.body);
    bind_text(stmt, 7, notification.link);
    bind_text(stmt, 8, notification.created_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    return ok;
}

std::optional<AppNotification> Database::get_notification(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + NOTIFICATION_COLUMNS +
        " FROM notifications WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<AppNotification> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = notification_from_row(stmt);
    finalize(stmt);
    return result;
}

std::vector<AppNotification> Database::list_notifications(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& user_id,
    int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AppNotification> notifications;
    std::string sql = std::string("SELECT ") + NOTIFICATION_COLUMNS + " FROM notifications WHERE 1=1";
    if (publisher_id.has_value()) {
        sql += " AND (publisher_id = ? OR publisher_id IS NULL OR publisher_id = '')";
    }
    if (user_id.has_value()) {
        sql += " AND (user_id = ? OR user_id IS NULL OR user_id = '')";
    }
    sql += " ORDER BY created_at DESC, id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return notifications;
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (user_id.has_value()) bind_text(stmt, index++, *user_id);
    sqlite3_bind_int(stmt, index, std::max(1, std::min(limit, 200)));
    while (sqlite3_step(stmt) == SQLITE_ROW) notifications.push_back(notification_from_row(stmt));
    finalize(stmt);
    return notifications;
}

bool Database::mark_notification_read(const std::string& id,
                                       const std::optional<std::string>& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE notifications SET read_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    const std::string now = utils::get_current_iso8601();
    bind_text(stmt, 1, now);
    bind_text(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    (void)user_id;
    return ok;
}

int Database::count_unread_notifications(const std::optional<std::string>& publisher_id,
                                          const std::optional<std::string>& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = "SELECT COUNT(*) FROM notifications WHERE read_at IS NULL";
    if (publisher_id.has_value()) {
        sql += " AND (publisher_id = ? OR publisher_id IS NULL OR publisher_id = '')";
    }
    if (user_id.has_value()) {
        sql += " AND (user_id = ? OR user_id IS NULL OR user_id = '')";
    }
    sql += ";";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return 0;
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (user_id.has_value()) bind_text(stmt, index++, *user_id);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    finalize(stmt);
    return count;
}

} // namespace adcontrol
