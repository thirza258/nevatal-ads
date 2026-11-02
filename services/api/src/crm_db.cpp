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

std::vector<std::string> tags_from_json(const std::string& raw) {
    std::vector<std::string> tags;
    try {
        auto parsed = nlohmann::json::parse(raw.empty() ? "[]" : raw);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (item.is_string()) tags.push_back(item.get<std::string>());
            }
        }
    } catch (...) {}
    return tags;
}

std::string tags_to_json(const std::vector<std::string>& tags) {
    return nlohmann::json(tags).dump();
}

const char* LEAD_SELECT =
    "SELECT l.id, COALESCE(l.inquiry_id, ''), l.name, l.email, l.company, l.website, "
    "l.phone, l.source, l.source_detail, l.stage, l.score, "
    "COALESCE(l.owner_user_id, ''), COALESCE(u.email, ''), l.value_cents, l.currency, "
    "l.tags, l.notes, l.utm_source, l.utm_medium, l.utm_campaign, l.utm_term, "
    "l.utm_content, l.referrer, COALESCE(l.last_contact_at, ''), "
    "COALESCE(l.next_follow_up_at, ''), COALESCE(l.converted_at, ''), "
    "l.created_at, l.updated_at "
    "FROM crm_leads l LEFT JOIN users u ON l.owner_user_id = u.id ";

CrmLead lead_from_row(sqlite3_stmt* stmt) {
    CrmLead lead;
    int i = 0;
    lead.id = text_col(stmt, i++);
    lead.inquiry_id = text_col(stmt, i++);
    lead.name = text_col(stmt, i++);
    lead.email = text_col(stmt, i++);
    lead.company = text_col(stmt, i++);
    lead.website = text_col(stmt, i++);
    lead.phone = text_col(stmt, i++);
    lead.source = text_col(stmt, i++);
    lead.source_detail = text_col(stmt, i++);
    lead.stage = text_col(stmt, i++);
    lead.score = sqlite3_column_int(stmt, i++);
    lead.owner_user_id = text_col(stmt, i++);
    lead.owner_email = text_col(stmt, i++);
    lead.value_cents = sqlite3_column_int(stmt, i++);
    lead.currency = text_col(stmt, i++);
    lead.tags = tags_from_json(text_col(stmt, i++));
    lead.notes = text_col(stmt, i++);
    lead.utm_source = text_col(stmt, i++);
    lead.utm_medium = text_col(stmt, i++);
    lead.utm_campaign = text_col(stmt, i++);
    lead.utm_term = text_col(stmt, i++);
    lead.utm_content = text_col(stmt, i++);
    lead.referrer = text_col(stmt, i++);
    lead.last_contact_at = text_col(stmt, i++);
    lead.next_follow_up_at = text_col(stmt, i++);
    lead.converted_at = text_col(stmt, i++);
    lead.created_at = text_col(stmt, i++);
    lead.updated_at = text_col(stmt, i++);
    return lead;
}

std::optional<CrmLead> lead_by_id_locked(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string(LEAD_SELECT) + "WHERE l.id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<CrmLead> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = lead_from_row(stmt);
    finalize(stmt);
    return result;
}

