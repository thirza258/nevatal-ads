#include "db.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>

namespace adcontrol {

namespace {

constexpr int LEARNING_REVIEW_INTERVAL_DAYS = 180;

std::string text_col(sqlite3_stmt* stmt, int index) {
    const unsigned char* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

int int_col(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_int(stmt, index);
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

std::string outcomes_to_json(const std::vector<std::string>& outcomes) {
    return nlohmann::json(outcomes).dump();
}

std::vector<std::string> outcomes_from_json(const std::string& raw) {
    try {
        auto parsed = nlohmann::json::parse(raw);
        if (parsed.is_array()) {
            std::vector<std::string> result;
            for (const auto& item : parsed) {
                if (item.is_string()) result.push_back(item.get<std::string>());
            }
            return result;
        }
    } catch (...) {}
    return {};
}

const char* COURSE_SELECT =
    "SELECT c.id, c.slug, c.topic_id, COALESCE(t.slug, ''), COALESCE(t.title, ''), "
    "c.title, c.summary, c.description_md, c.level, c.duration_minutes, c.outcomes, "
    "c.status, COALESCE(c.author_id, ''), COALESCE(au.email, ''), "
    "COALESCE(c.reviewer_id, ''), COALESCE(ru.email, ''), c.version, "
    "COALESCE(c.published_at, ''), COALESCE(c.last_reviewed_at, ''), "
    "COALESCE(c.next_review_due_at, ''), c.created_at, c.updated_at "
    "FROM learning_courses c "
    "LEFT JOIN learning_topics t ON t.id = c.topic_id "
    "LEFT JOIN users au ON au.id = c.author_id "
    "LEFT JOIN users ru ON ru.id = c.reviewer_id ";

LearningCourse course_from_row(sqlite3_stmt* stmt) {
    LearningCourse course;
    course.id = text_col(stmt, 0);
    course.slug = text_col(stmt, 1);
    course.topic_id = text_col(stmt, 2);
    course.topic_slug = text_col(stmt, 3);
    course.topic_title = text_col(stmt, 4);
    course.title = text_col(stmt, 5);
    course.summary = text_col(stmt, 6);
    course.description_md = text_col(stmt, 7);
    course.level = text_col(stmt, 8);
    course.duration_minutes = int_col(stmt, 9);
    course.outcomes = outcomes_from_json(text_col(stmt, 10));
    course.status = text_col(stmt, 11);
    course.author_id = text_col(stmt, 12);
    course.author_email = text_col(stmt, 13);
    course.reviewer_id = text_col(stmt, 14);
    course.reviewer_email = text_col(stmt, 15);
    course.version = int_col(stmt, 16);
    course.published_at = text_col(stmt, 17);
    course.last_reviewed_at = text_col(stmt, 18);
    course.next_review_due_at = text_col(stmt, 19);
    course.created_at = text_col(stmt, 20);
    course.updated_at = text_col(stmt, 21);
    return course;
}

LearningTopic topic_from_row(sqlite3_stmt* stmt) {
    LearningTopic topic;
    topic.id = text_col(stmt, 0);
    topic.slug = text_col(stmt, 1);
    topic.title = text_col(stmt, 2);
    topic.description = text_col(stmt, 3);
    topic.status = text_col(stmt, 4);
    topic.sort_order = int_col(stmt, 5);
    topic.created_at = text_col(stmt, 6);
    topic.updated_at = text_col(stmt, 7);
    return topic;
}

LearningModule module_from_row(sqlite3_stmt* stmt) {
    LearningModule module;
    module.id = text_col(stmt, 0);
    module.course_id = text_col(stmt, 1);
    module.title = text_col(stmt, 2);
    module.summary = text_col(stmt, 3);
    module.sort_order = int_col(stmt, 4);
    module.status = text_col(stmt, 5);
    module.created_at = text_col(stmt, 6);
    module.updated_at = text_col(stmt, 7);
    return module;
}

LearningLesson lesson_from_row(sqlite3_stmt* stmt) {
    LearningLesson lesson;
    lesson.id = text_col(stmt, 0);
    lesson.course_id = text_col(stmt, 1);
    lesson.module_id = text_col(stmt, 2);
    lesson.slug = text_col(stmt, 3);
    lesson.title = text_col(stmt, 4);
    lesson.content_md = text_col(stmt, 5);
    lesson.estimated_minutes = int_col(stmt, 6);
    lesson.sort_order = int_col(stmt, 7);
    lesson.is_preview = int_col(stmt, 8) != 0;
    lesson.status = text_col(stmt, 9);
    lesson.video_provider = text_col(stmt, 10);
    lesson.video_id = text_col(stmt, 11);
    lesson.created_at = text_col(stmt, 12);
    lesson.updated_at = text_col(stmt, 13);
    return lesson;
}

LearningResource resource_from_row(sqlite3_stmt* stmt) {
    LearningResource resource;
    resource.id = text_col(stmt, 0);
    resource.course_id = text_col(stmt, 1);
    resource.lesson_id = text_col(stmt, 2);
    resource.title = text_col(stmt, 3);
    resource.description = text_col(stmt, 4);
    resource.url = text_col(stmt, 5);
    resource.kind = text_col(stmt, 6);
    resource.sort_order = int_col(stmt, 7);
    resource.created_at = text_col(stmt, 8);
    return resource;
}

Enrollment enrollment_from_row(sqlite3_stmt* stmt) {
    Enrollment enrollment;
    enrollment.id = text_col(stmt, 0);
    enrollment.user_id = text_col(stmt, 1);
    enrollment.course_id = text_col(stmt, 2);
    enrollment.status = text_col(stmt, 3);
    enrollment.last_lesson_id = text_col(stmt, 4);
    enrollment.enrolled_at = text_col(stmt, 5);
    enrollment.completed_at = text_col(stmt, 6);
    enrollment.updated_at = text_col(stmt, 7);
    enrollment.total_lessons = int_col(stmt, 8);
    enrollment.completed_lessons = int_col(stmt, 9);
    return enrollment;
}

const char* ENROLLMENT_SELECT =
    "SELECT e.id, e.user_id, e.course_id, e.status, COALESCE(e.last_lesson_id, ''), "
    "e.enrolled_at, COALESCE(e.completed_at, ''), e.updated_at, "
    "(SELECT COUNT(*) FROM learning_lessons l WHERE l.course_id = e.course_id AND l.status = 'active'), "
    "(SELECT COUNT(*) FROM learning_lesson_progress p "
    " JOIN learning_lessons l ON l.id = p.lesson_id "
    " WHERE p.enrollment_id = e.id AND l.status = 'active') "
    "FROM learning_enrollments e ";

std::optional<LearningCourse> load_course_by_id(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(COURSE_SELECT) + "WHERE c.id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningCourse> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = course_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<LearningCourse> load_course_by_slug(sqlite3* db, const std::string& slug) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(COURSE_SELECT) + "WHERE c.slug = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, slug);
    std::optional<LearningCourse> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = course_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<LearningModule> load_module(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, course_id, title, summary, sort_order, status, created_at, updated_at "
        "FROM learning_modules WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningModule> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = module_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<LearningLesson> load_lesson(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, course_id, module_id, slug, title, content_md, estimated_minutes, "
        "sort_order, is_preview, status, COALESCE(video_provider, ''), COALESCE(video_id, ''), "
        "created_at, updated_at FROM learning_lessons WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningLesson> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = lesson_from_row(stmt);
    finalize(stmt);
    return result;
}

std::vector<LearningModule> load_modules(sqlite3* db, const std::string& course_id, bool include_archived) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql =
        "SELECT id, course_id, title, summary, sort_order, status, created_at, updated_at "
        "FROM learning_modules WHERE course_id = ? ";
    if (!include_archived) sql += "AND status = 'active' ";
    sql += "ORDER BY sort_order ASC, created_at ASC;";
    std::vector<LearningModule> modules;
    if (!prepare(db, sql, &stmt)) return modules;
    bind_text(stmt, 1, course_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) modules.push_back(module_from_row(stmt));
    finalize(stmt);
    return modules;
}

std::vector<LearningLesson> load_lessons(sqlite3* db, const std::string& course_id, bool include_archived) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql =
        "SELECT id, course_id, module_id, slug, title, content_md, estimated_minutes, "
        "sort_order, is_preview, status, COALESCE(video_provider, ''), COALESCE(video_id, ''), "
        "created_at, updated_at FROM learning_lessons WHERE course_id = ? ";
    if (!include_archived) sql += "AND status = 'active' ";
    sql += "ORDER BY sort_order ASC, created_at ASC;";
    std::vector<LearningLesson> lessons;
    if (!prepare(db, sql, &stmt)) return lessons;
    bind_text(stmt, 1, course_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) lessons.push_back(lesson_from_row(stmt));
    finalize(stmt);
    return lessons;
}

std::vector<LearningResource> load_resources(sqlite3* db, const std::string& course_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, course_id, COALESCE(lesson_id, ''), title, description, url, kind, "
        "sort_order, created_at FROM learning_resources WHERE course_id = ? "
        "ORDER BY sort_order ASC, created_at ASC;";
    std::vector<LearningResource> resources;
    if (!prepare(db, sql, &stmt)) return resources;
    bind_text(stmt, 1, course_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) resources.push_back(resource_from_row(stmt));
    finalize(stmt);
    return resources;
}

std::optional<LessonQuiz> load_quiz(sqlite3* db, const std::string& lesson_id, bool include_answers) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, lesson_id, course_id, title, passing_score, created_at, updated_at "
        "FROM learning_quizzes WHERE lesson_id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, lesson_id);
    LessonQuiz quiz;
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        quiz.id = text_col(stmt, 0);
        quiz.lesson_id = text_col(stmt, 1);
        quiz.course_id = text_col(stmt, 2);
        quiz.title = text_col(stmt, 3);
        quiz.passing_score = int_col(stmt, 4);
        quiz.created_at = text_col(stmt, 5);
        quiz.updated_at = text_col(stmt, 6);
    }
    finalize(stmt);
    if (!found) return std::nullopt;

    sqlite3_stmt* qstmt = nullptr;
    const char* qsql =
        "SELECT id, quiz_id, prompt, explanation, sort_order "
        "FROM learning_quiz_questions WHERE quiz_id = ? ORDER BY sort_order ASC, id ASC;";
    if (!prepare(db, qsql, &qstmt)) return std::nullopt;
    bind_text(qstmt, 1, quiz.id);
    while (sqlite3_step(qstmt) == SQLITE_ROW) {
        QuizQuestion question;
        question.id = text_col(qstmt, 0);
        question.quiz_id = text_col(qstmt, 1);
        question.prompt = text_col(qstmt, 2);
        question.explanation = text_col(qstmt, 3);
        question.sort_order = int_col(qstmt, 4);
        quiz.questions.push_back(question);
    }
    finalize(qstmt);

    for (auto& question : quiz.questions) {
        sqlite3_stmt* ostmt = nullptr;
        std::string osql = std::string(
            "SELECT id, question_id, option_text, is_correct, sort_order "
            "FROM learning_quiz_options WHERE question_id = ? ORDER BY sort_order ASC, id ASC;");
        if (!prepare(db, osql, &ostmt)) continue;
        bind_text(ostmt, 1, question.id);
        while (sqlite3_step(ostmt) == SQLITE_ROW) {
            QuizOption option;
            option.id = text_col(ostmt, 0);
            option.question_id = text_col(ostmt, 1);
            option.text = text_col(ostmt, 2);
            option.is_correct = int_col(ostmt, 3) != 0;
            option.sort_order = int_col(ostmt, 4);
            if (!include_answers) option.is_correct = false;
            question.options.push_back(option);
        }
        finalize(ostmt);
    }
    return quiz;
}

bool audit(Database& db, const std::string& actor, const std::string& action,
           const std::string& entity_type, const std::string& entity_id,
           const nlohmann::json& details) {
    AuditLog log;
    log.id = utils::generate_uuid();
    log.actor = actor;
    log.action = action;
    log.entity_type = entity_type;
    log.entity_id = entity_id;
    log.details = details.dump();
    log.created_at = utils::get_current_iso8601();
    return db.record_audit_log(log);
}

} // namespace

bool Database::create_user(User& user, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        error = "Database is not available";
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO users (id, email, password_hash, salt, role, publisher_id, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create account";
        return false;
    }
    bind_text(stmt, 1, user.id);
    bind_text(stmt, 2, user.email);
    bind_text(stmt, 3, user.password_hash);
    bind_text(stmt, 4, user.salt);
    bind_text(stmt, 5, user.role);
    if (user.publisher_id.empty()) sqlite3_bind_null(stmt, 6);
    else bind_text(stmt, 6, user.publisher_id);
    bind_text(stmt, 7, user.created_at);
    bind_text(stmt, 8, user.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::string message = sqlite3_errmsg(db_);
        if (message.find("UNIQUE") != std::string::npos) {
            error = "An account with this email already exists";
        } else {
            error = "Could not create account";
        }
        return false;
    }
    return true;
}

std::vector<User> Database::list_learning_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<User> users;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, password_hash, salt, role, COALESCE(publisher_id, ''), created_at, updated_at "
        "FROM users WHERE role IN ('learner', 'instructor', 'editor') ORDER BY created_at DESC;";
    if (!prepare(db_, sql, &stmt)) return users;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = text_col(stmt, 0);
        user.email = text_col(stmt, 1);
        user.password_hash = text_col(stmt, 2);
        user.salt = text_col(stmt, 3);
        user.role = text_col(stmt, 4);
        user.publisher_id = text_col(stmt, 5);
        user.created_at = text_col(stmt, 6);
        user.updated_at = text_col(stmt, 7);
        users.push_back(user);
    }
    finalize(stmt);
    return users;
}

