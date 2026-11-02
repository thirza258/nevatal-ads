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

std::vector<std::string> json_array(const std::string& raw) {
    std::vector<std::string> values;
    try {
        auto parsed = nlohmann::json::parse(raw.empty() ? "[]" : raw);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (item.is_string()) values.push_back(item.get<std::string>());
            }
        }
    } catch (...) {}
    return values;
}

std::string json_array_dump(const std::vector<std::string>& values) {
    return nlohmann::json(values).dump();
}

SeoProject project_from_row(sqlite3_stmt* stmt) {
    SeoProject project;
    project.id = text_col(stmt, 0);
    project.site_id = text_col(stmt, 1);
    project.publisher_id = text_col(stmt, 2);
    project.name = text_col(stmt, 3);
    project.domain = text_col(stmt, 4);
    project.country = text_col(stmt, 5);
    project.language = text_col(stmt, 6);
    project.status = text_col(stmt, 7);
    project.created_at = text_col(stmt, 8);
    project.updated_at = text_col(stmt, 9);
    return project;
}

const char* PROJECT_COLUMNS =
    "id, site_id, COALESCE(publisher_id, ''), name, domain, country, language, "
    "status, created_at, updated_at";

SeoKeyword keyword_from_row(sqlite3_stmt* stmt) {
    SeoKeyword keyword;
    int i = 0;
    keyword.id = text_col(stmt, i++);
    keyword.project_id = text_col(stmt, i++);
    keyword.keyword = text_col(stmt, i++);
    keyword.target_url = text_col(stmt, i++);
    keyword.country = text_col(stmt, i++);
    keyword.device = text_col(stmt, i++);
    keyword.intent = text_col(stmt, i++);
    keyword.priority = sqlite3_column_int(stmt, i++);
    keyword.search_volume = sqlite3_column_int(stmt, i++);
    keyword.difficulty = sqlite3_column_int(stmt, i++);
    keyword.current_position = sqlite3_column_int(stmt, i++);
    keyword.previous_position = sqlite3_column_int(stmt, i++);
    keyword.last_checked_at = text_col(stmt, i++);
    keyword.source = text_col(stmt, i++);
    keyword.tags = json_array(text_col(stmt, i++));
    keyword.status = text_col(stmt, i++);
    keyword.created_at = text_col(stmt, i++);
    keyword.updated_at = text_col(stmt, i++);
    return keyword;
}

const char* KEYWORD_COLUMNS =
    "id, project_id, keyword, target_url, country, device, intent, priority, "
    "search_volume, difficulty, current_position, previous_position, "
    "COALESCE(last_checked_at, ''), source, tags, status, created_at, updated_at";

std::optional<SeoKeyword> keyword_by_id_locked(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + KEYWORD_COLUMNS +
        " FROM seo_keywords WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<SeoKeyword> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = keyword_from_row(stmt);
    finalize(stmt);
    return result;
}

SeoRanking ranking_from_row(sqlite3_stmt* stmt) {
    SeoRanking ranking;
    ranking.id = text_col(stmt, 0);
    ranking.keyword_id = text_col(stmt, 1);
    ranking.position = sqlite3_column_int(stmt, 2);
    ranking.url = text_col(stmt, 3);
    ranking.checked_at = text_col(stmt, 4);
    ranking.source = text_col(stmt, 5);
    ranking.note = text_col(stmt, 6);
    ranking.created_at = text_col(stmt, 7);
    return ranking;
}

SeoCompetitor competitor_from_row(sqlite3_stmt* stmt) {
    SeoCompetitor competitor;
    competitor.id = text_col(stmt, 0);
    competitor.project_id = text_col(stmt, 1);
    competitor.domain = text_col(stmt, 2);
    competitor.name = text_col(stmt, 3);
    competitor.notes = text_col(stmt, 4);
    competitor.created_at = text_col(stmt, 5);
    competitor.updated_at = text_col(stmt, 6);
    return competitor;
}

SeoCompetitorKeyword competitor_keyword_from_row(sqlite3_stmt* stmt) {
    SeoCompetitorKeyword keyword;
    keyword.id = text_col(stmt, 0);
    keyword.competitor_id = text_col(stmt, 1);
    keyword.keyword = text_col(stmt, 2);
    keyword.position = sqlite3_column_int(stmt, 3);
    keyword.search_volume = sqlite3_column_int(stmt, 4);
    keyword.url = text_col(stmt, 5);
    keyword.checked_at = text_col(stmt, 6);
    keyword.source = text_col(stmt, 7);
    keyword.created_at = text_col(stmt, 8);
    return keyword;
}