bool insert_lead_locked(sqlite3* db, CrmLead& lead, std::string& error) {
    error.clear();
    if (lead.id.empty()) lead.id = utils::generate_uuid();
    if (lead.stage.empty()) lead.stage = "new";
    if (lead.currency.empty()) lead.currency = "USD";
    if (lead.source.empty()) lead.source = "manual";
    const std::string now = utils::get_current_iso8601();
    lead.created_at = now;
    lead.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO crm_leads
            (id, inquiry_id, name, email, company, website, phone, source, source_detail,
             stage, score, owner_user_id, value_cents, currency, tags, notes, utm_source,
             utm_medium, utm_campaign, utm_term, utm_content, referrer, last_contact_at,
             next_follow_up_at, converted_at, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db, sql, &stmt)) {
        error = "Could not create lead";
        return false;
    }
    int i = 1;
    bind_text(stmt, i++, lead.id);
    if (lead.inquiry_id.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.inquiry_id);
    bind_text(stmt, i++, lead.name);
    bind_text(stmt, i++, lead.email);
    bind_text(stmt, i++, lead.company);
    bind_text(stmt, i++, lead.website);
    bind_text(stmt, i++, lead.phone);
    bind_text(stmt, i++, lead.source);
    bind_text(stmt, i++, lead.source_detail);
    bind_text(stmt, i++, lead.stage);
    sqlite3_bind_int(stmt, i++, lead.score);
    if (lead.owner_user_id.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.owner_user_id);
    sqlite3_bind_int(stmt, i++, lead.value_cents);
    bind_text(stmt, i++, lead.currency);
    bind_text(stmt, i++, tags_to_json(lead.tags));
    bind_text(stmt, i++, lead.notes);
    bind_text(stmt, i++, lead.utm_source);
    bind_text(stmt, i++, lead.utm_medium);
    bind_text(stmt, i++, lead.utm_campaign);
    bind_text(stmt, i++, lead.utm_term);
    bind_text(stmt, i++, lead.utm_content);
    bind_text(stmt, i++, lead.referrer);
    if (lead.last_contact_at.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.last_contact_at);
    if (lead.next_follow_up_at.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.next_follow_up_at);
    if (lead.converted_at.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.converted_at);
    bind_text(stmt, i++, lead.created_at);
    bind_text(stmt, i, lead.updated_at);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not create lead";
        return false;
    }
    return true;
}

int calculate_score_locked(sqlite3* db, const CrmLead& lead, int activity_count) {
    int score = 10;
    if (lead.source.find("client") != std::string::npos) score += 30;
    else if (lead.source.find("publisher") != std::string::npos) score += 20;
    else if (lead.source.find("referral") != std::string::npos) score += 15;
    if (!lead.inquiry_id.empty()) score += 10;
    if (!lead.company.empty()) score += 10;
    if (!lead.website.empty()) score += 8;
    if (!lead.phone.empty()) score += 5;
    if (lead.notes.size() > 100) score += 5;
    if (lead.stage == "contacted") score += 5;
    else if (lead.stage == "qualified") score += 15;
    else if (lead.stage == "proposal") score += 20;
    else if (lead.stage == "negotiation") score += 25;
    else if (lead.stage == "won") score += 30;
    else if (lead.stage == "lost") score -= 15;
    score += std::min(20, activity_count * 2);
    if (lead.value_cents >= 100000) score += 5;
    return std::max(0, std::min(100, score));
}

bool recalculate_score_locked(sqlite3* db, const std::string& lead_id) {
    auto lead = lead_by_id_locked(db, lead_id);
    if (!lead) return false;
    int activities = 0;
    sqlite3_stmt* stmt = nullptr;
    if (prepare(db, "SELECT COUNT(*) FROM crm_activities WHERE lead_id = ?;", &stmt)) {
        bind_text(stmt, 1, lead_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) activities = sqlite3_column_int(stmt, 0);
        finalize(stmt);
    }
    const int score = calculate_score_locked(db, *lead, activities);
    sqlite3_stmt* update = nullptr;
    if (!prepare(db, "UPDATE crm_leads SET score = ?, updated_at = ? WHERE id = ?;", &update)) return false;
    const std::string now = utils::get_current_iso8601();
    sqlite3_bind_int(update, 1, score);
    bind_text(update, 2, now);
    bind_text(update, 3, lead_id);
    const bool ok = sqlite3_step(update) == SQLITE_DONE;
    finalize(update);
    return ok;
}