bool Database::delete_learning_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM users WHERE id = ? AND role = 'learner';";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, user_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::vector<LearningTopic> Database::list_learning_topics(bool published_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LearningTopic> topics;
    sqlite3_stmt* stmt = nullptr;
    std::string sql =
        "SELECT id, slug, title, description, status, sort_order, created_at, updated_at "
        "FROM learning_topics ";
    if (published_only) sql += "WHERE status = 'published' ";
    sql += "ORDER BY sort_order ASC, title ASC;";
    if (!prepare(db_, sql, &stmt)) return topics;
    while (sqlite3_step(stmt) == SQLITE_ROW) topics.push_back(topic_from_row(stmt));
    finalize(stmt);
    return topics;
}

std::optional<LearningTopic> Database::get_learning_topic(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, slug, title, description, status, sort_order, created_at, updated_at "
        "FROM learning_topics WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningTopic> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = topic_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<LearningTopic> Database::get_learning_topic_by_slug(const std::string& slug) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, slug, title, description, status, sort_order, created_at, updated_at "
        "FROM learning_topics WHERE slug = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, slug);
    std::optional<LearningTopic> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = topic_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_learning_topic(LearningTopic& topic, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_topics (id, slug, title, description, status, sort_order, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create topic";
        return false;
    }
    bind_text(stmt, 1, topic.id);
    bind_text(stmt, 2, topic.slug);
    bind_text(stmt, 3, topic.title);
    bind_text(stmt, 4, topic.description);
    bind_text(stmt, 5, topic.status);
    sqlite3_bind_int(stmt, 6, topic.sort_order);
    bind_text(stmt, 7, topic.created_at);
    bind_text(stmt, 8, topic.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool Database::update_learning_topic(const LearningTopic& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE learning_topics SET slug = ?, title = ?, description = ?, status = ?, "
        "sort_order = ?, updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, topic.slug);
    bind_text(stmt, 2, topic.title);
    bind_text(stmt, 3, topic.description);
    bind_text(stmt, 4, topic.status);
    sqlite3_bind_int(stmt, 5, topic.sort_order);
    bind_text(stmt, 6, topic.updated_at);
    bind_text(stmt, 7, topic.id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::learning_topic_has_courses(const std::string& topic_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM learning_courses WHERE topic_id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return true;
    bind_text(stmt, 1, topic_id);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    finalize(stmt);
    return found;
}

std::vector<LearningCourse> Database::list_learning_courses(
    const std::optional<std::string>& status,
    const std::optional<std::string>& topic_id,
    const std::optional<std::string>& author_id,
    const std::string& query,
    int limit,
    int offset,
    bool published_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LearningCourse> courses;
    std::string sql = std::string(COURSE_SELECT) + "WHERE 1 = 1 ";
    if (published_only) sql += "AND c.status = 'published' ";
    if (status.has_value() && !status->empty()) sql += "AND c.status = ? ";
    if (topic_id.has_value() && !topic_id->empty()) sql += "AND c.topic_id = ? ";
    if (author_id.has_value() && !author_id->empty()) sql += "AND c.author_id = ? ";
    if (!query.empty()) {
        sql += "AND (LOWER(c.title) LIKE ? OR LOWER(c.summary) LIKE ?) ";
    }
    sql += "ORDER BY c.published_at DESC, c.updated_at DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return courses;
    int idx = 1;
    if (status.has_value() && !status->empty()) bind_text(stmt, idx++, *status);
    if (topic_id.has_value() && !topic_id->empty()) bind_text(stmt, idx++, *topic_id);
    if (author_id.has_value() && !author_id->empty()) bind_text(stmt, idx++, *author_id);
    if (!query.empty()) {
        std::string pattern = "%" + utils::to_lower_copy(query) + "%";
        bind_text(stmt, idx++, pattern);
        bind_text(stmt, idx++, pattern);
    }
    sqlite3_bind_int(stmt, idx++, std::clamp(limit, 1, 200));
    sqlite3_bind_int(stmt, idx, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) courses.push_back(course_from_row(stmt));
    finalize(stmt);
    return courses;
}

std::optional<LearningCourse> Database::get_learning_course(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_course_by_id(db_, id);
}

std::optional<LearningCourse> Database::get_learning_course_by_slug(const std::string& slug) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_course_by_slug(db_, slug);
}

bool Database::create_learning_course(LearningCourse& course, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_courses (id, slug, topic_id, title, summary, description_md, level, "
        "duration_minutes, outcomes, status, author_id, reviewer_id, version, published_at, "
        "last_reviewed_at, next_review_due_at, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create course";
        return false;
    }
    bind_text(stmt, 1, course.id);
    bind_text(stmt, 2, course.slug);
    bind_text(stmt, 3, course.topic_id);
    bind_text(stmt, 4, course.title);
    bind_text(stmt, 5, course.summary);
    bind_text(stmt, 6, course.description_md);
    bind_text(stmt, 7, course.level);
    sqlite3_bind_int(stmt, 8, course.duration_minutes);
    bind_text(stmt, 9, outcomes_to_json(course.outcomes));
    bind_text(stmt, 10, course.status);
    bind_text(stmt, 11, course.author_id);
    if (course.reviewer_id.empty()) sqlite3_bind_null(stmt, 12);
    else bind_text(stmt, 12, course.reviewer_id);
    sqlite3_bind_int(stmt, 13, course.version);
    if (course.published_at.empty()) sqlite3_bind_null(stmt, 14);
    else bind_text(stmt, 14, course.published_at);
    if (course.last_reviewed_at.empty()) sqlite3_bind_null(stmt, 15);
    else bind_text(stmt, 15, course.last_reviewed_at);
    if (course.next_review_due_at.empty()) sqlite3_bind_null(stmt, 16);
    else bind_text(stmt, 16, course.next_review_due_at);
    bind_text(stmt, 17, course.created_at);
    bind_text(stmt, 18, course.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::string message = sqlite3_errmsg(db_);
        error = message.find("UNIQUE") != std::string::npos
                    ? "A course with this slug already exists"
                    : "Could not create course";
        return false;
    }
    return true;
}

bool Database::update_learning_course(const LearningCourse& course) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE learning_courses SET slug = ?, topic_id = ?, title = ?, summary = ?, "
        "description_md = ?, level = ?, duration_minutes = ?, outcomes = ?, version = ?, "
        "updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, course.slug);
    bind_text(stmt, 2, course.topic_id);
    bind_text(stmt, 3, course.title);
    bind_text(stmt, 4, course.summary);
    bind_text(stmt, 5, course.description_md);
    bind_text(stmt, 6, course.level);
    sqlite3_bind_int(stmt, 7, course.duration_minutes);
    bind_text(stmt, 8, outcomes_to_json(course.outcomes));
    sqlite3_bind_int(stmt, 9, course.version);
    bind_text(stmt, 10, course.updated_at);
    bind_text(stmt, 11, course.id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::set_learning_course_status(const std::string& course_id,
                                          const std::string& status,
                                          const std::string& reviewer_id,
                                          const std::string& now_iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE learning_courses SET status = ?, reviewer_id = ?, "
        "published_at = CASE WHEN ? = 'published' THEN COALESCE(published_at, ?) ELSE published_at END, "
        "last_reviewed_at = CASE WHEN ? = 'published' THEN ? ELSE last_reviewed_at END, "
        "next_review_due_at = CASE WHEN ? = 'published' THEN ? ELSE next_review_due_at END, "
        "updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    std::time_t published_epoch = utils::utc_now_epoch();
    auto parsed = utils::normalize_iso8601_utc(now_iso);
    std::time_t parsed_epoch = 0;
    if (!parsed.empty() && utils::parse_iso8601_utc(parsed, parsed_epoch)) published_epoch = parsed_epoch;
    const std::string next_review = utils::iso8601_from_epoch(
        published_epoch + static_cast<std::time_t>(LEARNING_REVIEW_INTERVAL_DAYS) * 86400);

    bind_text(stmt, 1, status);
    if (reviewer_id.empty()) sqlite3_bind_null(stmt, 2);
    else bind_text(stmt, 2, reviewer_id);
    bind_text(stmt, 3, status);
    bind_text(stmt, 4, now_iso);
    bind_text(stmt, 5, status);
    bind_text(stmt, 6, now_iso);
    bind_text(stmt, 7, status);
    bind_text(stmt, 8, next_review);
    bind_text(stmt, 9, now_iso);
    bind_text(stmt, 10, course_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::archive_learning_course(const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE learning_courses SET status = 'archived', updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, utils::get_current_iso8601());
    bind_text(stmt, 2, course_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

int Database::count_learning_lessons(const std::string& course_id, bool active_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT COUNT(*) FROM learning_lessons WHERE course_id = ? ";
    if (active_only) sql += "AND status = 'active';";
    if (!prepare(db_, sql, &stmt)) return 0;
    bind_text(stmt, 1, course_id);
    int count = sqlite3_step(stmt) == SQLITE_ROW ? int_col(stmt, 0) : 0;
    finalize(stmt);
    return count;
}

int Database::count_learning_modules(const std::string& course_id, bool active_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT COUNT(*) FROM learning_modules WHERE course_id = ? ";
    if (active_only) sql += "AND status = 'active';";
    if (!prepare(db_, sql, &stmt)) return 0;
    bind_text(stmt, 1, course_id);
    int count = sqlite3_step(stmt) == SQLITE_ROW ? int_col(stmt, 0) : 0;
    finalize(stmt);
    return count;
}

std::vector<LearningModule> Database::list_learning_modules(const std::string& course_id,
                                                           bool include_archived) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_modules(db_, course_id, include_archived);
}

std::optional<LearningModule> Database::get_learning_module(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_module(db_, id);
}

bool Database::create_learning_module(LearningModule& module, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_modules (id, course_id, title, summary, sort_order, status, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create module";
        return false;
    }
    bind_text(stmt, 1, module.id);
    bind_text(stmt, 2, module.course_id);
    bind_text(stmt, 3, module.title);
    bind_text(stmt, 4, module.summary);
    sqlite3_bind_int(stmt, 5, module.sort_order);
    bind_text(stmt, 6, module.status);
    bind_text(stmt, 7, module.created_at);
    bind_text(stmt, 8, module.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not create module";
        return false;
    }
    return true;
}

bool Database::update_learning_module(const LearningModule& module) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE learning_modules SET title = ?, summary = ?, sort_order = ?, updated_at = ? "
        "WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, module.title);
    bind_text(stmt, 2, module.summary);
    sqlite3_bind_int(stmt, 3, module.sort_order);
    bind_text(stmt, 4, module.updated_at);
    bind_text(stmt, 5, module.id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::archive_learning_module(const std::string& module_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE learning_modules SET status = 'archived', updated_at = ? WHERE id = ?;";
    bool ok = false;
    if (prepare(db_, sql, &stmt)) {
        bind_text(stmt, 1, now);
        bind_text(stmt, 2, module_id);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
        finalize(stmt);
    }
    if (ok) {
        sqlite3_stmt* lesson_stmt = nullptr;
        const char* lesson_sql =
            "UPDATE learning_lessons SET status = 'archived', updated_at = ? WHERE module_id = ?;";
        if (prepare(db_, lesson_sql, &lesson_stmt)) {
            bind_text(lesson_stmt, 1, now);
            bind_text(lesson_stmt, 2, module_id);
            sqlite3_step(lesson_stmt);
            finalize(lesson_stmt);
        }
    }
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

bool Database::learning_module_has_lessons(const std::string& module_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT 1 FROM learning_lessons WHERE module_id = ? AND status = 'active' LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return true;
    bind_text(stmt, 1, module_id);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    finalize(stmt);
    return found;
}

std::vector<LearningLesson> Database::list_learning_lessons(const std::string& course_id,
                                                            bool include_archived) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_lessons(db_, course_id, include_archived);
}

std::optional<LearningLesson> Database::get_learning_lesson(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_lesson(db_, id);
}

std::optional<LearningLesson> Database::get_learning_lesson_by_slug(const std::string& course_id,
                                                                     const std::string& slug) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, course_id, module_id, slug, title, content_md, estimated_minutes, "
        "sort_order, is_preview, status, COALESCE(video_provider, ''), COALESCE(video_id, ''), "
        "created_at, updated_at FROM learning_lessons "
        "WHERE course_id = ? AND slug = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, course_id);
    bind_text(stmt, 2, slug);
    std::optional<LearningLesson> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = lesson_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_learning_lesson(LearningLesson& lesson, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_lessons (id, course_id, module_id, slug, title, content_md, "
        "estimated_minutes, sort_order, is_preview, status, video_provider, video_id, "
        "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create lesson";
        return false;
    }
    bind_text(stmt, 1, lesson.id);
    bind_text(stmt, 2, lesson.course_id);
    bind_text(stmt, 3, lesson.module_id);
    bind_text(stmt, 4, lesson.slug);
    bind_text(stmt, 5, lesson.title);
    bind_text(stmt, 6, lesson.content_md);
    sqlite3_bind_int(stmt, 7, lesson.estimated_minutes);
    sqlite3_bind_int(stmt, 8, lesson.sort_order);
    sqlite3_bind_int(stmt, 9, lesson.is_preview ? 1 : 0);
    bind_text(stmt, 10, lesson.status);
    if (lesson.video_provider.empty()) sqlite3_bind_null(stmt, 11);
    else bind_text(stmt, 11, lesson.video_provider);
    if (lesson.video_id.empty()) sqlite3_bind_null(stmt, 12);
    else bind_text(stmt, 12, lesson.video_id);
    bind_text(stmt, 13, lesson.created_at);
    bind_text(stmt, 14, lesson.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::string message = sqlite3_errmsg(db_);
        error = message.find("UNIQUE") != std::string::npos
                    ? "A lesson with this slug already exists in this course"
                    : "Could not create lesson";
        return false;
    }
    return true;
}

bool Database::update_learning_lesson(const LearningLesson& lesson) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE learning_lessons SET slug = ?, title = ?, content_md = ?, estimated_minutes = ?, "
        "sort_order = ?, is_preview = ?, video_provider = ?, video_id = ?, updated_at = ? "
        "WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, lesson.slug);
    bind_text(stmt, 2, lesson.title);
    bind_text(stmt, 3, lesson.content_md);
    sqlite3_bind_int(stmt, 4, lesson.estimated_minutes);
    sqlite3_bind_int(stmt, 5, lesson.sort_order);
    sqlite3_bind_int(stmt, 6, lesson.is_preview ? 1 : 0);
    if (lesson.video_provider.empty()) sqlite3_bind_null(stmt, 7);
    else bind_text(stmt, 7, lesson.video_provider);
    if (lesson.video_id.empty()) sqlite3_bind_null(stmt, 8);
    else bind_text(stmt, 8, lesson.video_id);
    bind_text(stmt, 9, lesson.updated_at);
    bind_text(stmt, 10, lesson.id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Database::archive_learning_lesson(const std::string& lesson_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE learning_lessons SET status = 'archived', updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, utils::get_current_iso8601());
    bind_text(stmt, 2, lesson_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::vector<LearningResource> Database::list_learning_resources(const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_resources(db_, course_id);
}

std::optional<LearningResource> Database::get_learning_resource(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, course_id, COALESCE(lesson_id, ''), title, description, url, kind, "
        "sort_order, created_at FROM learning_resources WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningResource> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = resource_from_row(stmt);
    finalize(stmt);
    return result;
}

bool Database::create_learning_resource(LearningResource& resource, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_resources (id, course_id, lesson_id, title, description, url, kind, "
        "sort_order, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create resource";
        return false;
    }
    bind_text(stmt, 1, resource.id);
    bind_text(stmt, 2, resource.course_id);
    if (resource.lesson_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, resource.lesson_id);
    bind_text(stmt, 4, resource.title);
    bind_text(stmt, 5, resource.description);
    bind_text(stmt, 6, resource.url);
    bind_text(stmt, 7, resource.kind);
    sqlite3_bind_int(stmt, 8, resource.sort_order);
    bind_text(stmt, 9, resource.created_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not create resource";
        return false;
    }
    return true;
}

bool Database::delete_learning_resource(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM learning_resources WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::optional<LessonQuiz> Database::get_learning_quiz_for_lesson(const std::string& lesson_id,
                                                                 bool include_answers) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_quiz(db_, lesson_id, include_answers);
}

bool Database::replace_learning_quiz(const std::string& lesson_id,
                                     const std::string& course_id,
                                     LessonQuiz& quiz,
                                     std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();
    sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

    sqlite3_stmt* existing = nullptr;
    const char* find_sql = "SELECT id FROM learning_quizzes WHERE lesson_id = ? LIMIT 1;";
    std::string quiz_id = quiz.id;
    bool exists = false;
    if (prepare(db_, find_sql, &existing)) {
        bind_text(existing, 1, lesson_id);
        if (sqlite3_step(existing) == SQLITE_ROW) {
            exists = true;
            quiz_id = text_col(existing, 0);
        }
        finalize(existing);
    }
    if (quiz_id.empty()) quiz_id = utils::generate_uuid();

    bool ok = true;
    if (exists) {
        sqlite3_stmt* update = nullptr;
        const char* update_sql =
            "UPDATE learning_quizzes SET title = ?, passing_score = ?, updated_at = ? WHERE id = ?;";
        if (prepare(db_, update_sql, &update)) {
            bind_text(update, 1, quiz.title);
            sqlite3_bind_int(update, 2, quiz.passing_score);
            bind_text(update, 3, now);
            bind_text(update, 4, quiz_id);
            ok = sqlite3_step(update) == SQLITE_DONE;
            finalize(update);
        } else {
            ok = false;
        }
        if (ok) {
            sqlite3_stmt* clear = nullptr;
            if (prepare(db_, "DELETE FROM learning_quiz_questions WHERE quiz_id = ?;", &clear)) {
                bind_text(clear, 1, quiz_id);
                ok = sqlite3_step(clear) == SQLITE_DONE;
                finalize(clear);
            } else {
                ok = false;
            }
        }
    } else {
        sqlite3_stmt* insert = nullptr;
        const char* insert_sql =
            "INSERT INTO learning_quizzes (id, lesson_id, course_id, title, passing_score, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);";
        if (prepare(db_, insert_sql, &insert)) {
            bind_text(insert, 1, quiz_id);
            bind_text(insert, 2, lesson_id);
            bind_text(insert, 3, course_id);
            bind_text(insert, 4, quiz.title);
            sqlite3_bind_int(insert, 5, quiz.passing_score);
            bind_text(insert, 6, now);
            bind_text(insert, 7, now);
            ok = sqlite3_step(insert) == SQLITE_DONE;
            finalize(insert);
        } else {
            ok = false;
        }
    }

    if (ok) {
        for (size_t qi = 0; qi < quiz.questions.size() && ok; ++qi) {
            auto& question = quiz.questions[qi];
            if (question.id.empty()) question.id = utils::generate_uuid();
            question.quiz_id = quiz_id;
            sqlite3_stmt* qinsert = nullptr;
            const char* qsql =
                "INSERT INTO learning_quiz_questions (id, quiz_id, prompt, explanation, sort_order) "
                "VALUES (?, ?, ?, ?, ?);";
            if (!prepare(db_, qsql, &qinsert)) { ok = false; break; }
            bind_text(qinsert, 1, question.id);
            bind_text(qinsert, 2, quiz_id);
            bind_text(qinsert, 3, question.prompt);
            bind_text(qinsert, 4, question.explanation);
            sqlite3_bind_int(qinsert, 5, static_cast<int>(qi));
            ok = sqlite3_step(qinsert) == SQLITE_DONE;
            finalize(qinsert);
            if (!ok) break;

            for (size_t oi = 0; oi < question.options.size() && ok; ++oi) {
                auto& option = question.options[oi];
                if (option.id.empty()) option.id = utils::generate_uuid();
                option.question_id = question.id;
                sqlite3_stmt* oinsert = nullptr;
                const char* osql =
                    "INSERT INTO learning_quiz_options (id, question_id, option_text, is_correct, sort_order) "
                    "VALUES (?, ?, ?, ?, ?);";
                if (!prepare(db_, osql, &oinsert)) { ok = false; break; }
                bind_text(oinsert, 1, option.id);
                bind_text(oinsert, 2, question.id);
                bind_text(oinsert, 3, option.text);
                sqlite3_bind_int(oinsert, 4, option.is_correct ? 1 : 0);
                sqlite3_bind_int(oinsert, 5, static_cast<int>(oi));
                ok = sqlite3_step(oinsert) == SQLITE_DONE;
                finalize(oinsert);
            }
        }
    }

    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        error = "Could not save quiz";
        return false;
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    quiz.id = quiz_id;
    quiz.lesson_id = lesson_id;
    quiz.course_id = course_id;
    quiz.created_at = now;
    quiz.updated_at = now;
    return true;
}

bool Database::delete_learning_quiz(const std::string& lesson_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM learning_quizzes WHERE lesson_id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, lesson_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::vector<LearningPath> Database::list_learning_paths(bool published_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LearningPath> paths;
    std::string sql =
        "SELECT id, slug, title, description, level, status, sort_order, COALESCE(published_at, ''), "
        "created_at, updated_at FROM learning_paths ";
    if (published_only) sql += "WHERE status = 'published' ";
    sql += "ORDER BY sort_order ASC, title ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return paths;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LearningPath path;
        path.id = text_col(stmt, 0);
        path.slug = text_col(stmt, 1);
        path.title = text_col(stmt, 2);
        path.description = text_col(stmt, 3);
        path.level = text_col(stmt, 4);
        path.status = text_col(stmt, 5);
        path.sort_order = int_col(stmt, 6);
        path.published_at = text_col(stmt, 7);
        path.created_at = text_col(stmt, 8);
        path.updated_at = text_col(stmt, 9);
        paths.push_back(path);
    }
    finalize(stmt);
    return paths;
}

std::optional<LearningPath> Database::get_learning_path(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, slug, title, description, level, status, sort_order, COALESCE(published_at, ''), "
        "created_at, updated_at FROM learning_paths WHERE id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<LearningPath> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        LearningPath path;
        path.id = text_col(stmt, 0);
        path.slug = text_col(stmt, 1);
        path.title = text_col(stmt, 2);
        path.description = text_col(stmt, 3);
        path.level = text_col(stmt, 4);
        path.status = text_col(stmt, 5);
        path.sort_order = int_col(stmt, 6);
        path.published_at = text_col(stmt, 7);
        path.created_at = text_col(stmt, 8);
        path.updated_at = text_col(stmt, 9);
        result = path;
    }
    finalize(stmt);
    return result;
}

std::optional<LearningPath> Database::get_learning_path_by_slug(const std::string& slug) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, slug, title, description, level, status, sort_order, COALESCE(published_at, ''), "
        "created_at, updated_at FROM learning_paths WHERE slug = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, slug);
    std::optional<LearningPath> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        LearningPath path;
        path.id = text_col(stmt, 0);
        path.slug = text_col(stmt, 1);
        path.title = text_col(stmt, 2);
        path.description = text_col(stmt, 3);
        path.level = text_col(stmt, 4);
        path.status = text_col(stmt, 5);
        path.sort_order = int_col(stmt, 6);
        path.published_at = text_col(stmt, 7);
        path.created_at = text_col(stmt, 8);
        path.updated_at = text_col(stmt, 9);
        result = path;
    }
    finalize(stmt);
    return result;
}

std::vector<LearningCourse> Database::list_learning_path_courses(const std::string& path_id,
                                                                 bool published_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LearningCourse> courses;
    std::string sql = std::string(COURSE_SELECT) +
        "JOIN learning_path_courses pc ON pc.course_id = c.id WHERE pc.path_id = ? ";
    if (published_only) sql += "AND c.status = 'published' ";
    sql += "ORDER BY pc.sort_order ASC, c.title ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return courses;
    bind_text(stmt, 1, path_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) courses.push_back(course_from_row(stmt));
    finalize(stmt);
    return courses;
}

std::optional<Enrollment> Database::get_learning_enrollment(const std::string& user_id,
                                                             const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = std::string(ENROLLMENT_SELECT) + "WHERE e.user_id = ? AND e.course_id = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, user_id);
    bind_text(stmt, 2, course_id);
    std::optional<Enrollment> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = enrollment_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<Enrollment> Database::get_learning_enrollment_by_id(const std::string& enrollment_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = std::string(ENROLLMENT_SELECT) + "WHERE e.id = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, enrollment_id);
    std::optional<Enrollment> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = enrollment_from_row(stmt);
    finalize(stmt);
    return result;
}

std::vector<Enrollment> Database::list_learning_enrollments(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Enrollment> enrollments;
    std::string sql = std::string(ENROLLMENT_SELECT) +
        "WHERE e.user_id = ? ORDER BY e.updated_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return enrollments;
    bind_text(stmt, 1, user_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) enrollments.push_back(enrollment_from_row(stmt));
    finalize(stmt);
    return enrollments;
}

bool Database::enroll_in_learning_course(const std::string& user_id,
                                          const std::string& course_id,
                                          Enrollment& enrollment,
                                          bool& created,
                                          std::string& error) {
    created = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string select_sql = std::string(ENROLLMENT_SELECT) +
            "WHERE e.user_id = ? AND e.course_id = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare(db_, select_sql, &stmt)) {
            bind_text(stmt, 1, user_id);
            bind_text(stmt, 2, course_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) enrollment = enrollment_from_row(stmt);
            finalize(stmt);
            if (!enrollment.id.empty()) return true;
        }

        std::string now = utils::get_current_iso8601();
        Enrollment fresh;
        fresh.id = utils::generate_uuid();
        fresh.user_id = user_id;
        fresh.course_id = course_id;
        fresh.status = "active";
        fresh.enrolled_at = now;
        fresh.updated_at = now;

        const char* insert_sql =
            "INSERT INTO learning_enrollments (id, user_id, course_id, status, enrolled_at, updated_at) "
            "VALUES (?, ?, ?, 'active', ?, ?);";
        sqlite3_stmt* insert = nullptr;
        if (!prepare(db_, insert_sql, &insert)) {
            error = "Could not enroll in this course";
            return false;
        }
        bind_text(insert, 1, fresh.id);
        bind_text(insert, 2, user_id);
        bind_text(insert, 3, course_id);
        bind_text(insert, 4, now);
        bind_text(insert, 5, now);
        int rc = sqlite3_step(insert);
        finalize(insert);
        if (rc != SQLITE_DONE) {
            // A concurrent request won the unique constraint; return that row.
            sqlite3_stmt* retry = nullptr;
            if (prepare(db_, select_sql, &retry)) {
                bind_text(retry, 1, user_id);
                bind_text(retry, 2, course_id);
                if (sqlite3_step(retry) == SQLITE_ROW) enrollment = enrollment_from_row(retry);
                finalize(retry);
                if (!enrollment.id.empty()) return true;
            }
            error = "Could not enroll in this course";
            return false;
        }
        created = true;
        enrollment = fresh;
    }
    // Re-read to attach current lesson counts without calling another locked method.
    auto reloaded = get_learning_enrollment(user_id, course_id);
    if (reloaded) enrollment = *reloaded;
    return true;
}

bool Database::complete_learning_lesson(const std::string& enrollment_id,
                                         const std::string& course_id,
                                         const std::string& lesson_id,
                                         Enrollment& enrollment,
                                         std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = utils::get_current_iso8601();

    sqlite3_stmt* lesson_stmt = nullptr;
    const char* lesson_sql =
        "SELECT 1 FROM learning_lessons WHERE id = ? AND course_id = ? AND status = 'active' LIMIT 1;";
    if (!prepare(db_, lesson_sql, &lesson_stmt)) {
        error = "Could not update progress";
        return false;
    }
    bind_text(lesson_stmt, 1, lesson_id);
    bind_text(lesson_stmt, 2, course_id);
    bool lesson_valid = sqlite3_step(lesson_stmt) == SQLITE_ROW;
    finalize(lesson_stmt);
    if (!lesson_valid) {
        error = "Lesson is not part of this course";
        return false;
    }

    bool found = false;
    std::string status;
    sqlite3_stmt* check = nullptr;
    if (prepare(db_, "SELECT status FROM learning_enrollments WHERE id = ? AND course_id = ? LIMIT 1;", &check)) {
        bind_text(check, 1, enrollment_id);
        bind_text(check, 2, course_id);
        if (sqlite3_step(check) == SQLITE_ROW) {
            found = true;
            status = text_col(check, 0);
        }
        finalize(check);
    }
    if (!found) {
        error = "Enrollment not found";
        return false;
    }

    sqlite3_stmt* progress = nullptr;
    const char* progress_sql =
        "INSERT OR IGNORE INTO learning_lesson_progress (id, enrollment_id, lesson_id, completed_at, created_at) "
        "VALUES (?, ?, ?, ?, ?);";
    if (!prepare(db_, progress_sql, &progress)) {
        error = "Could not update progress";
        return false;
    }
    bind_text(progress, 1, utils::generate_uuid());
    bind_text(progress, 2, enrollment_id);
    bind_text(progress, 3, lesson_id);
    bind_text(progress, 4, now);
    bind_text(progress, 5, now);
    bool progress_ok = sqlite3_step(progress) == SQLITE_DONE;
    finalize(progress);
    if (!progress_ok) {
        error = "Could not update progress";
        return false;
    }

    int total = 0;
    int completed = 0;
    sqlite3_stmt* counts = nullptr;
    const char* counts_sql =
        "SELECT "
        "(SELECT COUNT(*) FROM learning_lessons WHERE course_id = ? AND status = 'active'), "
        "(SELECT COUNT(*) FROM learning_lesson_progress p JOIN learning_lessons l ON l.id = p.lesson_id "
        " WHERE p.enrollment_id = ? AND l.status = 'active');";
    if (prepare(db_, counts_sql, &counts)) {
        bind_text(counts, 1, course_id);
        bind_text(counts, 2, enrollment_id);
        if (sqlite3_step(counts) == SQLITE_ROW) {
            total = int_col(counts, 0);
            completed = int_col(counts, 1);
        }
        finalize(counts);
    }

    bool should_complete = status != "completed" && total > 0 && completed >= total;
    sqlite3_stmt* update = nullptr;
    const char* update_sql =
        "UPDATE learning_enrollments SET last_lesson_id = ?, status = ?, completed_at = ?, updated_at = ? "
        "WHERE id = ?;";
    if (!prepare(db_, update_sql, &update)) {
        error = "Could not update progress";
        return false;
    }
    bind_text(update, 1, lesson_id);
    bind_text(update, 2, should_complete ? "completed" : status);
    if (should_complete) bind_text(update, 3, now);
    else sqlite3_bind_null(update, 3);
    bind_text(update, 4, now);
    bind_text(update, 5, enrollment_id);
    int rc = sqlite3_step(update);
    finalize(update);
    if (rc != SQLITE_DONE) {
        error = "Could not update progress";
        return false;
    }

    std::string select_sql = std::string(ENROLLMENT_SELECT) + "WHERE e.id = ? LIMIT 1;";
    sqlite3_stmt* reload = nullptr;
    if (prepare(db_, select_sql, &reload)) {
        bind_text(reload, 1, enrollment_id);
        if (sqlite3_step(reload) == SQLITE_ROW) enrollment = enrollment_from_row(reload);
        finalize(reload);
    }
    return true;
}

bool Database::touch_enrollment_lesson(const std::string& enrollment_id,
                                        const std::string& lesson_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE learning_enrollments SET last_lesson_id = ?, updated_at = ? WHERE id = ?;";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, lesson_id);
    bind_text(stmt, 2, utils::get_current_iso8601());
    bind_text(stmt, 3, enrollment_id);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::vector<std::string> Database::list_completed_lesson_ids(const std::string& enrollment_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> lesson_ids;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT lesson_id FROM learning_lesson_progress WHERE enrollment_id = ? "
        "ORDER BY completed_at ASC;";
    if (!prepare(db_, sql, &stmt)) return lesson_ids;
    bind_text(stmt, 1, enrollment_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) lesson_ids.push_back(text_col(stmt, 0));
    finalize(stmt);
    return lesson_ids;
}

bool Database::record_quiz_attempt(QuizAttempt& attempt) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (attempt.id.empty()) attempt.id = utils::generate_uuid();
    if (attempt.attempted_at.empty()) attempt.attempted_at = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_quiz_attempts (id, enrollment_id, user_id, course_id, lesson_id, quiz_id, "
        "score, passed, correct_count, total_questions, attempted_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, attempt.id);
    bind_text(stmt, 2, attempt.enrollment_id);
    bind_text(stmt, 3, attempt.user_id);
    bind_text(stmt, 4, attempt.course_id);
    bind_text(stmt, 5, attempt.lesson_id);
    bind_text(stmt, 6, attempt.quiz_id);
    sqlite3_bind_int(stmt, 7, attempt.score);
    sqlite3_bind_int(stmt, 8, attempt.passed ? 1 : 0);
    sqlite3_bind_int(stmt, 9, attempt.correct_count);
    sqlite3_bind_int(stmt, 10, attempt.total_questions);
    bind_text(stmt, 11, attempt.attempted_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<QuizAttempt> Database::list_quiz_attempts(const std::string& user_id,
                                                       const std::string& course_id,
                                                       const std::string& lesson_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<QuizAttempt> attempts;
    std::string sql =
        "SELECT id, enrollment_id, user_id, course_id, lesson_id, quiz_id, score, passed, "
        "correct_count, total_questions, attempted_at FROM learning_quiz_attempts WHERE user_id = ? ";
    if (!course_id.empty()) sql += "AND course_id = ? ";
    if (!lesson_id.empty()) sql += "AND lesson_id = ? ";
    sql += "ORDER BY attempted_at DESC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return attempts;
    int idx = 1;
    bind_text(stmt, idx++, user_id);
    if (!course_id.empty()) bind_text(stmt, idx++, course_id);
    if (!lesson_id.empty()) bind_text(stmt, idx++, lesson_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QuizAttempt attempt;
        attempt.id = text_col(stmt, 0);
        attempt.enrollment_id = text_col(stmt, 1);
        attempt.user_id = text_col(stmt, 2);
        attempt.course_id = text_col(stmt, 3);
        attempt.lesson_id = text_col(stmt, 4);
        attempt.quiz_id = text_col(stmt, 5);
        attempt.score = int_col(stmt, 6);
        attempt.passed = int_col(stmt, 7) != 0;
        attempt.correct_count = int_col(stmt, 8);
        attempt.total_questions = int_col(stmt, 9);
        attempt.attempted_at = text_col(stmt, 10);
        attempts.push_back(attempt);
    }
    finalize(stmt);
    return attempts;
}

bool Database::create_course_revision(const std::string& course_id,
                                       const std::string& created_by,
                                       const std::string& note,
                                       CourseRevision& revision,
                                       std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto course = load_course_by_id(db_, course_id);
    if (!course) {
        error = "Course not found";
        return false;
    }
    auto modules = load_modules(db_, course_id, true);
    auto lessons = load_lessons(db_, course_id, true);
    auto resources = load_resources(db_, course_id);

    nlohmann::json modules_json = nlohmann::json::array();
    for (const auto& module : modules) {
        nlohmann::json module_json = module.to_json();
        nlohmann::json lesson_list = nlohmann::json::array();
        for (const auto& lesson : lessons) {
            if (lesson.module_id != module.id) continue;
            nlohmann::json lesson_json = lesson.to_json();
            auto quiz = load_quiz(db_, lesson.id, true);
            if (quiz) lesson_json["quiz"] = quiz->to_json(true);
            else lesson_json["quiz"] = nullptr;
            lesson_list.push_back(lesson_json);
        }
        module_json["lessons"] = lesson_list;
        modules_json.push_back(module_json);
    }
    nlohmann::json snapshot = {
        {"course", course->to_json()},
        {"modules", modules_json},
        {"resources", nlohmann::json::array()}
    };
    for (const auto& resource : resources) snapshot["resources"].push_back(resource.to_json());

    revision.id = utils::generate_uuid();
    revision.course_id = course_id;
    revision.version = course->version;
    revision.created_by = created_by;
    revision.note = note;
    revision.snapshot = snapshot.dump();
    revision.created_at = utils::get_current_iso8601();

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO learning_course_revisions (id, course_id, version, created_by, note, snapshot, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not record revision";
        return false;
    }
    bind_text(stmt, 1, revision.id);
    bind_text(stmt, 2, revision.course_id);
    sqlite3_bind_int(stmt, 3, revision.version);
    if (created_by.empty()) sqlite3_bind_null(stmt, 4);
    else bind_text(stmt, 4, created_by);
    bind_text(stmt, 5, revision.note);
    bind_text(stmt, 6, revision.snapshot);
    bind_text(stmt, 7, revision.created_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not record revision";
        return false;
    }
    return true;
}

std::vector<CourseRevision> Database::list_course_revisions(const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CourseRevision> revisions;
    const char* sql =
        "SELECT r.id, r.course_id, r.version, COALESCE(r.created_by, ''), COALESCE(u.email, ''), "
        "r.note, '', r.created_at FROM learning_course_revisions r "
        "LEFT JOIN users u ON u.id = r.created_by WHERE r.course_id = ? "
        "ORDER BY r.created_at DESC, r.version DESC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return revisions;
    bind_text(stmt, 1, course_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CourseRevision revision;
        revision.id = text_col(stmt, 0);
        revision.course_id = text_col(stmt, 1);
        revision.version = int_col(stmt, 2);
        revision.created_by = text_col(stmt, 3);
        revision.created_by_email = text_col(stmt, 4);
        revision.note = text_col(stmt, 5);
        revision.snapshot = text_col(stmt, 6);
        revision.created_at = text_col(stmt, 7);
        revisions.push_back(revision);
    }
    finalize(stmt);
    return revisions;
}

std::optional<CourseRevision> Database::get_course_revision(const std::string& revision_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql =
        "SELECT r.id, r.course_id, r.version, COALESCE(r.created_by, ''), COALESCE(u.email, ''), "
        "r.note, r.snapshot, r.created_at FROM learning_course_revisions r "
        "LEFT JOIN users u ON u.id = r.created_by WHERE r.id = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, revision_id);
    std::optional<CourseRevision> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CourseRevision revision;
        revision.id = text_col(stmt, 0);
        revision.course_id = text_col(stmt, 1);
        revision.version = int_col(stmt, 2);
        revision.created_by = text_col(stmt, 3);
        revision.created_by_email = text_col(stmt, 4);
        revision.note = text_col(stmt, 5);
        revision.snapshot = text_col(stmt, 6);
        revision.created_at = text_col(stmt, 7);
        result = revision;
    }
    finalize(stmt);
    return result;
}

nlohmann::json Database::get_course_learning_analytics(const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json result = {
        {"course_id", course_id},
        {"enrollments", 0},
        {"active_enrollments", 0},
        {"completions", 0},
        {"average_progress_percent", 0},
        {"quiz_attempts", 0},
        {"average_quiz_score", 0},
        {"note", "Aggregate AdControl learning metrics only; no named learner data is returned."}
    };
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT COUNT(*), "
        "SUM(CASE WHEN status = 'active' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN status = 'completed' THEN 1 ELSE 0 END) "
        "FROM learning_enrollments WHERE course_id = ?;";
    if (prepare(db_, sql, &stmt)) {
        bind_text(stmt, 1, course_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result["enrollments"] = int_col(stmt, 0);
            result["active_enrollments"] = int_col(stmt, 1);
            result["completions"] = int_col(stmt, 2);
        }
        finalize(stmt);
    }

    sqlite3_stmt* quiz_stmt = nullptr;
    const char* quiz_sql =
        "SELECT COUNT(*), COALESCE(CAST(AVG(score) AS INTEGER), 0) "
        "FROM learning_quiz_attempts WHERE course_id = ?;";
    if (prepare(db_, quiz_sql, &quiz_stmt)) {
        bind_text(quiz_stmt, 1, course_id);
        if (sqlite3_step(quiz_stmt) == SQLITE_ROW) {
            result["quiz_attempts"] = int_col(quiz_stmt, 0);
            result["average_quiz_score"] = int_col(quiz_stmt, 1);
        }
        finalize(quiz_stmt);
    }

    // Average progress is computed over current active lessons. Completed
    // enrollments count as 100% even if the curriculum was later expanded.
    sqlite3_stmt* progress_stmt = nullptr;
    const char* progress_sql =
        "SELECT COALESCE(CAST(AVG(CASE WHEN e.status = 'completed' THEN 100 ELSE "
        "CASE WHEN lessons.total = 0 THEN 0 ELSE (completed.done * 100 / lessons.total) END END) AS INTEGER), 0) "
        "FROM learning_enrollments e "
        "LEFT JOIN (SELECT course_id, COUNT(*) AS total FROM learning_lessons WHERE status = 'active' GROUP BY course_id) lessons "
        "ON lessons.course_id = e.course_id "
        "LEFT JOIN (SELECT p.enrollment_id, COUNT(*) AS done "
        "FROM learning_lesson_progress p "
        "JOIN learning_lessons l ON l.id = p.lesson_id AND l.status = 'active' "
        "GROUP BY p.enrollment_id) completed "
        "ON completed.enrollment_id = e.id "
        "WHERE e.course_id = ?;";
    if (prepare(db_, progress_sql, &progress_stmt)) {
        bind_text(progress_stmt, 1, course_id);
        if (sqlite3_step(progress_stmt) == SQLITE_ROW) {
            result["average_progress_percent"] = int_col(progress_stmt, 0);
        }
        finalize(progress_stmt);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Initial launch catalog
//
// The PRD asks for a focused, reviewed launch catalog rather than an empty
// marketplace. These seeds are deliberately concise, practical courses. They
// are inserted once (guarded by learning_seed_state) and can then be edited,
// reviewed, or archived through the authoring API without being recreated on
// restart.
// ---------------------------------------------------------------------------
namespace {

struct SeedLesson {
    std::string slug;
    std::string title;
    int minutes;
    bool preview;
    std::string content;
};

struct SeedModule {
    std::string title;
    std::string summary;
    std::vector<SeedLesson> lessons;
};

struct SeedOption {
    std::string text;
    bool correct;
};

struct SeedQuestion {
    std::string prompt;
    std::string explanation;
    std::vector<SeedOption> options;
};

struct SeedCourse {
    std::string topic_slug;
    std::string slug;
    std::string title;
    std::string summary;
    std::string description;
    std::string level;
    int minutes;
    std::vector<std::string> outcomes;
    std::vector<SeedModule> modules;
    std::vector<SeedQuestion> quiz;
};

std::vector<SeedCourse> launch_courses() {
    return {
        {
            "digital-marketing-foundations",
            "digital-marketing-foundations",
            "Digital Marketing Foundations: From Audience to Measurement",
            "Build a practical map of audiences, offers, channels, and measurement before you spend money.",
            "This course gives you a working model of digital marketing that you can apply to a small business, a publisher site, or your own project. It focuses on decisions rather than jargon: who you are trying to help, what change you want, which channel has the right job, and how you will know whether it worked.\n\nThe lessons are vendor-neutral. Examples use common advertising, search, content, and analytics concepts, but you should always check current platform documentation before changing a live account.",
            "beginner",
            75,
            {"Explain the relationship between audience, offer, message, and channel.",
             "Describe a customer journey without pretending it is perfectly linear.",
             "Choose a small set of channels based on the job each must do.",
             "Define an outcome and a supporting metric before launching work."},
            {
                {"Start with the customer and the offer",
                 "Anchor every marketing decision in a specific audience and a clear promise.",
                 {
                     {"define-the-audience-and-offer", "Define the audience and the offer", 18, true,
                      "## Start with a specific person\n\nAn audience is not everyone who might buy. It is a group with a shared situation, constraint, and reason to act. For a small shop, that could be local runners who need a durable shoe before a rainy season. For a publisher, it could be freelance designers looking for practical pricing templates.\n\nWrite one sentence: **For [audience] who [situation], we offer [offer] so they can [outcome].** If the sentence needs five qualifiers, narrow the audience or simplify the offer.\n\n## Make the promise testable\n\nAn offer includes the thing, the price or effort, the proof, and the next step. Vague promises such as \"grow your business\" cannot guide a landing page or an ad. A stronger promise names a result and a boundary: \"Get a one-page pricing template you can adapt in 30 minutes.\"\n\n## Check alignment\n\nBefore choosing a channel, ask:\n\n1. Does this audience already spend time where I plan to appear?\n2. Can I state the promise in the format that channel allows?\n3. Is there evidence the audience will believe the promise?\n4. Can I measure a meaningful next step without tracking people excessively?\n\nIf the answer to any question is no, fix the offer or the measurement plan before buying reach."},
                     {"map-the-journey-without-false-precision", "Map the journey without false precision", 17, false,
                      "## A useful journey is a hypothesis\n\nJourney maps describe stages such as awareness, consideration, purchase, and retention. They are useful for finding gaps, but real behavior loops, skips steps, and returns later. Avoid presenting a diagram as if every customer follows it.\n\nFor each stage, write down what the person is trying to do, what they need to trust, and what could block them. A simple table is more useful than a decorative funnel.\n\n## Look for evidence\n\nUse search terms, support questions, sales conversations, and on-site behavior to test your assumptions. Look for repeated language. If people ask \"does it work with my current setup?\", the consideration stage is not only about price; it is about compatibility.\n\n## Choose one friction point\n\nYou cannot fix every stage at once. Pick one friction point that matters to revenue or mission, describe the evidence, and decide what change would reduce it. That decision becomes the input to your channel plan."}
                 }},
                {"Choose channels and measure outcomes",
                 "Select channels by the job they perform, then define honest measures.",
                 {
                     {"pick-channels-by-job-to-be-done", "Pick channels by job to be done", 20, false,
                      "## Channels are tools, not identities\n\nSearch captures existing intent. Paid social can create attention or retarget known audiences. Email serves an audience that already opted in. Content and SEO build durable discovery. Affiliate partnerships borrow another publisher's trust. Each channel has a different job, cost structure, and failure mode.\n\nStart with the job: do you need to be found when someone is already looking, or do you need to build awareness before demand exists? The answer changes the creative, the landing page, and the measurement window.\n\n## Match format to attention\n\nA channel that gives you two seconds needs a clear promise and a recognizable next step. A channel where someone reads a long article can support evidence and nuance. Do not copy a long-form script into a short placement or expect a one-line image to carry a complex explanation.\n\n## Start with two bets\n\nChoose one channel with existing demand and one experiment that can create demand. Give each a narrow audience, a budget, and a review date. This keeps learning manageable and prevents a scatter of half-finished campaigns."},
                     {"measure-what-matters", "Measure what matters", 20, false,
                      "## Start from a decision\n\nA metric is useful only if it can change a decision. \"More traffic\" rarely tells you what to do. \"Qualified demo requests from the pricing page\" points to a specific page, audience, and action.\n\nDefine:\n\n- the next step you want (for example, a trial or a resource download),\n- the event that proves it,\n- a comparison point (previous period, holdout, or test group),\n- the decision you will make if the number moves.\n\n## Separate observed from inferred\n\nClicks and page views are observed by your tools to the extent that measurement works. Purchases, commissions, and offline outcomes may be confirmed elsewhere. Label each number by its source. Never call a redirect a sale.\n\n## Keep data minimal\n\nCollect what you need, not everything you can. Avoid full IP addresses or persistent visitor identifiers when aggregate counts answer the question. Document retention and access.\n\n**Practice:** write a one-page measurement plan for a small campaign. Include one outcome, two supporting metrics, a review date, and an explicit statement of what the data cannot prove."}
                 }},
            },
            {
                {"Which statement is the strongest audience definition?", "A usable audience definition names a shared situation and a reason to act. It is narrow enough to guide a message but not a single fictional person.", {{"Small businesses.", false}, {"Local runners preparing for a rainy-season race who need durable shoes.", true}, {"People aged 18-65 interested in fitness.", false}, {"Everyone who uses the internet.", false}}},
                {"What is the best reason to label a metric with its data source?", "Observed events and confirmed outcomes have different reliability. Source labels prevent overstating what your system actually knows.", {{"It makes dashboards look more technical.", false}, {"It proves the campaign caused a purchase.", false}, {"It keeps observed events separate from externally confirmed outcomes.", true}, {"It removes the need for a measurement plan.", false}}}
            }
        },
        {
            "advertising-campaigns",
            "campaign-optimization-basics",
            "Campaign Optimization Basics: Diagnose, Test, Improve",
            "A structured routine for finding the real bottleneck before changing bids, audiences, or creative.",
            "Campaign optimization is diagnostic work. This course teaches you to separate delivery problems from creative, offer, and landing-page problems, then run small honest tests. It applies to paid search, paid social, and owned placements where you control the creative and destination.\n\nThe goal is not constant tweaking. It is a repeatable loop: observe, form a hypothesis, make one meaningful change, wait for enough evidence, and update the plan.",
            "intermediate",
            90,
            {"Read campaign metrics as a system rather than isolated numbers.",
             "Separate delivery, creative, offer, and landing-page failures.",
             "Write a testable hypothesis with a pre-committed decision rule.",
             "Recognize false wins caused by seasonality, mix changes, or noisy samples."},
            {
                {"Read before you tune",
                 "Build a weekly diagnostic habit that protects you from random adjustments.",
                 {
                     {"separate-delivery-creative-and-landing-problems", "Separate delivery, creative, and landing-page problems", 22, true,
                      "## Find the first broken link\n\nWhen results are weak, resist the urge to change everything. Work backwards through the chain: Was the ad eligible to serve? Did the intended audience see it? Did they understand the promise? Did they click for the right reason? Did the destination keep the promise? Did the next step work?\n\nA low click-through rate with strong conversion and healthy delivery may mean the message is filtering well. High clicks with low completion may mean the creative promised something the page does not deliver. Very low delivery may be a policy, audience, budget, or schedule problem, not a creative problem.\n\n## Use a short diagnostic table\n\nFor each campaign, record delivery status, spend pacing, audience size, click quality signals, landing-page completion, and the one constraint you believe is binding. Review the table before making changes.\n\n## Change one layer at a time\n\nIf you change the audience, creative, and landing page together, a better result does not tell you why. Pick the layer with the clearest evidence and change one meaningful variable."},
                     {"build-a-weekly-diagnostic-routine", "Build a weekly diagnostic routine", 18, false,
                      "## A 30-minute review\n\n1. Check that tracking and the destination still work.\n2. Confirm spend and delivery match the plan.\n3. Compare this period with a relevant baseline, not a randomly chosen week.\n4. Review search terms, creative feedback, and customer questions for new evidence.\n5. Write one decision: keep, pause, investigate, or test.\n\n## Keep a decision log\n\nRecord the date, the observation, the assumption, the change, and the review date. A decision log prevents circular debates and helps the next person understand why a campaign looks the way it does.\n\n## Protect the baseline\n\nDo not compare a campaign against a period with a different budget, season, or audience. When the environment changes, say so in the review. If you cannot make a fair comparison, describe the uncertainty instead of inventing a percentage."}
                 }},
                {"Design honest tests",
                 "Make experiments small enough to run and strong enough to learn from.",
                 {
                     {"write-a-testable-hypothesis", "Write a testable hypothesis", 20, false,
                      "## From hunch to hypothesis\n\nA useful hypothesis names the audience, the change, the expected behavior, and the reason. Example: \"For returning visitors on mobile, replacing the generic CTA with a pricing link will increase pricing-page views because the current CTA hides the next step.\"\n\nDefine the primary measure before launch. Add a decision rule: if the measure moves by a meaningful amount and the guardrail metrics do not degrade, adopt the change. Otherwise, stop or redesign.\n\n## Sample and duration\n\nSmall samples produce unstable percentages. Estimate how much traffic you need, or accept that the test is directional and call it so. Do not stop the moment the number looks favorable; choose the review date in advance.\n\n## Guardrails\n\nInclude measures that protect quality: refunds, unsubscribes, support contacts, or downstream qualification. A cheaper click is not a win if it attracts people who cannot use the product."},
                     {"avoid-fake-wins", "Avoid fake wins and premature conclusions", 18, false,
                      "## Common traps\n\n- **Peeking and stopping early:** checking repeatedly and stopping on a favorable swing.\n- **Mix shift:** comparing periods with different traffic sources or devices.\n- **Seasonality:** a holiday or outage explains the change.\n- **Tracking gaps:** a broken event looks like a decline.\n- **Multiple changes:** too many variables to attribute the result.\n\n## Report ranges and uncertainty\n\nWhen the sample is small, report counts and acknowledge uncertainty rather than a precise lift. State what you changed, what you observed, and what you cannot conclude.\n\n## Build a learning library\n\nFile the hypothesis, the result, and the next question. Failed tests are useful when they answer a real question. **Practice:** write one hypothesis for a page you own, including a guardrail metric and a review date."}
                 }},
            },
            {
                {"A campaign gets many clicks but few completed sign-ups. Which investigation is most useful first?", "The mismatch may sit between the creative promise and the destination. Check the landing page and event tracking before assuming the audience is wrong.", {{"Increase the budget immediately.", false}, {"Verify the landing page keeps the promise and the completion event works.", true}, {"Change every creative at once.", false}, {"Assume the audience is low quality.", false}}},
                {"Why should a decision rule be written before a test starts?", "Pre-committing reduces the temptation to reinterpret noise as a win after seeing the data.", {{"It guarantees a statistically significant result.", false}, {"It removes the need to measure guardrails.", false}, {"It reduces after-the-fact rationalization of noisy results.", true}, {"It makes sample size irrelevant.", false}}}
            }
        },
        {
            "seo",
            "seo-foundations",
            "SEO Foundations: Build Discoverable Pages",
            "Map intent, structure crawlable content, and improve pages without chasing shortcuts.",
            "Search engine optimization is the practice of making useful pages easy to find and understand. This course covers the durable parts: intent mapping, information architecture, page quality, titles, internal links, and technical hygiene that lets crawlers reach your content.\n\nNo course can guarantee rankings. Search engines change their systems and competitors change too. The aim is to build assets that deserve to be discovered and to remove avoidable barriers.",
            "beginner",
            85,
            {"Map real search questions to useful page formats.",
             "Design a crawlable information architecture with clear internal links.",
             "Write titles and summaries that are accurate rather than clickbait.",
             "Recognize technical problems that block discovery or indexing."},
            {
                {"Understand intent and structure",
                 "Start with the question, then build the smallest page that answers it well.",
                 {
                     {"map-queries-to-intent", "Map queries to intent", 20, true,
                      "## Read the question behind the query\n\nPeople search to learn, compare, find a place, or complete a task. The words hint at the job, but the result page shows what search engines currently consider useful. Review the mix of results: guides, product pages, local listings, forums, or documentation.\n\nIf most results are comparison pages, a single promotional page may not satisfy the intent. If most are short definitions, a 4,000-word essay may bury the answer.\n\n## Group by job\n\nBuild topic groups around a shared job and audience, not around every keyword variation. One strong page can answer related phrasings. Separate pages only when the intent genuinely differs.\n\n## Choose a page format\n\nA checklist, tutorial, comparison table, calculator, and product page all serve different needs. Match the format to the work the visitor is trying to finish. Add evidence where trust matters: examples, limitations, dates, and sources."},
                     {"design-crawlable-structure", "Design crawlable structure", 18, false,
                      "## Make the path obvious\n\nCrawlers and people both follow links. A clear hierarchy lets important pages accumulate internal links and helps visitors understand where they are. Use stable, readable URLs and keep a page reachable through normal navigation, not only through a search box or a complex script.\n\n## Internal linking\n\nLink from related content with descriptive text. Avoid dumping hundreds of unrelated links into a footer. A link should answer: why would someone click this next?\n\n## Sitemaps and robots\n\nA sitemap lists canonical pages you want discovered. Robots rules control crawling, not ranking, and should not be used to hide a page from search results. If a page must not be public, require authentication or remove it; `noindex` is not a security control.\n\n## Check the basics\n\n- One clear canonical URL per page where possible.\n- Useful titles and headings.\n- No accidental blocking by authentication, JavaScript errors, or redirect loops.\n- Mobile layout that keeps the main content readable."}
                 }},
                {"Write and maintain quality",
                 "Improve the page and keep it fresh without turning content into filler.",
                 {
                     {"write-useful-titles-and-page-copy", "Write useful titles and page copy", 18, false,
                      "## Titles are labels\n\nA title should tell a person what the page is and why it is relevant. Include the primary topic naturally; do not stuff every variation into it. The same principle applies to the H1 and the opening paragraph.\n\n## Answer early\n\nState the answer, then provide context, evidence, and caveats. Long introductions that delay the answer frustrate readers and make it harder to extract useful information.\n\n## Add first-hand value\n\nGeneric summaries compete with everything already published. Add examples, screenshots, test results, local constraints, or a clear method. Explain who the advice is for and when it does not apply.\n\n## Avoid fabricated authority\n\nDo not invent ratings, customer counts, credentials, or guaranteed outcomes. If you use AI assistance to draft, verify facts and take responsibility for the final page. Cite primary sources where claims are technical or consequential."},
                     {"handle-technical-debt", "Handle technical debt that blocks indexing", 16, false,
                      "## Diagnose before rewriting\n\nUse search console reports, server logs, and crawl data to see whether pages are discovered, crawled, and indexed. A page missing from results may be new, blocked, duplicated, low quality, or simply not selected. These are different problems.\n\n## Common blockers\n\n- Canonical tags pointing to the wrong URL.\n- Accidental `noindex` after a migration.\n- Redirect chains and soft 404s.\n- Content rendered only after an interaction that crawlers do not perform.\n- Multiple URLs serving the same content without a canonical choice.\n\n## Make changes reversible\n\nRecord the before state, change one technical variable, and verify after deployment. Migrations deserve a checklist and a rollback plan. **Practice:** choose one public page and write down its search intent, canonical URL, internal links, and one improvement you can verify."}
                 }},
            },
            {
                {"What is the most useful first step when planning a new SEO page?", "Intent should drive format and scope. Keyword volume alone does not tell you what the searcher is trying to complete.", {{"Choose the highest-volume keyword regardless of intent.", false}, {"Review the job behind the query and the formats currently satisfying it.", true}, {"Add every related keyword to the title.", false}, {"Publish several near-duplicate pages for each phrasing.", false}}},
                {"Why is `noindex` a poor way to protect private content?", "Search directives are not access control. Private data must be protected by authentication or removed from public storage.", {{"It prevents all bots from ever seeing the page.", false}, {"It guarantees encryption.", false}, {"It does not prevent access; private content needs authentication or removal.", true}, {"It blocks direct links from being opened.", false}}}
            }
        },
        {
            "geo",
            "geo-answer-engine-basics",
            "GEO Basics: Writing for AI-Assisted Search",
            "Understand generative search, structure evidence for retrieval, and measure visibility without hype.",
            "Generative Engine Optimization (GEO) is an emerging name for an old discipline: making clear, attributable, well-structured information easier to retrieve and cite in AI-assisted search experiences. The field changes quickly, so this course emphasizes durable practices and honest measurement rather than tricks or guarantees.\n\nYou will learn how retrieval-based systems may use pages, how to structure content for both people and machines, and how to evaluate whether your work is helping.",
            "intermediate",
            70,
            {"Explain how generative search differs from a ranked list of links.",
             "Structure pages so key answers and evidence are easy to retrieve.",
             "Write attributable content without fabricated claims.",
             "Measure visibility carefully without treating citations as guaranteed traffic."},
            {
                {"How generative search uses content",
                 "Build a realistic mental model instead of chasing imaginary algorithms.",
                 {
                     {"what-generative-search-does-differently", "What generative search does differently", 16, true,
                      "## From links to synthesized answers\n\nClassic search returns a ranked list. Generative search may retrieve passages, combine them, and produce an answer with links or citations. The exact systems vary by product and change over time. We do not control them, and we cannot guarantee inclusion.\n\n## Retrieval rewards clarity\n\nA page that directly answers a distinct question, uses descriptive headings, and provides evidence is easier to use as a source. Ambiguous marketing language is harder to quote accurately and harder for a person to verify.\n\n## Trust is part of relevance\n\nAttribution, dates, author expertise, primary sources, and transparent limitations help readers judge a claim. These are quality signals for people first. Treat them as such rather than as a checklist to game.\n\n## What stays the same\n\nUseful content, crawlable pages, clear structure, and accurate claims remain the foundation. If a tactic sounds like manipulating an AI system, it is probably fragile and may violate platform or ethical expectations."},
                     {"structure-pages-for-retrieval", "Structure pages for retrieval", 17, false,
                      "## Write answerable sections\n\nUse headings that name the question. Put the short answer near the heading, then explain the reasoning and exceptions. Keep one main idea per section so a retrieved passage is useful on its own.\n\n## Use structured evidence\n\nTables, definitions, step lists, and concise examples are easier to interpret than long unbroken paragraphs. Include units, dates, and scope. If you compare tools, state the criteria and who the comparison suits.\n\n## Mark up public facts, not fiction\n\nStructured data can clarify an article, organization, or product, but it must match the visible page. Never add ratings, prices, or availability that are not real. Search features may ignore your markup; it is not a ranking guarantee.\n\n## Keep entrances obvious\n\nVisitors may arrive at a section rather than the top. Each major section should orient the reader and link to related context. Avoid pronouns that only make sense after a long previous passage."}
                 }},
                {"Evidence, freshness, and measurement",
                 "Earn trust with verifiable material and judge results honestly.",
                 {
                     {"write-attributable-content", "Write attributable content", 16, false,
                      "## Show your sources\n\nLink to primary documentation, research, or first-hand measurements. Distinguish your own experience from external claims. Give the date of a changing fact and explain when it might expire.\n\n## State limits\n\nA useful page says who it is for and when it does not apply. \"This worked for a 50-page site\" is more credible than \"this always works.\" Avoid invented case studies, testimonials, or expert quotes.\n\n## Disclose AI assistance where appropriate\n\nPolicies differ by publisher and jurisdiction. At minimum, a human should verify facts, add genuine context, and accept responsibility. Do not publish a generic summary and call it expertise.\n\n## Keep a fact ledger\n\nFor consequential content, maintain a list of claims, sources, and review dates. When a source changes, update the page rather than letting stale advice linger."},
                     {"measure-visibility-without-myths", "Measure visibility without chasing myths", 16, false,
                      "## Observe what you can\n\nReferral traffic from AI-assisted products is often small, inconsistent, or bundled with other referrals. Track it as a separate source if the referrer is identifiable, but expect gaps. Do not claim causation from a single spike.\n\n## Combine signals\n\nUse branded search, direct traffic, assisted conversions, citation observations, and qualitative customer feedback. A citation without a click can still influence a later decision, but you cannot measure that perfectly. Say so.\n\n## Run content experiments\n\nImprove one page with clearer sections and stronger evidence, record the date, and compare relevant outcomes over time. Small changes are confounded by seasonality and product changes, so treat results as directional unless you have a proper comparison.\n\n**Practice:** rewrite one section of a public page to answer a single question directly, with one source and one stated limitation."}
                 }},
            },
            {
                {"What is a durable way to improve a page's chance of being useful in generative search?", "Clear structure, direct answers, and verifiable evidence help both people and retrieval systems. Shortcuts and fabricated authority are fragile.", {{"Add hidden text listing many keywords.", false}, {"Publish invented expert quotes to look authoritative.", false}, {"Answer specific questions clearly and cite verifiable sources.", true}, {"Guarantee that every AI answer will cite the page.", false}}},
                {"A generative search product sends a handful of referrals after a change. What is the sound conclusion?", "Small referral counts are noisy and often incomplete. You can note the observation, but cannot attribute a causal win from this alone.", {{"The change caused a permanent traffic increase.", false}, {"The observation is interesting but too small to establish causation.", true}, {"Citations are worthless because clicks are low.", false}, {"You should immediately rewrite every page the same way.", false}}}
            }
        },
        {
            "sem-ppc",
            "sem-ppc-foundations",
            "SEM & PPC Foundations: Intent, Structure, and Budgets",
            "Plan paid search around intent and economics, then diagnose waste without breaking what works.",
            "This course introduces paid search and pay-per-click advertising with a focus on decisions: which queries deserve a paid answer, how to structure campaigns so results are interpretable, how to write ads that match intent, and how to manage budgets and waste.\n\nPlatform interfaces and bidding features change constantly. Confirm current settings in the platform's documentation, and keep a written account of what you changed and why.",
            "beginner",
            80,
            {"Translate business economics into a workable cost-per-action target.",
             "Group keywords and ads so results remain interpretable.",
             "Write ad copy that matches query intent and sets honest expectations.",
             "Diagnose common sources of wasted spend."},
            {
                {"Intent and economics come first",
                 "Set the boundaries before you build a large account.",
                 {
                     {"choose-keywords-by-intent-and-margin", "Choose keywords by intent and margin", 18, true,
                      "## Not every query is worth buying\n\nA query may show interest without commercial intent, or intent without the ability to buy. Paid search works best when the query reveals a task and the economics support the cost of appearing.\n\nStart with the value of a completed action. If a lead is worth 100 and you can afford 20 per lead, that target shapes bids, geography, and which queries you pursue. It is not a guarantee; it is a constraint.\n\n## Build intent groups\n\nGroup queries that can share an ad and a destination page. A person searching for a specific product model expects that model, not a generic category page. A person researching a definition may not want a demo.\n\n## Exclusions are part of strategy\n\nAdd negative keywords for irrelevant jobs, existing customers if they should use support, and terms that attract low-quality traffic. Review search terms regularly; do not set and forget."},
                     {"build-account-structure", "Build account structure that answers questions", 17, false,
                      "## Structure for decisions\n\nA good account structure makes it easy to answer: which audience, intent, ad, and landing page produced this result? Avoid hundreds of tiny groups with no data, and avoid one giant group where every query shares a generic ad.\n\n## Match ads and destinations\n\nThe ad should confirm the query's promise and the landing page should continue it. If the ad says \"same-day delivery,\" the page must make that easy to verify. Specificity usually beats cleverness.\n\n## Track meaningful actions\n\nDefine the action before launch and verify tracking end to end. Include a guardrail such as refunds, unsubscribes, or lead quality. A click is a cost; it is not a customer."}
                 }},
                {"Run and diagnose",
                 "Use a weekly rhythm to protect budget and learn from the account.",
                 {
                     {"write-ads-that-match-the-query", "Write ads that match the query", 16, false,
                      "## Relevance and honesty\n\nLead with the offer and a reason to believe. Use the query's language when it clarifies rather than mimics. Include a clear next step. Do not promise outcomes you cannot control, and do not use urgency that is not real.\n\n## Test one idea\n\nIf testing headlines, keep the destination and offer stable. If testing a landing page, keep the ad stable. One change at a time makes the result interpretable, even when the platform rotates variants automatically.\n\n## Consider the whole result\n\nA lower click-through rate can be fine if it filters unqualified traffic. Judge ad variants by downstream quality, not clicks alone."},
                     {"manage-budgets-and-waste", "Manage budgets and diagnose waste", 16, false,
                      "## Common waste sources\n\n- Irrelevant search terms.\n- Broad match with no negatives.\n- Ads sending traffic to a slow or mismatched page.\n- Duplicate campaigns competing for the same query.\n- Budget spread across too many unproven experiments.\n- Conversion tracking that counts the same action twice.\n\n## Review cadence\n\nCheck spend pacing and tracking daily for a new campaign, then weekly once stable. Investigate sudden changes before adjusting bids. A spend drop may be a billing, policy, or eligibility issue, not a performance signal.\n\n## Stop rules\n\nDecide in advance what evidence would pause a campaign. For example: no qualified action after a defined spend threshold, or a cost per action persistently above the viable target. **Practice:** write one excluded query theme and one stop rule for a campaign you own."}
                 }},
            },
            {
                {"Why should paid-search account structure be designed for decisions?", "Structure is useful when it keeps audience, intent, creative, and destination interpretable. A structure that hides which factor caused a result cannot guide improvement.", {{"It guarantees the lowest cost per click.", false}, {"It makes it easier to interpret which intent and creative produced results.", true}, {"It removes the need for negative keywords.", false}, {"It prevents competitors from bidding.", false}}},
                {"Which is a reliable sign that conversion tracking may be misleading?", "Duplicate or inflated conversion events make cost-per-action look better than reality. Verify tracking end to end before optimizing.", {{"The same action is counted twice when a page reloads.", true}, {"The campaign uses a daily budget.", false}, {"The ad has a clear call to action.", false}, {"The landing page loads quickly.", false}}}
            }
        },
        {
            "affiliate-monetization",
            "shopee-affiliate-basics",
            "Affiliate Marketing & Shopee Basics: Disclose and Measure",
            "Evaluate affiliate offers, preserve tracking links, and report clicks without mistaking them for sales.",
            "Affiliate marketing pays a publisher when a qualifying action is attributed through a tracked link. This course introduces the mechanics, with Shopee affiliate links as a common example. It focuses on trust: choosing relevant offers, disclosing the relationship, preserving the merchant's tracking URL exactly, and reporting observed clicks separately from externally confirmed commission.\n\nAdControl can store and serve an affiliate URL you enter manually through a first-party redirect. It does not generate links or import commission data.",
            "beginner",
            75,
            {"Explain how affiliate attribution and commissions typically work.",
             "Choose offers that fit an audience and can be disclosed clearly.",
             "Preserve an affiliate tracking URL without rewriting its query string.",
             "Report clicks as observed redirects, not confirmed sales."},
            {
                {"Affiliate mechanics and trust",
                 "Understand the commercial relationship before you place a link.",
                 {
                     {"how-affiliate-attribution-works", "How affiliate models and tracking work", 18, true,
                      "## The basic loop\n\nA merchant or affiliate network gives a publisher a tracked link. A visitor follows the link, the merchant records a qualifying event, and the network attributes a commission according to its terms. Attribution may use cookies, sub-IDs, or account-level rules. Different programs define qualifying purchases, returns, and commission windows differently.\n\n## What a publisher controls\n\nYou control where and how the link is presented, whether the audience understands the relationship, and whether the destination matches the promise. You do not control the merchant's attribution system or commission decision.\n\n## Read the program terms\n\nCheck allowed traffic sources, disclosure requirements, prohibited claims, cookie duration, and payment thresholds. A link that works technically may still violate program rules. Keep evidence of the terms and review dates.\n\n## Never fake urgency or proof\n\nDo not invent discounts, stock levels, ratings, or endorsements. Product details and prices change; stale claims erode trust and may breach program rules."},
                     {"choose-offers-that-fit", "Choose offers that fit your audience", 18, false,
                      "## Relevance beats commission rate\n\nA high commission on an irrelevant product wastes attention and trust. Start with the audience's problem, then ask whether the product genuinely helps. If you would not mention it without payment, say so in your editorial policy or do not place it.\n\n## Check the whole offer\n\nReview the product title, merchant, price presentation, availability date, image, landing page, and program terms. AdControl stores product metadata as operator-entered content; it does not verify the merchant's claims or availability.\n\n## Disclose clearly\n\nPlace a visible disclosure near the affiliate link, in plain language. The disclosure must not be hidden in a footer or inside the clickable anchor where it can be separated from the link. The exact wording can vary by jurisdiction and audience; make it understandable.\n\n## Treat offers as a reviewable inventory\n\nPause expired or withdrawn offers. Remove misleading images. Keep a note of when you last checked the product."}
                 }},
                {"Publish and measure responsibly",
                 "Use exact tracking URLs, a first-party redirect, and honest reporting.",
                 {
                     {"place-links-without-hiding-disclosure", "Place product links without hiding disclosure", 18, false,
                      "## Preserve the tracking URL\n\nAn affiliate URL often contains parameters that identify you and the campaign. Do not add, remove, decode, or reorder them unless the program explicitly tells you to. Store the exact URL provided by the merchant or network. AdControl validates its structure and serves it through a signed first-party redirect; it does not rewrite the query string.\n\n## Keep the destination server-side\n\nA public page should not contain the raw affiliate destination or allow arbitrary destination parameters. The click token binds the click to a stored offer. The API re-checks that the offer still belongs to your site and is active before redirecting.\n\n## Disclosure placement\n\nPut the disclosure immediately before or after the product card, outside the clickable anchor. Do not rely on a tooltip alone. On mobile, make sure it remains visible without hovering.\n\n## Do not mask the destination\n\nExplain that the link goes to the merchant. A visitor should not be surprised by a redirect."},
                     {"read-click-data-honestly", "Read click data without calling it revenue", 18, false,
                      "## Observed versus confirmed\n\nA click or outbound redirect is observed by AdControl when the browser follows the signed link. A sale, qualifying action, commission, and payout are confirmed by the merchant or affiliate network. They are not the same event.\n\nIf you do not import merchant reports, your conversion and commission numbers should show as not connected rather than zero-with-certainty. Describe clicks as clicks.\n\n## Useful measures\n\n- outbound redirects per offer,\n- click-through rate from an impression to an outbound visit,\n- changes after a disclosure or placement edit,\n- merchant-reported commission as a separate, dated import if available.\n\n## Watch for abuse\n\nThe same signed link can be followed more than once, and automated traffic can inflate counts. Rate limits reduce abuse but do not prove a person or a unique visitor. Do not use click counts for payouts without independent evidence. **Practice:** write a one-sentence disclosure for an offer and a reporting note explaining what your click count can and cannot prove."}
                 }},
            },
            {
                {"What is the safest way to handle a merchant's affiliate URL?", "The URL contains tracking parameters whose exact form matters. Store and serve it byte-for-byte rather than rewriting it.", {{"Remove parameters that look unnecessary to shorten it.", false}, {"Store the exact URL and redirect to it without adding or reordering parameters.", true}, {"Move the affiliate URL into a public HTML attribute.", false}, {"Generate a new link automatically from the product name.", false}}},
                {"AdControl records 40 outbound redirects. What has been confirmed?", "A redirect is an observed event. It does not confirm a purchase, qualifying action, or commission.", {{"Forty sales.", false}, {"Forty unique customers.", false}, {"Forty observed redirects; commissions require merchant confirmation.", true}, {"A guaranteed return on ad spend.", false}}}
            }
        },
        {
            "content-social",
            "content-social-system",
            "Content & Social Systems: Plan Once, Publish Consistently",
            "Build a small, repeatable publishing system instead of a pile of disconnected posts.",
            "Content and social media work when they serve a clear audience promise and fit a sustainable production rhythm. This course helps you define that promise, plan a small calendar, adapt one idea per channel, and review performance without vanity metrics.\n\nIt is deliberately tool-agnostic. You can use a notebook, a spreadsheet, or a publishing platform; the system matters more than the software.",
            "beginner",
            70,
            {"Write a content promise that guides what you publish and what you skip.",
             "Plan a calendar small enough to sustain.",
             "Adapt one core idea to different channel formats without spam.",
             "Review content using decision-relevant signals rather than raw reach."},
            {
                {"Define the promise and the system",
                 "Make it clear why the audience should return.",
                 {
                     {"define-a-content-promise", "Define a content promise", 16, true,
                      "## A promise is a repeated benefit\n\nA content promise tells the audience what they can expect and how it will help. \"Practical, tested advice for independent shop owners\" is a promise. \"We post about business\" is not.\n\nWrite three parts:\n\n1. **For whom** — a recognizable audience.\n2. **What they get** — a useful outcome or perspective.\n3. **How it is made** — your format, evidence, or point of view.\n\nThe promise is also a filter. If an idea does not serve it, skip the idea even if it is trending.\n\n## Choose your evidence\n\nDecide what makes your content credible: first-hand tests, customer questions, original data, expert review, or clear sourcing. Do not claim expertise you cannot support."},
                     {"plan-a-small-calendar", "Plan a small, sustainable calendar", 16, false,
                      "## Start below capacity\n\nChoose a cadence you can maintain for three months. Two useful posts per week beat five posts that collapse in a month. Batch research and production where possible, but leave room for timely updates.\n\n## Use content pillars\n\nThree or four pillars keep variety while reinforcing the promise. Each pillar should answer a recurring audience question. For a marketing site, pillars might be measurement, creative diagnosis, channel selection, and practical teardowns.\n\n## Plan the work, not just the dates\n\nFor each item, record the audience question, the format, the source, the owner, and the publish date. A calendar without a source encourages rushed or invented content.\n\n## Make maintenance visible\n\nReview existing content on a schedule. Update stale facts and retire advice that no longer applies. An archive is part of the system."}
                 }},
                {"Adapt and learn",
                 "Turn one useful idea into channel-appropriate work, then improve the system.",
                 {
                     {"adapt-one-idea-per-channel", "Adapt one idea per channel", 16, false,
                      "## Respect the format\n\nA long article, a short video, a newsletter, and a social post need different pacing. Do not paste the same text everywhere. Keep the core idea and evidence, then choose the smallest format that delivers it.\n\n## Write channel-specific openings\n\nA reader on a social feed needs context instantly; a newsletter reader has opted in but still needs to know why this issue matters. A new channel should not require the audience to know your history.\n\n## Accessibility\n\nAdd captions to video, meaningful alternative text for images, readable contrast, and descriptive link text. Avoid flashing content and text baked into images when you can present it as text.\n\n## Avoid spammy repurposing\n\nIf a channel's audience receives near-identical posts repeatedly, reduce the frequency or change the angle. Repetition without added value erodes trust."},
                     {"review-without-vanity-metrics", "Review and improve without vanity metrics", 16, false,
                      "## Start from the job\n\nImpressions and follower counts are not outcomes by themselves. Decide what the content should cause: a saved recipe, a qualified newsletter signup, a support deflection, or a relevant conversation. Choose a small set of signals that can inform the next publishing decision.\n\n## Read qualitative evidence\n\nComments, replies, support questions, and sales conversations often explain why something worked. Look for repeated questions that deserve a dedicated piece.\n\n## Run a content review\n\nMonthly: which pieces answered the promise? Which brought the right audience? Which created confusion? What should be updated, repeated with a new angle, or retired?\n\n## Protect trust\n\nDo not buy fake engagement, misrepresent sponsorship, or use engagement bait that withholds the answer. **Practice:** write your content promise and plan two weeks of posts with sources and a review question."}
                 }},
            },
            {
                {"What makes a content promise useful?", "A useful promise names an audience, a repeated benefit, and a credible approach. It can guide both publishing and skipping off-strategy ideas.", {{"It includes every keyword the business cares about.", false}, {"It names an audience and a repeatable benefit that guides decisions.", true}, {"It promises daily posting no matter the quality.", false}, {"It hides the source of the advice.", false}}},
                {"Why is follower count a weak primary success metric?", "Follower count says little about whether content served the audience or influenced a meaningful action. Pair it with decision-relevant signals.", {{"It is always fabricated.", false}, {"It cannot be measured by platforms.", false}, {"It does not show whether content served a meaningful audience outcome.", true}, {"It guarantees revenue.", false}}}
            }
        },
        {
            "analytics-cro",
            "analytics-cro-loop",
            "Analytics & CRO: Turn Observations into Experiments",
            "Ask better measurement questions, build a trustworthy event model, and test conversion improvements.",
            "Analytics and conversion-rate optimization (CRO) are a loop: define a decision, measure the behavior that informs it, find a friction point, and test a change. This course covers event models, privacy-conscious measurement, prioritization, and simple experiment design.\n\nIt avoids tool-specific recipes. Names and interfaces change, but the decisions remain: what happened, what it means, what to try next, and how to avoid fooling yourself.",
            "intermediate",
            90,
            {"Turn a business decision into a measurable question.",
             "Design a small, documented event model with clear ownership.",
             "Prioritize friction points using evidence and expected impact.",
             "Evaluate a simple experiment without overstating certainty."},
            {
                {"Build measurement you can trust",
                 "Define events from decisions, then protect data quality and privacy.",
                 {
                     {"ask-a-decision-shaped-question", "Ask a decision-shaped analytics question", 18, true,
                      "## Replace \"how are we doing?\"\n\nA decision-shaped question names a choice and the evidence that would inform it. Instead of \"why is traffic down?\", ask \"did the change to the pricing page reduce qualified trial starts, and should we keep it?\" The second question has an owner, a comparison, and a next action.\n\n## Choose the smallest measure\n\nUse an event that closely represents the desired outcome. Add supporting context only when it helps explain a change. More dashboards do not create more clarity.\n\n## Write definitions down\n\nDefine what counts as a trial, a qualified lead, or an active user. Include time zones, deduplication rules, and exclusions. Ambiguous definitions create arguments that data cannot resolve."},
                     {"build-a-trustworthy-event-model", "Build a trustworthy event model", 20, false,
                      "## One event, one meaning\n\nName events by what happened. Keep names stable after launch; renaming an event breaks history. Document required properties and who owns the definition.\n\n## Test the path\n\nTrigger the event in a real browser and verify it arrives with the expected properties. Check duplicate sends, failed requests, and blocked scripts. A dashboard showing zero can be a tracking problem, not a behavior change.\n\n## Privacy by design\n\nCollect the minimum needed for the decision. Avoid full IP addresses and persistent cross-site identifiers by default. Hash identifiers only when necessary, document retention, and restrict access. Analytics should not become a shadow customer database.\n\n## Reconcile with a second source\n\nCompare key counts with a server-side record, order system, or support volume. Discrepancies may be normal, but unexplained discrepancies make optimization risky."}
                 }},
                {"Find friction and test",
                 "Use evidence to choose a change and keep experiments interpretable.",
                 {
                     {"prioritize-friction-with-evidence", "Prioritize friction with evidence", 18, false,
                      "## Look for the stuck step\n\nFind where people stop or repeat work. Combine quantitative signals (drop-off, error events, repeated searches) with qualitative evidence (session notes, support tickets, interviews). A high bounce rate is a symptom, not a diagnosis.\n\n## Score opportunities\n\nEstimate the number of affected visitors, the likely impact on the desired action, confidence in the evidence, and the effort to change. Reject ideas that cannot be implemented or measured reliably.\n\n## Fix the obvious before testing\n\nBroken buttons, slow pages, and confusing labels are defects. Fix them and verify. Save uncertain improvements for experiments. Do not run a test to decide whether to repair a bug."},
                     {"design-and-analyze-an-experiment", "Design and analyze a simple experiment", 18, false,
                      "## State the hypothesis and rule\n\nName the change, the audience, the primary measure, the expected direction, and the decision rule. Include guardrails. Choose the duration before launch.\n\n## Randomization and contamination\n\nEnsure assignment is consistent for a visitor and that both variants can actually run. If the change affects shared systems, a client-side split may not be valid.\n\n## Results\n\nReport sample sizes, raw counts, and the uncertainty. A result can be directionally useful without proving a universal law. Check for novelty effects, seasonality, and segment differences.\n\n## Close the loop\n\nAdopt, reject, or iterate with a reason. Archive the experiment and update the documentation. **Practice:** write a decision-shaped analytics question and one guardrail for a page you can modify."}
                 }},
            },
            {
                {"Which is a decision-shaped analytics question?", "It names a specific change, outcome, comparison, and likely action. General curiosity questions do not guide a decision.", {{"How are our numbers this month?", false}, {"Did the new checkout button change completed orders enough to keep it?", true}, {"What is the meaning of traffic?", false}, {"Can we collect every possible user attribute?", false}}},
                {"Why should obvious defects be fixed before running an A/B test?", "Testing whether to repair something broken wastes traffic and delays a known improvement. Experiments are for uncertain changes, not avoidable defects.", {{"Defects disqualify a site from analytics.", false}, {"Fixing a defect guarantees a conversion increase.", false}, {"A broken experience is a known problem; testing whether to fix it wastes users' time.", true}, {"A/B tests cannot be run on buttons.", false}}}
            }
        }
    };
}

} // namespace

bool Database::seed_learning_catalog() {
    {
        // Repair published rows that predate persistence of freshness metadata.
        // This is intentionally idempotent and limited to published content.
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* repair = nullptr;
        const char* repair_sql =
            "UPDATE learning_courses SET "
            "published_at = COALESCE(NULLIF(published_at, ''), updated_at), "
            "last_reviewed_at = COALESCE(NULLIF(last_reviewed_at, ''), updated_at), "
            "next_review_due_at = COALESCE(NULLIF(next_review_due_at, ''), "
            "strftime('%Y-%m-%dT%H:%M:%SZ', updated_at, '+' || ? || ' days')) "
            "WHERE status = 'published' AND (published_at IS NULL OR published_at = '' OR "
            "last_reviewed_at IS NULL OR last_reviewed_at = '' OR next_review_due_at IS NULL OR "
            "next_review_due_at = '');";
        if (prepare(db_, repair_sql, &repair)) {
            sqlite3_bind_int(repair, 1, LEARNING_REVIEW_INTERVAL_DAYS);
            sqlite3_step(repair);
            finalize(repair);
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = nullptr;
        if (prepare(db_, "SELECT 1 FROM learning_seed_state WHERE key = 'launch-catalog-v1' LIMIT 1;", &stmt)) {
            bool seeded = sqlite3_step(stmt) == SQLITE_ROW;
            finalize(stmt);
            if (seeded) return true;
        }
    }

    // Resolve an administrator as the initial author/reviewer. If none exists
    // yet, author_id stays empty; the catalog is still readable.
    std::string admin_id;
    std::string admin_email;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = nullptr;
        if (prepare(db_, "SELECT id, email FROM users WHERE role = 'admin' ORDER BY created_at ASC LIMIT 1;", &stmt)) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                admin_id = text_col(stmt, 0);
                admin_email = text_col(stmt, 1);
            }
            finalize(stmt);
        }
    }

    struct SeedTopic {
        const char* slug;
        const char* title;
        const char* description;
        int sort_order;
    };
    const std::vector<SeedTopic> topics = {
        {"digital-marketing-foundations", "Digital Marketing Foundations",
         "Audience, offer, channel, and measurement fundamentals for people starting or rebuilding a marketing practice.", 10},
        {"advertising-campaigns", "Advertising & Campaign Optimization",
         "Diagnose delivery, creative, landing-page, and measurement problems before changing a campaign.", 20},
        {"seo", "Search Engine Optimization",
         "Earn discoverability with useful pages, clear structure, and technical hygiene.", 30},
        {"geo", "Generative Engine Optimization",
         "Understand AI-assisted search and structure trustworthy, retrieval-friendly content.", 40},
        {"sem-ppc", "SEM & PPC Advertising",
         "Plan and operate paid search with intent, economics, and honest measurement.", 50},
        {"affiliate-monetization", "Affiliate Marketing & Shopee Basics",
         "Publish affiliate offers with visible disclosure and exact tracking links.", 60},
        {"content-social", "Content & Social Media",
         "Build a sustainable content system that serves a defined audience promise.", 70},
        {"analytics-cro", "Analytics & Conversion Optimization",
         "Measure decisions, find friction, and run experiments that survive scrutiny.", 80}
    };

    std::string now = utils::get_current_iso8601();
    for (const auto& seed : topics) {
        if (get_learning_topic_by_slug(seed.slug)) continue;
        LearningTopic topic;
        topic.id = std::string("seed-topic-") + seed.slug;
        topic.slug = seed.slug;
        topic.title = seed.title;
        topic.description = seed.description;
        topic.status = "published";
        topic.sort_order = seed.sort_order;
        topic.created_at = now;
        topic.updated_at = now;
        std::string error;
        if (!create_learning_topic(topic, error)) {
            std::cerr << "[Learning seed] Topic " << seed.slug << ": " << error << std::endl;
            return false;
        }
    }

    for (const auto& seed : launch_courses()) {
        if (get_learning_course_by_slug(seed.slug)) continue;
        auto topic = get_learning_topic_by_slug(seed.topic_slug);
        if (!topic) {
            std::cerr << "[Learning seed] Missing topic for " << seed.slug << std::endl;
            return false;
        }

        LearningCourse course;
        course.id = std::string("seed-course-") + seed.slug;
        course.slug = seed.slug;
        course.topic_id = topic->id;
        course.title = seed.title;
        course.summary = seed.summary;
        course.description_md = seed.description;
        course.level = seed.level;
        course.duration_minutes = seed.minutes;
        course.outcomes = seed.outcomes;
        course.status = "published";
        course.author_id = admin_id;
        course.reviewer_id = admin_id;
        course.version = 1;
        course.published_at = now;
        course.last_reviewed_at = now;
        course.next_review_due_at = utils::iso8601_from_epoch(
            utils::utc_now_epoch() + static_cast<std::time_t>(LEARNING_REVIEW_INTERVAL_DAYS) * 86400);
        course.created_at = now;
        course.updated_at = now;
        std::string error;
        if (!create_learning_course(course, error)) {
            std::cerr << "[Learning seed] Course " << seed.slug << ": " << error << std::endl;
            return false;
        }

        std::string last_lesson_id;
        int module_index = 0;
        for (const auto& seed_module : seed.modules) {
            ++module_index;
            LearningModule module;
            module.id = course.id + "-m" + std::to_string(module_index);
            module.course_id = course.id;
            module.title = seed_module.title;
            module.summary = seed_module.summary;
            module.sort_order = module_index * 10;
            module.status = "active";
            module.created_at = now;
            module.updated_at = now;
            if (!create_learning_module(module, error)) {
                std::cerr << "[Learning seed] Module " << seed.slug << ": " << error << std::endl;
                return false;
            }

            int lesson_index = 0;
            for (const auto& seed_lesson : seed_module.lessons) {
                ++lesson_index;
                LearningLesson lesson;
                lesson.id = module.id + "-l" + std::to_string(lesson_index);
                lesson.course_id = course.id;
                lesson.module_id = module.id;
                lesson.slug = seed_lesson.slug;
                lesson.title = seed_lesson.title;
                lesson.content_md = seed_lesson.content;
                lesson.estimated_minutes = seed_lesson.minutes;
                lesson.sort_order = module_index * 100 + lesson_index;
                lesson.is_preview = seed_lesson.preview;
                lesson.status = "active";
                lesson.created_at = now;
                lesson.updated_at = now;
                if (!create_learning_lesson(lesson, error)) {
                    std::cerr << "[Learning seed] Lesson " << seed.slug << ": " << error << std::endl;
                    return false;
                }
                last_lesson_id = lesson.id;
            }
        }

        if (!last_lesson_id.empty() && !seed.quiz.empty()) {
            LessonQuiz quiz;
            quiz.id = course.id + "-quiz";
            quiz.lesson_id = last_lesson_id;
            quiz.course_id = course.id;
            quiz.title = "Knowledge check";
            quiz.passing_score = 70;
            int question_index = 0;
            for (const auto& seed_question : seed.quiz) {
                ++question_index;
                QuizQuestion question;
                question.id = quiz.id + "-q" + std::to_string(question_index);
                question.prompt = seed_question.prompt;
                question.explanation = seed_question.explanation;
                question.sort_order = question_index;
                int option_index = 0;
                for (const auto& seed_option : seed_question.options) {
                    ++option_index;
                    QuizOption option;
                    option.id = question.id + "-o" + std::to_string(option_index);
                    option.text = seed_option.text;
                    option.is_correct = seed_option.correct;
                    option.sort_order = option_index;
                    question.options.push_back(option);
                }
                quiz.questions.push_back(question);
            }
            if (!replace_learning_quiz(last_lesson_id, course.id, quiz, error)) {
                std::cerr << "[Learning seed] Quiz " << seed.slug << ": " << error << std::endl;
                return false;
            }
        }
    }

    // A small set of launch learning paths. They are published only when every
    // course referenced is published.
    struct SeedPath {
        const char* slug;
        const char* title;
        const char* description;
        const char* level;
        int sort_order;
        std::vector<std::string> course_slugs;
    };
    const std::vector<SeedPath> paths = {
        {"marketing-launch-path", "Marketing Launch Path",
         "Start with foundations, learn to diagnose campaigns, build durable content, and close the loop with analytics.",
         "beginner", 10,
         {"digital-marketing-foundations", "campaign-optimization-basics", "content-social-system", "analytics-cro-loop"}},
        {"discoverability-path", "Discoverability Path",
         "Learn how search and generative search surface useful content, then connect discovery to measurement.",
         "beginner", 20,
         {"digital-marketing-foundations", "seo-foundations", "geo-answer-engine-basics", "analytics-cro-loop"}},
        {"paid-growth-path", "Paid Growth Path",
         "Plan paid acquisition around intent and economics, optimize honestly, and measure the result.",
         "intermediate", 30,
         {"digital-marketing-foundations", "sem-ppc-foundations", "campaign-optimization-basics", "analytics-cro-loop"}},
        {"publisher-monetization-path", "Publisher Monetization Path",
         "Build an audience-serving site, add affiliate offers with clear disclosure, and report observed results honestly.",
         "beginner", 40,
         {"digital-marketing-foundations", "shopee-affiliate-basics", "content-social-system", "analytics-cro-loop"}}
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& path : paths) {
            sqlite3_stmt* check = nullptr;
            bool exists = false;
            if (prepare(db_, "SELECT 1 FROM learning_paths WHERE slug = ? LIMIT 1;", &check)) {
                bind_text(check, 1, path.slug);
                exists = sqlite3_step(check) == SQLITE_ROW;
                finalize(check);
            }
            if (exists) continue;

            std::string path_id = std::string("seed-path-") + path.slug;
            sqlite3_stmt* insert = nullptr;
            const char* sql =
                "INSERT INTO learning_paths (id, slug, title, description, level, status, sort_order, "
                "published_at, created_at, updated_at) VALUES (?, ?, ?, ?, ?, 'published', ?, ?, ?, ?);";
            if (!prepare(db_, sql, &insert)) return false;
            bind_text(insert, 1, path_id);
            bind_text(insert, 2, path.slug);
            bind_text(insert, 3, path.title);
            bind_text(insert, 4, path.description);
            bind_text(insert, 5, path.level);
            sqlite3_bind_int(insert, 6, path.sort_order);
            bind_text(insert, 7, now);
            bind_text(insert, 8, now);
            bind_text(insert, 9, now);
            bool ok = sqlite3_step(insert) == SQLITE_DONE;
            finalize(insert);
            if (!ok) return false;

            int order = 0;
            for (const auto& slug : path.course_slugs) {
                sqlite3_stmt* link = nullptr;
                const char* link_sql =
                    "INSERT OR IGNORE INTO learning_path_courses (path_id, course_id, sort_order) "
                    "SELECT ?, id, ? FROM learning_courses WHERE slug = ? AND status = 'published';";
                if (!prepare(db_, link_sql, &link)) return false;
                bind_text(link, 1, path_id);
                sqlite3_bind_int(link, 2, order += 10);
                bind_text(link, 3, slug);
                ok = sqlite3_step(link) == SQLITE_DONE;
                finalize(link);
                if (!ok) return false;
            }
        }
        sqlite3_stmt* marker = nullptr;
        const char* marker_sql =
            "INSERT OR REPLACE INTO learning_seed_state (key, applied_at) VALUES ('launch-catalog-v1', ?);";
        if (!prepare(db_, marker_sql, &marker)) return false;
        bind_text(marker, 1, now);
        bool ok = sqlite3_step(marker) == SQLITE_DONE;
        finalize(marker);
        if (!ok) return false;
    }

    // A best-effort audit entry marks the initial catalog import.
    if (admin_id.empty()) {
        AuditLog al;
        al.id = utils::generate_uuid();
        al.actor = "system";
        al.action = "SEED_LEARNING_CATALOG";
        al.entity_type = "learning";
        al.entity_id = "launch-catalog-v1";
        al.details = "{}";
        al.created_at = now;
        record_audit_log(al);
    } else {
        audit(*this, admin_email, "SEED_LEARNING_CATALOG", "learning",
              "launch-catalog-v1", {{"courses", 8}, {"paths", 4}});
    }
    return true;
}

} // namespace adcontrol