SeoBacklink backlink_from_row(sqlite3_stmt* stmt) {
    SeoBacklink backlink;
    backlink.id = text_col(stmt, 0);
    backlink.project_id = text_col(stmt, 1);
    backlink.source_url = text_col(stmt, 2);
    backlink.target_url = text_col(stmt, 3);
    backlink.anchor_text = text_col(stmt, 4);
    backlink.domain = text_col(stmt, 5);
    backlink.status = text_col(stmt, 6);
    backlink.nofollow = sqlite3_column_int(stmt, 7) != 0;
    backlink.first_seen_at = text_col(stmt, 8);
    backlink.last_checked_at = text_col(stmt, 9);
    backlink.notes = text_col(stmt, 10);
    backlink.created_at = text_col(stmt, 11);
    backlink.updated_at = text_col(stmt, 12);
    return backlink;
}

const char* BACKLINK_COLUMNS =
    "id, project_id, source_url, target_url, anchor_text, domain, status, nofollow, "
    "first_seen_at, COALESCE(last_checked_at, ''), notes, created_at, updated_at";

SeoAuditIssue issue_from_row(sqlite3_stmt* stmt) {
    SeoAuditIssue issue;
    issue.id = text_col(stmt, 0);
    issue.audit_id = text_col(stmt, 1);
    issue.severity = text_col(stmt, 2);
    issue.category = text_col(stmt, 3);
    issue.code = text_col(stmt, 4);
    issue.message = text_col(stmt, 5);
    issue.recommendation = text_col(stmt, 6);
    issue.page_url = text_col(stmt, 7);
    return issue;
}

const char* ISSUE_COLUMNS =
    "id, audit_id, severity, category, code, message, recommendation, page_url";

SeoAudit audit_from_row(sqlite3_stmt* stmt) {
    SeoAudit audit;
    audit.id = text_col(stmt, 0);
    audit.project_id = text_col(stmt, 1);
    audit.target_url = text_col(stmt, 2);
    audit.status = text_col(stmt, 3);
    audit.score = sqlite3_column_int(stmt, 4);
    audit.checked_at = text_col(stmt, 5);
    audit.summary = text_col(stmt, 6);
    audit.created_at = text_col(stmt, 7);
    return audit;
}

const char* AUDIT_COLUMNS =
    "id, project_id, target_url, status, score, checked_at, summary, created_at";

GeoQuery geo_query_from_row(sqlite3_stmt* stmt) {
    GeoQuery query;
    query.id = text_col(stmt, 0);
    query.project_id = text_col(stmt, 1);
    query.query_text = text_col(stmt, 2);
    query.engines = json_array(text_col(stmt, 3));
    query.country = text_col(stmt, 4);
    query.status = text_col(stmt, 5);
    query.last_checked_at = text_col(stmt, 6);
    query.created_at = text_col(stmt, 7);
    query.updated_at = text_col(stmt, 8);
    return query;
}

const char* GEO_QUERY_COLUMNS =
    "id, project_id, query_text, engines, country, status, "
    "COALESCE(last_checked_at, ''), created_at, updated_at";

GeoMention geo_mention_from_row(sqlite3_stmt* stmt) {
    GeoMention mention;
    mention.id = text_col(stmt, 0);
    mention.query_id = text_col(stmt, 1);
    mention.engine = text_col(stmt, 2);
    mention.mentioned = sqlite3_column_int(stmt, 3) != 0;
    mention.position = sqlite3_column_int(stmt, 4);
    mention.sentiment = text_col(stmt, 5);
    mention.snippet = text_col(stmt, 6);
    mention.checked_at = text_col(stmt, 7);
    mention.source = text_col(stmt, 8);
    mention.created_at = text_col(stmt, 9);
    return mention;
}

const char* GEO_MENTION_COLUMNS =
    "id, query_id, engine, mentioned, position, sentiment, snippet, checked_at, source, created_at";

} // namespace

std::vector<SeoProject> Database::list_seo_projects(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& site_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoProject> projects;
    std::string sql = std::string("SELECT ") + PROJECT_COLUMNS + " FROM seo_projects WHERE 1=1";
    if (publisher_id.has_value()) sql += " AND publisher_id = ?";
    if (site_id.has_value()) sql += " AND site_id = ?";
    sql += " ORDER BY created_at DESC, id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return projects;
    int index = 1;
    if (publisher_id.has_value()) bind_text(stmt, index++, *publisher_id);
    if (site_id.has_value()) bind_text(stmt, index++, *site_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) projects.push_back(project_from_row(stmt));
    finalize(stmt);
    return projects;
}