bool insert_activity_locked(sqlite3* db, CrmActivity& activity, std::string& error) {
    if (activity.id.empty()) activity.id = utils::generate_uuid();
    if (activity.type.empty()) activity.type = "note";
    if (activity.occurred_at.empty()) activity.occurred_at = utils::get_current_iso8601();
    activity.created_at = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO crm_activities
            (id, lead_id, type, subject, body, occurred_at, created_by, metadata, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db, sql, &stmt)) {
        error = "Could not save activity";
        return false;
    }
    bind_text(stmt, 1, activity.id);
    bind_text(stmt, 2, activity.lead_id);
    bind_text(stmt, 3, activity.type);
    bind_text(stmt, 4, activity.subject);
    bind_text(stmt, 5, activity.body);
    bind_text(stmt, 6, activity.occurred_at);
    bind_text(stmt, 7, activity.created_by);
    bind_text(stmt, 8, activity.metadata);
    bind_text(stmt, 9, activity.created_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) {
        error = "Could not save activity";
        return false;
    }
    if (activity.type == "email" || activity.type == "call" || activity.type == "meeting") {
        sqlite3_stmt* update = nullptr;
        if (prepare(db, "UPDATE crm_leads SET last_contact_at = ?, updated_at = ? WHERE id = ?;", &update)) {
            bind_text(update, 1, activity.occurred_at);
            bind_text(update, 2, utils::get_current_iso8601());
            bind_text(update, 3, activity.lead_id);
            sqlite3_step(update);
            finalize(update);
        }
    }
    recalculate_score_locked(db, activity.lead_id);
    return true;
}

bool refresh_next_follow_up_locked(sqlite3* db, const std::string& lead_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT MIN(due_at) FROM crm_tasks
        WHERE lead_id = ? AND status = 'open';
    )";
    if (!prepare(db, sql, &stmt)) return false;
    bind_text(stmt, 1, lead_id);
    std::string next;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        next = text_col(stmt, 0);
    }
    finalize(stmt);
    sqlite3_stmt* update = nullptr;
    if (!prepare(db, "UPDATE crm_leads SET next_follow_up_at = ?, updated_at = ? WHERE id = ?;", &update)) return false;
    if (next.empty()) sqlite3_bind_null(update, 1); else bind_text(update, 1, next);
    bind_text(update, 2, utils::get_current_iso8601());
    bind_text(update, 3, lead_id);
    const bool ok = sqlite3_step(update) == SQLITE_DONE;
    finalize(update);
    return ok;
}

CrmActivity activity_from_row(sqlite3_stmt* stmt) {
    CrmActivity activity;
    activity.id = text_col(stmt, 0);
    activity.lead_id = text_col(stmt, 1);
    activity.type = text_col(stmt, 2);
    activity.subject = text_col(stmt, 3);
    activity.body = text_col(stmt, 4);
    activity.occurred_at = text_col(stmt, 5);
    activity.created_by = text_col(stmt, 6);
    activity.metadata = text_col(stmt, 7);
    activity.created_at = text_col(stmt, 8);
    return activity;
}

CrmTask task_from_row(sqlite3_stmt* stmt) {
    CrmTask task;
    task.id = text_col(stmt, 0);
    task.lead_id = text_col(stmt, 1);
    task.title = text_col(stmt, 2);
    task.due_at = text_col(stmt, 3);
    task.status = text_col(stmt, 4);
    task.assigned_to = text_col(stmt, 5);
    task.notes = text_col(stmt, 6);
    task.completed_at = text_col(stmt, 7);
    task.created_at = text_col(stmt, 8);
    task.updated_at = text_col(stmt, 9);
    return task;
}

const char* TASK_COLUMNS =
    "id, lead_id, title, due_at, status, assigned_to, notes, "
    "COALESCE(completed_at, ''), created_at, updated_at";

} // namespace

std::vector<CrmLead> Database::list_crm_leads(const std::optional<std::string>& stage,
                                               const std::string& query,
                                               int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CrmLead> leads;
    std::string sql = std::string(LEAD_SELECT) + "WHERE 1=1";
    if (stage.has_value()) sql += " AND l.stage = ?";
    if (!query.empty()) {
        sql += " AND (l.name LIKE ? OR l.email LIKE ? OR l.company LIKE ? OR l.source LIKE ?)";
    }
    sql += " ORDER BY l.score DESC, l.updated_at DESC, l.id DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return leads;
    int index = 1;
    if (stage.has_value()) bind_text(stmt, index++, *stage);
    if (!query.empty()) {
        const std::string pattern = "%" + query + "%";
        bind_text(stmt, index++, pattern);
        bind_text(stmt, index++, pattern);
        bind_text(stmt, index++, pattern);
        bind_text(stmt, index++, pattern);
    }
    sqlite3_bind_int(stmt, index++, std::max(1, std::min(limit, 500)));
    sqlite3_bind_int(stmt, index, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) leads.push_back(lead_from_row(stmt));
    finalize(stmt);
    return leads;
}

std::optional<CrmLead> Database::get_crm_lead(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return lead_by_id_locked(db_, id);
}

bool Database::create_crm_lead(CrmLead& lead, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lead.name.empty() || lead.email.empty()) {
        error = "name and email are required";
        return false;
    }
    if (!insert_lead_locked(db_, lead, error)) return false;
    recalculate_score_locked(db_, lead.id);
    auto stored = lead_by_id_locked(db_, lead.id);
    if (stored) lead = *stored;
    return true;
}

bool Database::update_crm_lead(const CrmLead& lead, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE crm_leads SET
            name = ?, email = ?, company = ?, website = ?, phone = ?, source = ?,
            source_detail = ?, stage = ?, owner_user_id = ?, value_cents = ?, currency = ?,
            tags = ?, notes = ?, next_follow_up_at = ?, converted_at = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not update lead";
        return false;
    }
    const std::string now = utils::get_current_iso8601();
    int i = 1;
    bind_text(stmt, i++, lead.name);
    bind_text(stmt, i++, lead.email);
    bind_text(stmt, i++, lead.company);
    bind_text(stmt, i++, lead.website);
    bind_text(stmt, i++, lead.phone);
    bind_text(stmt, i++, lead.source);
    bind_text(stmt, i++, lead.source_detail);
    bind_text(stmt, i++, lead.stage);
    if (lead.owner_user_id.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.owner_user_id);
    sqlite3_bind_int(stmt, i++, lead.value_cents);
    bind_text(stmt, i++, lead.currency);
    bind_text(stmt, i++, tags_to_json(lead.tags));
    bind_text(stmt, i++, lead.notes);
    if (lead.next_follow_up_at.empty()) sqlite3_bind_null(stmt, i++); else bind_text(stmt, i++, lead.next_follow_up_at);
    if (lead.converted_at.empty()) {
        if (lead.stage == "won") sqlite3_bind_text(stmt, i++, now.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, i++);
    } else {
        bind_text(stmt, i++, lead.converted_at);
    }
    bind_text(stmt, i++, now);
    bind_text(stmt, i, lead.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    if (!ok) {
        error = "Lead not found or not changed";
        return false;
    }
    recalculate_score_locked(db_, lead.id);
    return true;
}