std::optional<SeoProject> Database::get_seo_project(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + PROJECT_COLUMNS +
        " FROM seo_projects WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<SeoProject> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = project_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_seo_project(SeoProject& project, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (project.id.empty()) project.id = utils::generate_uuid();
    if (project.country.empty()) project.country = "US";
    if (project.language.empty()) project.language = "en";
    if (project.status.empty()) project.status = "active";
    const std::string now = utils::get_current_iso8601();
    project.created_at = now;
    project.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO seo_projects
            (id, site_id, publisher_id, name, domain, country, language, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create SEO project";
        return false;
    }
    bind_text(stmt, 1, project.id);
    bind_text(stmt, 2, project.site_id);
    if (project.publisher_id.empty()) sqlite3_bind_null(stmt, 3); else bind_text(stmt, 3, project.publisher_id);
    bind_text(stmt, 4, project.name);
    bind_text(stmt, 5, project.domain);
    bind_text(stmt, 6, project.country);
    bind_text(stmt, 7, project.language);
    bind_text(stmt, 8, project.status);
    bind_text(stmt, 9, project.created_at);
    bind_text(stmt, 10, project.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create SEO project";
    return ok;
}

bool Database::update_seo_project(const SeoProject& project) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE seo_projects SET name = ?, domain = ?, country = ?, language = ?,
            status = ?, updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, project.name);
    bind_text(stmt, 2, project.domain);
    bind_text(stmt, 3, project.country);
    bind_text(stmt, 4, project.language);
    bind_text(stmt, 5, project.status);
    bind_text(stmt, 6, utils::get_current_iso8601());
    bind_text(stmt, 7, project.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_seo_project(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM seo_projects WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

std::vector<SeoKeyword> Database::list_seo_keywords(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoKeyword> keywords;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + KEYWORD_COLUMNS +
        " FROM seo_keywords WHERE project_id = ? ORDER BY priority, current_position = 0, current_position, keyword;";
    if (!prepare(db_, sql, &stmt)) return keywords;
    bind_text(stmt, 1, project_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) keywords.push_back(keyword_from_row(stmt));
    finalize(stmt);
    return keywords;
}

std::optional<SeoKeyword> Database::get_seo_keyword(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return keyword_by_id_locked(db_, id);
}

bool Database::create_seo_keyword(SeoKeyword& keyword, std::string& error) {
    std::vector<SeoKeyword> one{keyword};
    if (!create_seo_keywords(one, error)) return false;
    keyword = one[0];
    return true;
}

bool Database::create_seo_keywords(std::vector<SeoKeyword>& keywords, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    const std::string now = utils::get_current_iso8601();
    for (auto& keyword : keywords) {
        if (keyword.id.empty()) keyword.id = utils::generate_uuid();
        if (keyword.country.empty()) keyword.country = "US";
        if (keyword.device.empty()) keyword.device = "desktop";
        if (keyword.intent.empty()) keyword.intent = "informational";
        if (keyword.status.empty()) keyword.status = "active";
        if (keyword.source.empty()) keyword.source = "manual";
        keyword.created_at = now;
        keyword.updated_at = now;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            INSERT INTO seo_keywords
                (id, project_id, keyword, target_url, country, device, intent, priority,
                 search_volume, difficulty, current_position, previous_position, source,
                 tags, status, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        if (!prepare(db_, sql, &stmt)) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            error = "Could not create keyword";
            return false;
        }
        int i = 1;
        bind_text(stmt, i++, keyword.id);
        bind_text(stmt, i++, keyword.project_id);
        bind_text(stmt, i++, keyword.keyword);
        bind_text(stmt, i++, keyword.target_url);
        bind_text(stmt, i++, keyword.country);
        bind_text(stmt, i++, keyword.device);
        bind_text(stmt, i++, keyword.intent);
        sqlite3_bind_int(stmt, i++, keyword.priority);
        sqlite3_bind_int(stmt, i++, keyword.search_volume);
        sqlite3_bind_int(stmt, i++, keyword.difficulty);
        sqlite3_bind_int(stmt, i++, keyword.current_position);
        sqlite3_bind_int(stmt, i++, keyword.previous_position);
        bind_text(stmt, i++, keyword.source);
        bind_text(stmt, i++, json_array_dump(keyword.tags));
        bind_text(stmt, i++, keyword.status);
        bind_text(stmt, i++, keyword.created_at);
        bind_text(stmt, i, keyword.updated_at);
        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
        if (!ok) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            error = "Could not create keyword";
            return false;
        }
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::update_seo_keyword(const SeoKeyword& keyword) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE seo_keywords SET keyword = ?, target_url = ?, country = ?, device = ?,
            intent = ?, priority = ?, search_volume = ?, difficulty = ?, status = ?,
            tags = ?, updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    int i = 1;
    bind_text(stmt, i++, keyword.keyword);
    bind_text(stmt, i++, keyword.target_url);
    bind_text(stmt, i++, keyword.country);
    bind_text(stmt, i++, keyword.device);
    bind_text(stmt, i++, keyword.intent);
    sqlite3_bind_int(stmt, i++, keyword.priority);
    sqlite3_bind_int(stmt, i++, keyword.search_volume);
    sqlite3_bind_int(stmt, i++, keyword.difficulty);
    bind_text(stmt, i++, keyword.status);
    bind_text(stmt, i++, json_array_dump(keyword.tags));
    bind_text(stmt, i++, utils::get_current_iso8601());
    bind_text(stmt, i, keyword.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_seo_keyword(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM seo_keywords WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::record_seo_ranking(SeoKeyword& keyword, SeoRanking& ranking, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    auto existing = keyword_by_id_locked(db_, keyword.id);
    if (!existing) {
        error = "Keyword not found";
        return false;
    }
    const std::string now = utils::get_current_iso8601();
    if (ranking.id.empty()) ranking.id = utils::generate_uuid();
    if (ranking.checked_at.empty()) ranking.checked_at = now;
    if (ranking.source.empty()) ranking.source = "manual";
    ranking.keyword_id = keyword.id;
    ranking.created_at = now;

    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* insert = R"(
        INSERT INTO seo_rankings
            (id, keyword_id, position, url, checked_at, source, note, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    bool ok = prepare(db_, insert, &stmt);
    if (ok) {
        bind_text(stmt, 1, ranking.id);
        bind_text(stmt, 2, ranking.keyword_id);
        sqlite3_bind_int(stmt, 3, ranking.position);
        bind_text(stmt, 4, ranking.url);
        bind_text(stmt, 5, ranking.checked_at);
        bind_text(stmt, 6, ranking.source);
        bind_text(stmt, 7, ranking.note);
        bind_text(stmt, 8, ranking.created_at);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
    }
    if (ok) {
        const char* update = R"(
            UPDATE seo_keywords SET previous_position = current_position,
                current_position = ?, last_checked_at = ?, updated_at = ? WHERE id = ?;
        )";
        ok = prepare(db_, update, &stmt);
        if (ok) {
            sqlite3_bind_int(stmt, 1, ranking.position);
            bind_text(stmt, 2, ranking.checked_at);
            bind_text(stmt, 3, now);
            bind_text(stmt, 4, keyword.id);
            ok = sqlite3_step(stmt) == SQLITE_DONE;
            finalize(stmt);
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not record ranking";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    keyword = *existing;
    keyword.previous_position = existing->current_position;
    keyword.current_position = ranking.position;
    keyword.last_checked_at = ranking.checked_at;
    keyword.updated_at = now;
    return true;
}

std::vector<SeoRanking> Database::list_seo_rankings(const std::string& keyword_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoRanking> rankings;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, keyword_id, position, url, checked_at, source, note, created_at
        FROM seo_rankings WHERE keyword_id = ? ORDER BY checked_at DESC, id DESC LIMIT ?;
    )";
    if (!prepare(db_, sql, &stmt)) return rankings;
    bind_text(stmt, 1, keyword_id);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 500)));
    while (sqlite3_step(stmt) == SQLITE_ROW) rankings.push_back(ranking_from_row(stmt));
    finalize(stmt);
    return rankings;
}

std::vector<SeoCompetitor> Database::list_seo_competitors(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoCompetitor> competitors;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, project_id, domain, name, notes, created_at, updated_at "
                      "FROM seo_competitors WHERE project_id = ? ORDER BY created_at;";
    if (!prepare(db_, sql, &stmt)) return competitors;
    bind_text(stmt, 1, project_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) competitors.push_back(competitor_from_row(stmt));
    finalize(stmt);
    return competitors;
}

std::optional<SeoCompetitor> Database::get_seo_competitor(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "SELECT id, project_id, domain, name, notes, created_at, updated_at "
                       "FROM seo_competitors WHERE id = ? LIMIT 1;", &stmt)) {
        return std::nullopt;
    }
    bind_text(stmt, 1, id);
    std::optional<SeoCompetitor> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = competitor_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_seo_competitor(SeoCompetitor& competitor, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (competitor.id.empty()) competitor.id = utils::generate_uuid();
    const std::string now = utils::get_current_iso8601();
    competitor.created_at = now;
    competitor.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO seo_competitors "
                      "(id, project_id, domain, name, notes, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create competitor";
        return false;
    }
    bind_text(stmt, 1, competitor.id);
    bind_text(stmt, 2, competitor.project_id);
    bind_text(stmt, 3, competitor.domain);
    bind_text(stmt, 4, competitor.name);
    bind_text(stmt, 5, competitor.notes);
    bind_text(stmt, 6, competitor.created_at);
    bind_text(stmt, 7, competitor.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create competitor";
    return ok;
}

bool Database::update_seo_competitor(const SeoCompetitor& competitor) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE seo_competitors SET domain = ?, name = ?, notes = ?, updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, competitor.domain);
    bind_text(stmt, 2, competitor.name);
    bind_text(stmt, 3, competitor.notes);
    bind_text(stmt, 4, utils::get_current_iso8601());
    bind_text(stmt, 5, competitor.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_seo_competitor(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM seo_competitors WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

std::vector<SeoCompetitorKeyword> Database::list_seo_competitor_keywords(const std::string& competitor_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoCompetitorKeyword> keywords;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, competitor_id, keyword, position, search_volume, url,
               COALESCE(checked_at, ''), source, created_at
        FROM seo_competitor_keywords WHERE competitor_id = ? ORDER BY position = 0, position, keyword;
    )";
    if (!prepare(db_, sql, &stmt)) return keywords;
    bind_text(stmt, 1, competitor_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) keywords.push_back(competitor_keyword_from_row(stmt));
    finalize(stmt);
    return keywords;
}

bool Database::create_seo_competitor_keyword(SeoCompetitorKeyword& keyword, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (keyword.id.empty()) keyword.id = utils::generate_uuid();
    if (keyword.source.empty()) keyword.source = "manual";
    keyword.created_at = utils::get_current_iso8601();
    if (keyword.checked_at.empty()) keyword.checked_at = keyword.created_at;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO seo_competitor_keywords
            (id, competitor_id, keyword, position, search_volume, url, checked_at, source, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create competitor keyword";
        return false;
    }
    bind_text(stmt, 1, keyword.id);
    bind_text(stmt, 2, keyword.competitor_id);
    bind_text(stmt, 3, keyword.keyword);
    sqlite3_bind_int(stmt, 4, keyword.position);
    sqlite3_bind_int(stmt, 5, keyword.search_volume);
    bind_text(stmt, 6, keyword.url);
    bind_text(stmt, 7, keyword.checked_at);
    bind_text(stmt, 8, keyword.source);
    bind_text(stmt, 9, keyword.created_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create competitor keyword";
    return ok;
}

bool Database::delete_seo_competitor_keyword(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM seo_competitor_keywords WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

std::vector<SeoBacklink> Database::list_seo_backlinks(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoBacklink> backlinks;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + BACKLINK_COLUMNS +
        " FROM seo_backlinks WHERE project_id = ? ORDER BY first_seen_at DESC;";
    if (!prepare(db_, sql, &stmt)) return backlinks;
    bind_text(stmt, 1, project_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) backlinks.push_back(backlink_from_row(stmt));
    finalize(stmt);
    return backlinks;
}

std::optional<SeoBacklink> Database::get_seo_backlink(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + BACKLINK_COLUMNS +
        " FROM seo_backlinks WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<SeoBacklink> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = backlink_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_seo_backlink(SeoBacklink& backlink, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (backlink.id.empty()) backlink.id = utils::generate_uuid();
    const std::string now = utils::get_current_iso8601();
    if (backlink.first_seen_at.empty()) backlink.first_seen_at = now;
    if (backlink.status.empty()) backlink.status = "active";
    backlink.created_at = now;
    backlink.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO seo_backlinks
            (id, project_id, source_url, target_url, anchor_text, domain, status, nofollow,
             first_seen_at, last_checked_at, notes, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create backlink";
        return false;
    }
    bind_text(stmt, 1, backlink.id);
    bind_text(stmt, 2, backlink.project_id);
    bind_text(stmt, 3, backlink.source_url);
    bind_text(stmt, 4, backlink.target_url);
    bind_text(stmt, 5, backlink.anchor_text);
    bind_text(stmt, 6, backlink.domain);
    bind_text(stmt, 7, backlink.status);
    sqlite3_bind_int(stmt, 8, backlink.nofollow ? 1 : 0);
    bind_text(stmt, 9, backlink.first_seen_at);
    if (backlink.last_checked_at.empty()) sqlite3_bind_null(stmt, 10);
    else bind_text(stmt, 10, backlink.last_checked_at);
    bind_text(stmt, 11, backlink.notes);
    bind_text(stmt, 12, backlink.created_at);
    bind_text(stmt, 13, backlink.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create backlink";
    return ok;
}

bool Database::update_seo_backlink(const SeoBacklink& backlink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE seo_backlinks SET source_url = ?, target_url = ?, anchor_text = ?, domain = ?,
            status = ?, nofollow = ?, last_checked_at = ?, notes = ?, updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    int i = 1;
    bind_text(stmt, i++, backlink.source_url);
    bind_text(stmt, i++, backlink.target_url);
    bind_text(stmt, i++, backlink.anchor_text);
    bind_text(stmt, i++, backlink.domain);
    bind_text(stmt, i++, backlink.status);
    sqlite3_bind_int(stmt, i++, backlink.nofollow ? 1 : 0);
    if (backlink.last_checked_at.empty()) sqlite3_bind_null(stmt, i++);
    else bind_text(stmt, i++, backlink.last_checked_at);
    bind_text(stmt, i++, backlink.notes);
    bind_text(stmt, i++, utils::get_current_iso8601());
    bind_text(stmt, i, backlink.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_seo_backlink(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM seo_backlinks WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::create_seo_audit(SeoAudit& audit, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (audit.id.empty()) audit.id = utils::generate_uuid();
    if (audit.checked_at.empty()) audit.checked_at = utils::get_current_iso8601();
    audit.created_at = utils::get_current_iso8601();
    if (audit.status.empty()) audit.status = "completed";
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO seo_audits
            (id, project_id, target_url, status, score, checked_at, summary, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    bool ok = prepare(db_, sql, &stmt);
    if (ok) {
        bind_text(stmt, 1, audit.id);
        bind_text(stmt, 2, audit.project_id);
        bind_text(stmt, 3, audit.target_url);
        bind_text(stmt, 4, audit.status);
        sqlite3_bind_int(stmt, 5, audit.score);
        bind_text(stmt, 6, audit.checked_at);
        bind_text(stmt, 7, audit.summary);
        bind_text(stmt, 8, audit.created_at);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
    }
    if (ok) {
        for (auto& issue : audit.issues) {
            if (issue.id.empty()) issue.id = utils::generate_uuid();
            issue.audit_id = audit.id;
            const char* issue_sql = R"(
                INSERT INTO seo_audit_issues
                    (id, audit_id, severity, category, code, message, recommendation, page_url)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )";
            ok = prepare(db_, issue_sql, &stmt);
            if (!ok) break;
            bind_text(stmt, 1, issue.id);
            bind_text(stmt, 2, issue.audit_id);
            bind_text(stmt, 3, issue.severity);
            bind_text(stmt, 4, issue.category);
            bind_text(stmt, 5, issue.code);
            bind_text(stmt, 6, issue.message);
            bind_text(stmt, 7, issue.recommendation);
            bind_text(stmt, 8, issue.page_url);
            ok = sqlite3_step(stmt) == SQLITE_DONE;
            finalize(stmt);
            if (!ok) break;
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not save SEO audit";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<SeoAudit> Database::list_seo_audits(const std::string& project_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SeoAudit> audits;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + AUDIT_COLUMNS +
        " FROM seo_audits WHERE project_id = ? ORDER BY checked_at DESC LIMIT ?;";
    if (!prepare(db_, sql, &stmt)) return audits;
    bind_text(stmt, 1, project_id);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 100)));
    while (sqlite3_step(stmt) == SQLITE_ROW) audits.push_back(audit_from_row(stmt));
    finalize(stmt);
    return audits;
}

std::optional<SeoAudit> Database::get_seo_audit(const std::string& id, bool include_issues) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + AUDIT_COLUMNS +
        " FROM seo_audits WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<SeoAudit> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = audit_from_row(stmt);
    finalize(stmt);
    if (result && include_issues) {
        const std::string issue_sql = std::string("SELECT ") + ISSUE_COLUMNS +
            " FROM seo_audit_issues WHERE audit_id = ? ORDER BY CASE severity "
            "WHEN 'critical' THEN 0 WHEN 'high' THEN 1 WHEN 'medium' THEN 2 "
            "WHEN 'low' THEN 3 ELSE 4 END, code;";
        if (prepare(db_, issue_sql, &stmt)) {
            bind_text(stmt, 1, id);
            while (sqlite3_step(stmt) == SQLITE_ROW) result->issues.push_back(issue_from_row(stmt));
            finalize(stmt);
        }
    }
    return result;
}

std::vector<GeoQuery> Database::list_geo_queries(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GeoQuery> queries;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + GEO_QUERY_COLUMNS +
        " FROM geo_queries WHERE project_id = ? ORDER BY created_at;";
    if (!prepare(db_, sql, &stmt)) return queries;
    bind_text(stmt, 1, project_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) queries.push_back(geo_query_from_row(stmt));
    finalize(stmt);
    return queries;
}

std::optional<GeoQuery> Database::get_geo_query(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + GEO_QUERY_COLUMNS +
        " FROM geo_queries WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<GeoQuery> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = geo_query_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_geo_query(GeoQuery& query, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (query.id.empty()) query.id = utils::generate_uuid();
    if (query.country.empty()) query.country = "US";
    if (query.status.empty()) query.status = "active";
    const std::string now = utils::get_current_iso8601();
    query.created_at = now;
    query.updated_at = now;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO geo_queries
            (id, project_id, query_text, engines, country, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create GEO query";
        return false;
    }
    bind_text(stmt, 1, query.id);
    bind_text(stmt, 2, query.project_id);
    bind_text(stmt, 3, query.query_text);
    bind_text(stmt, 4, json_array_dump(query.engines));
    bind_text(stmt, 5, query.country);
    bind_text(stmt, 6, query.status);
    bind_text(stmt, 7, query.created_at);
    bind_text(stmt, 8, query.updated_at);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    finalize(stmt);
    if (!ok) error = "Could not create GEO query";
    return ok;
}

bool Database::update_geo_query(const GeoQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE geo_queries SET query_text = ?, engines = ?, country = ?, status = ?,
            last_checked_at = ?, updated_at = ? WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, query.query_text);
    bind_text(stmt, 2, json_array_dump(query.engines));
    bind_text(stmt, 3, query.country);
    bind_text(stmt, 4, query.status);
    if (query.last_checked_at.empty()) sqlite3_bind_null(stmt, 5);
    else bind_text(stmt, 5, query.last_checked_at);
    bind_text(stmt, 6, utils::get_current_iso8601());
    bind_text(stmt, 7, query.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::delete_geo_query(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, "DELETE FROM geo_queries WHERE id = ?;", &stmt)) return false;
    bind_text(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    finalize(stmt);
    return ok;
}

bool Database::create_geo_mention(GeoMention& mention, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (mention.id.empty()) mention.id = utils::generate_uuid();
    if (mention.engine.empty()) {
        error = "engine is required";
        return false;
    }
    if (mention.source.empty()) mention.source = "manual";
    if (mention.checked_at.empty()) mention.checked_at = utils::get_current_iso8601();
    mention.created_at = utils::get_current_iso8601();
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO geo_mentions
            (id, query_id, engine, mentioned, position, sentiment, snippet, checked_at, source, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    bool ok = prepare(db_, sql, &stmt);
    if (ok) {
        bind_text(stmt, 1, mention.id);
        bind_text(stmt, 2, mention.query_id);
        bind_text(stmt, 3, mention.engine);
        sqlite3_bind_int(stmt, 4, mention.mentioned ? 1 : 0);
        sqlite3_bind_int(stmt, 5, mention.position);
        bind_text(stmt, 6, mention.sentiment);
        bind_text(stmt, 7, mention.snippet);
        bind_text(stmt, 8, mention.checked_at);
        bind_text(stmt, 9, mention.source);
        bind_text(stmt, 10, mention.created_at);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        finalize(stmt);
    }
    if (ok) {
        sqlite3_stmt* update = nullptr;
        if (prepare(db_, "UPDATE geo_queries SET last_checked_at = ?, updated_at = ? WHERE id = ?;",
                    &update)) {
            bind_text(update, 1, mention.checked_at);
            bind_text(update, 2, mention.created_at);
            bind_text(update, 3, mention.query_id);
            ok = sqlite3_step(update) == SQLITE_DONE;
            finalize(update);
        } else {
            ok = false;
        }
    }
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not save GEO mention";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<GeoMention> Database::list_geo_mentions(const std::string& query_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GeoMention> mentions;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("SELECT ") + GEO_MENTION_COLUMNS +
        " FROM geo_mentions WHERE query_id = ? ORDER BY checked_at DESC, id DESC LIMIT ?;";
    if (!prepare(db_, sql, &stmt)) return mentions;
    bind_text(stmt, 1, query_id);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 500)));
    while (sqlite3_step(stmt) == SQLITE_ROW) mentions.push_back(geo_mention_from_row(stmt));
    finalize(stmt);
    return mentions;
}

nlohmann::json Database::get_seo_summary(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    int keywords = 0, tracked = 0, top10 = 0, improved = 0, declined = 0;
    double avg_position = 0.0;
    sqlite3_stmt* stmt = nullptr;
    const char* keyword_sql = R"(
        SELECT COUNT(*),
               COUNT(CASE WHEN current_position > 0 THEN 1 END),
               COUNT(CASE WHEN current_position BETWEEN 1 AND 10 THEN 1 END),
               COUNT(CASE WHEN previous_position > 0 AND current_position > 0
                           AND current_position < previous_position THEN 1 END),
               COUNT(CASE WHEN previous_position > 0 AND current_position > 0
                           AND current_position > previous_position THEN 1 END),
               COALESCE(AVG(CASE WHEN current_position > 0 THEN current_position END), 0)
        FROM seo_keywords WHERE project_id = ?;
    )";
    if (prepare(db_, keyword_sql, &stmt)) {
        bind_text(stmt, 1, project_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            keywords = sqlite3_column_int(stmt, 0);
            tracked = sqlite3_column_int(stmt, 1);
            top10 = sqlite3_column_int(stmt, 2);
            improved = sqlite3_column_int(stmt, 3);
            declined = sqlite3_column_int(stmt, 4);
            avg_position = sqlite3_column_double(stmt, 5);
        }
        finalize(stmt);
    }

    int backlinks = 0, active_backlinks = 0, lost_backlinks = 0, competitors = 0;
    const char* backlink_sql = R"(
        SELECT COUNT(*),
               COUNT(CASE WHEN status = 'active' THEN 1 END),
               COUNT(CASE WHEN status = 'lost' THEN 1 END)
        FROM seo_backlinks WHERE project_id = ?;
    )";
    stmt = nullptr;
    if (prepare(db_, backlink_sql, &stmt)) {
        bind_text(stmt, 1, project_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            backlinks = sqlite3_column_int(stmt, 0);
            active_backlinks = sqlite3_column_int(stmt, 1);
            lost_backlinks = sqlite3_column_int(stmt, 2);
        }
        finalize(stmt);
    }
    stmt = nullptr;
    if (prepare(db_, "SELECT COUNT(*) FROM seo_competitors WHERE project_id = ?;", &stmt)) {
        bind_text(stmt, 1, project_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) competitors = sqlite3_column_int(stmt, 0);
        finalize(stmt);
    }

    int latest_audit_score = 0;
    std::string latest_audit_at;
    stmt = nullptr;
    if (prepare(db_, "SELECT score, checked_at FROM seo_audits WHERE project_id = ? "
                     "ORDER BY checked_at DESC LIMIT 1;", &stmt)) {
        bind_text(stmt, 1, project_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            latest_audit_score = sqlite3_column_int(stmt, 0);
            latest_audit_at = text_col(stmt, 1);
        }
        finalize(stmt);
    }

    int geo_checks = 0, geo_mentions = 0;
    const char* geo_sql = R"(
        SELECT COUNT(*), COUNT(CASE WHEN m.mentioned = 1 THEN 1 END)
        FROM geo_mentions m
        JOIN geo_queries q ON m.query_id = q.id
        WHERE q.project_id = ?;
    )";
    stmt = nullptr;
    if (prepare(db_, geo_sql, &stmt)) {
        bind_text(stmt, 1, project_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            geo_checks = sqlite3_column_int(stmt, 0);
            geo_mentions = sqlite3_column_int(stmt, 1);
        }
        finalize(stmt);
    }

    return {
        {"keywords", keywords},
        {"tracked_keywords", tracked},
        {"top10_keywords", top10},
        {"improved_keywords", improved},
        {"declined_keywords", declined},
        {"average_position", avg_position},
        {"backlinks", backlinks},
        {"active_backlinks", active_backlinks},
        {"lost_backlinks", lost_backlinks},
        {"competitors", competitors},
        {"latest_audit_score", latest_audit_score},
        {"latest_audit_at", latest_audit_at},
        {"geo_checks", geo_checks},
        {"geo_mentions", geo_mentions},
        {"geo_visibility_percent", geo_checks > 0
            ? (static_cast<double>(geo_mentions) / static_cast<double>(geo_checks)) * 100.0 : 0.0}
    };
}

} // namespace adcontrol