bool Database::delete_crm_lead(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM crm_leads WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::recalculate_crm_lead_score(const std::string& lead_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return recalculate_score_locked(db_, lead_id);
}

bool Database::create_crm_lead_from_inquiry(const PartnerInquiry& inquiry,
                                             CrmLead& lead,
                                             bool& created,
                                             std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    created = false;
    sqlite3_stmt* lookup = nullptr;
    if (!prepare(db_, std::string("SELECT ") + "id FROM crm_leads WHERE inquiry_id = ? LIMIT 1;", &lookup)) {
        error = "Could not check existing lead";
        return false;
    }
    bind_text(lookup, 1, inquiry.id);
    std::string existing_id;
    if (sqlite3_step(lookup) == SQLITE_ROW) existing_id = text_col(lookup, 0);
    finalize(lookup);
    if (!existing_id.empty()) {
        auto existing = lead_by_id_locked(db_, existing_id);
        if (!existing) {
            error = "Could not load existing lead";
            return false;
        }
        lead = *existing;
        return true;
    }

    lead.inquiry_id = inquiry.id;
    lead.name = inquiry.name;
    lead.email = inquiry.email;
    lead.company = inquiry.company;
    lead.website = inquiry.website;
    lead.source = inquiry.kind + "_inquiry";
    lead.source_detail = "partner_application";
    lead.notes = inquiry.message;
    lead.utm_source = inquiry.utm_source;
    lead.utm_medium = inquiry.utm_medium;
    lead.utm_campaign = inquiry.utm_campaign;
    lead.utm_term = inquiry.utm_term;
    lead.utm_content = inquiry.utm_content;
    lead.referrer = inquiry.referrer;
    lead.currency = "USD";
    if (!insert_lead_locked(db_, lead, error)) return false;

    CrmActivity activity;
    activity.lead_id = lead.id;
    activity.type = "system";
    activity.subject = inquiry.kind == "publisher" ? "Publisher application received"
                                                   : "Client application received";
    activity.body = inquiry.message;
    activity.occurred_at = inquiry.created_at;
    activity.created_by = inquiry.email;
    activity.metadata = nlohmann::json({
        {"inquiry_id", inquiry.id},
        {"source", inquiry.source},
        {"utm_source", inquiry.utm_source},
        {"utm_campaign", inquiry.utm_campaign}
    }).dump();
    std::string activity_error;
    insert_activity_locked(db_, activity, activity_error);
    recalculate_score_locked(db_, lead.id);
    auto stored = lead_by_id_locked(db_, lead.id);
    if (stored) lead = *stored;
    created = true;
    return true;
}

std::vector<CrmActivity> Database::list_crm_activities(const std::string& lead_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CrmActivity> activities;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, lead_id, type, subject, body, occurred_at, created_by, metadata, created_at
        FROM crm_activities WHERE lead_id = ? ORDER BY occurred_at DESC, id DESC LIMIT ?;
    )";
    if (!prepare(db_, sql, &stmt)) return activities;
    bind_text(stmt, 1, lead_id);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 500)));
    while (sqlite3_step(stmt) == SQLITE_ROW) activities.push_back(activity_from_row(stmt));
    finalize(stmt);
    return activities;
}

bool Database::create_crm_activity(CrmActivity& activity, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activity.lead_id.empty()) {
        error = "lead_id is required";
        return false;
    }
    auto lead = lead_by_id_locked(db_, activity.lead_id);
    if (!lead) {
        error = "Lead not found";
        return false;
    }
    return insert_activity_locked(db_, activity, error);
}

std::vector<CrmTask> Database::list_crm_tasks(const std::optional<std::string>& lead_id,
                                               const std::optional<std::string>& status,
                                               int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CrmTask> tasks;
    std::string sql = std::string("SELECT ") + TASK_COLUMNS + " FROM crm_tasks WHERE 1=1";
    if (lead_id.has_value()) sql += " AND lead_id = ?";
    if (status.has_value()) sql += " AND status = ?";
    sql += " ORDER BY CASE status WHEN 'open' THEN 0 ELSE 1 END, due_at, created_at LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return tasks;
    int index = 1;
    if (lead_id.has_value()) bind_text(stmt, index++, *lead_id);
    if (status.has_value()) bind_text(stmt, index++, *status);
    sqlite3_bind_int(stmt, index, std::max(1, std::min(limit, 500)));
    while (sqlite3_step(stmt) == SQLITE_ROW) tasks.push_back(task_from_row(stmt));
    finalize(stmt);
    return tasks;
}

std::optional<CrmTask> Database::get_crm_task(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + TASK_COLUMNS +
        " FROM crm_tasks WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<CrmTask> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = task_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_crm_task(CrmTask& task, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (task.id.empty()) task.id = utils::generate_uuid();
    if (task.status.empty()) task.status = "open";
    const std::string now = utils::get_current_iso8601();
    task.created_at = now;
    task.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO crm_tasks
            (id, lead_id, title, due_at, status, assigned_to, notes, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create task";
        return false;
    }
    bind_text(stmt, 1, task.id);
    bind_text(stmt, 2, task.lead_id);
    bind_text(stmt, 3, task.title);
    bind_text(stmt, 4, task.due_at);
    bind_text(stmt, 5, task.status);
    bind_text(stmt, 6, task.assigned_to);
    bind_text(stmt, 7, task.notes);
    bind_text(stmt, 8, task.created_at);
    bind_text(stmt, 9, task.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) {
        error = "Could not create task";
        return false;
    }
    refresh_next_follow_up_locked(db_, task.lead_id);
    return true;
}

bool Database::update_crm_task(const CrmTask& task, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE crm_tasks SET title = ?, due_at = ?, status = ?, assigned_to = ?,
            notes = ?, completed_at = ?, updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not update task";
        return false;
    }
    int i = 1;
    bind_text(stmt, i++, task.title);
    bind_text(stmt, i++, task.due_at);
    bind_text(stmt, i++, task.status);
    bind_text(stmt, i++, task.assigned_to);
    bind_text(stmt, i++, task.notes);
    if (task.status == "done") {
        bind_text(stmt, i++, task.completed_at.empty() ? utils::get_current_iso8601() : task.completed_at);
    } else {
        sqlite3_bind_null(stmt, i++);
    }
    bind_text(stmt, i++, utils::get_current_iso8601());
    bind_text(stmt, i, task.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    if (!ok) {
        error = "Task not found";
        return false;
    }
    refresh_next_follow_up_locked(db_, task.lead_id);
    return true;
}

nlohmann::json Database::get_crm_pipeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json rows = nlohmann::json::array();
    for (const char* stage : {"new", "contacted", "qualified", "proposal", "negotiation", "won", "lost"}) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            SELECT COUNT(*), COALESCE(SUM(value_cents), 0)
            FROM crm_leads WHERE stage = ?;
        )";
        int leads = 0;
        long long value = 0;
        if (prepare(db_, sql, &stmt)) {
            bind_text(stmt, 1, stage);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                leads = sqlite3_column_int(stmt, 0);
                value = sqlite3_column_int64(stmt, 1);
            }
            finalize(stmt);
        }
        rows.push_back({{"stage", stage}, {"leads", leads}, {"value_cents", value}});
    }
    return {{"stages", rows}};
}

nlohmann::json Database::get_crm_summary() {
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0, open = 0, won = 0, due_tasks = 0, unread = 0;
    long long pipeline_value = 0, won_value = 0;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT COUNT(*),
               COUNT(CASE WHEN stage NOT IN ('won','lost') THEN 1 END),
               COUNT(CASE WHEN stage = 'won' THEN 1 END),
               COALESCE(SUM(CASE WHEN stage NOT IN ('won','lost') THEN value_cents ELSE 0 END), 0),
               COALESCE(SUM(CASE WHEN stage = 'won' THEN value_cents ELSE 0 END), 0)
        FROM crm_leads;
    )";
    if (prepare(db_, sql, &stmt)) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = sqlite3_column_int(stmt, 0);
            open = sqlite3_column_int(stmt, 1);
            won = sqlite3_column_int(stmt, 2);
            pipeline_value = sqlite3_column_int64(stmt, 3);
            won_value = sqlite3_column_int64(stmt, 4);
        }
        finalize(stmt);
    }
    const std::string now = utils::get_current_iso8601();
    if (prepare(db_, "SELECT COUNT(*) FROM crm_tasks WHERE status = 'open' AND due_at <= ?;", &stmt)) {
        bind_text(stmt, 1, now);
        if (sqlite3_step(stmt) == SQLITE_ROW) due_tasks = sqlite3_column_int(stmt, 0);
        finalize(stmt);
    }
    (void)unread;
    return {
        {"total_leads", total},
        {"open_leads", open},
        {"won_leads", won},
        {"pipeline_value_cents", pipeline_value},
        {"won_value_cents", won_value},
        {"overdue_tasks", due_tasks}
    };
}

} // namespace adcontrol
