#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include "models.hpp"

namespace adcontrol {

class Database {
public:
    Database();
    ~Database();

    bool init(const std::string& db_path = "adcontrol.db");
    void close();
    bool ping();

    // Online, consistent backup using SQLite's backup API (safe while the server is
    // running, including WAL content). Verifies the destination with integrity_check.
    bool backup_to(const std::string& destination_path);

    // User & Session
    std::optional<User> get_user_by_email(const std::string& email);
    std::optional<User> get_user_by_id(const std::string& id);
    bool create_session(const std::string& token, const std::string& user_id, const std::string& expires_at);
    std::optional<User> get_user_by_token(const std::string& token);
    bool delete_session(const std::string& token);
    bool delete_user_sessions(const std::string& user_id);
    bool update_user_password(const std::string& user_id, const std::string& password_hash, const std::string& salt);

    // Publishers / tenancy
    bool create_publisher_with_owner(Publisher& publisher, User& owner);
    std::vector<Publisher> list_publishers();
    std::optional<Publisher> get_publisher(const std::string& id);
    bool update_publisher(const Publisher& publisher);

    // Partner inquiries (public publisher/client applications)
    bool create_partner_inquiry(PartnerInquiry& inquiry);
    std::vector<PartnerInquiry> list_partner_inquiries(
        const std::optional<std::string>& kind = std::nullopt,
        const std::optional<std::string>& status = std::nullopt,
        int limit = 100, int offset = 0);
    std::optional<PartnerInquiry> get_partner_inquiry(const std::string& id);
    bool update_partner_inquiry_status(const std::string& id, const std::string& status);

    // Administrator-managed SMTP configuration (single row). The password is
    // stored as ciphertext; encryption and decryption happen in the API layer.
    std::optional<EmailSettings> get_email_settings();
    bool save_email_settings(const EmailSettings& settings);

    // Affiliate offers
    std::vector<AffiliateOffer> list_affiliate_offers(const std::optional<std::string>& publisher_id = std::nullopt,
                                                       const std::optional<std::string>& status = std::nullopt);
    std::optional<AffiliateOffer> get_affiliate_offer(const std::string& id);
    bool create_affiliate_offer(AffiliateOffer& offer);
    bool update_affiliate_offer(const AffiliateOffer& offer);
    bool set_affiliate_offer_status(const std::string& id, const std::string& status);
    bool delete_affiliate_offer(const std::string& id);
    bool affiliate_offer_has_campaigns(const std::string& id);
    std::vector<Campaign> list_campaigns_by_offer(const std::string& offer_id);

    // Sites
    std::vector<Site> list_sites(const std::optional<std::string>& publisher_id = std::nullopt);
    std::optional<Site> get_site(const std::string& id);
    std::optional<Site> get_site_by_public_key(const std::string& key);
    bool create_site(Site& site);
    bool update_site(const Site& site);
    bool rotate_site_key(const std::string& site_id, std::string& new_key);
    bool pause_all_campaigns_for_site(const std::string& site_id);
    bool delete_site(const std::string& site_id);
    bool bump_site_version(const std::string& site_id);
    bool mark_site_verified(const std::string& site_id, const std::string& method);
    // Domain changes invalidate ownership verification and issue a fresh token.
    bool reset_site_verification(const std::string& site_id);

    // Placements
    std::vector<Placement> list_placements(const std::optional<std::string>& site_id = std::nullopt);
    std::optional<Placement> get_placement(const std::string& id);
    std::optional<Placement> get_placement_by_site_and_key(const std::string& site_id, const std::string& placement_key);
    bool create_placement(Placement& placement);
    bool update_placement(const Placement& placement);
    bool delete_placement(const std::string& id);
    bool archive_placement(const std::string& id);
    bool placement_has_events(const std::string& id);

    // Campaigns
    std::vector<Campaign> list_campaigns(const std::optional<std::string>& site_id = std::nullopt,
                                         const std::optional<std::string>& placement_id = std::nullopt,
                                         const std::optional<std::string>& status = std::nullopt,
                                         const std::optional<std::string>& publisher_id = std::nullopt);
    std::optional<Campaign> get_campaign(const std::string& id);
    bool create_campaign(Campaign& campaign);
    bool update_campaign(const Campaign& campaign);
    bool publish_campaign(const std::string& id);
    bool pause_campaign(const std::string& id);
    bool delete_campaign(const std::string& id);
    bool archive_campaign(const std::string& id);
    bool campaign_has_events(const std::string& id);
    int advance_campaign_states(const std::string& now_iso);
    std::vector<std::pair<Placement, Campaign>> get_eligible_campaigns_for_delivery(const std::string& site_id, const std::string& now_iso);

    // Events
    bool record_event(const AdEvent& event);
    int purge_events_older_than(int days);
    bool site_has_events(const std::string& site_id);

    // Analytics
    nlohmann::json get_analytics_summary(const std::optional<std::string>& site_id,
                                        const std::optional<std::string>& placement_id,
                                        const std::optional<std::string>& campaign_id,
                                        const std::string& start_date = "",
                                        const std::string& end_date = "",
                                        const std::optional<std::string>& publisher_id = std::nullopt);

    nlohmann::json get_analytics_timeseries(const std::optional<std::string>& site_id,
                                           const std::optional<std::string>& placement_id,
                                           const std::optional<std::string>& campaign_id,
                                           int days = 7,
                                           const std::string& start_date = "",
                                           const std::string& end_date = "",
                                           const std::optional<std::string>& publisher_id = std::nullopt);

    nlohmann::json get_analytics_breakdown(const std::string& dimension,
                                          const std::optional<std::string>& site_id,
                                          const std::optional<std::string>& placement_id,
                                          const std::optional<std::string>& campaign_id,
                                          const std::string& start_date = "",
                                          const std::string& end_date = "",
                                          const std::optional<std::string>& publisher_id = std::nullopt);

    std::vector<AdEvent> get_events_export(const std::optional<std::string>& site_id,
                                          const std::optional<std::string>& placement_id,
                                          const std::optional<std::string>& campaign_id,
                                          const std::string& start_date = "",
                                          const std::string& end_date = "",
                                          const std::optional<std::string>& publisher_id = std::nullopt);

    // Audit Logs
    bool record_audit_log(const AuditLog& log);
    std::vector<AuditLog> list_audit_logs(int limit = 50, int offset = 0);

    // -----------------------------------------------------------------------
    // Phase 3 learning platform
    // -----------------------------------------------------------------------

    // Accounts. Registration uses role "learner"; staff roles are provisioned
    // only through an administrator endpoint.
    bool create_user(User& user, std::string& error);
    std::vector<User> list_learning_users();
    bool delete_learning_user(const std::string& user_id);

    // Topics
    std::vector<LearningTopic> list_learning_topics(bool published_only = false);
    std::optional<LearningTopic> get_learning_topic(const std::string& id);
    std::optional<LearningTopic> get_learning_topic_by_slug(const std::string& slug);
    bool create_learning_topic(LearningTopic& topic, std::string& error);
    bool update_learning_topic(const LearningTopic& topic);
    bool learning_topic_has_courses(const std::string& topic_id);

    // Courses
    std::vector<LearningCourse> list_learning_courses(
        const std::optional<std::string>& status = std::nullopt,
        const std::optional<std::string>& topic_id = std::nullopt,
        const std::optional<std::string>& author_id = std::nullopt,
        const std::string& query = "",
        int limit = 100,
        int offset = 0,
        bool published_only = false);
    std::optional<LearningCourse> get_learning_course(const std::string& id);
    std::optional<LearningCourse> get_learning_course_by_slug(const std::string& slug);
    bool create_learning_course(LearningCourse& course, std::string& error);
    bool update_learning_course(const LearningCourse& course);
    bool set_learning_course_status(const std::string& course_id,
                                    const std::string& status,
                                    const std::string& reviewer_id,
                                    const std::string& now_iso);
    bool archive_learning_course(const std::string& course_id);
    int count_learning_lessons(const std::string& course_id, bool active_only = true);
    int count_learning_modules(const std::string& course_id, bool active_only = true);

    // Modules and lessons
    std::vector<LearningModule> list_learning_modules(const std::string& course_id,
                                                      bool include_archived = false);
    std::optional<LearningModule> get_learning_module(const std::string& id);
    bool create_learning_module(LearningModule& module, std::string& error);
    bool update_learning_module(const LearningModule& module);
    bool archive_learning_module(const std::string& module_id);
    bool learning_module_has_lessons(const std::string& module_id);

    std::vector<LearningLesson> list_learning_lessons(const std::string& course_id,
                                                      bool include_archived = false);
    std::optional<LearningLesson> get_learning_lesson(const std::string& id);
    std::optional<LearningLesson> get_learning_lesson_by_slug(const std::string& course_id,
                                                              const std::string& slug);
    bool create_learning_lesson(LearningLesson& lesson, std::string& error);
    bool update_learning_lesson(const LearningLesson& lesson);
    bool archive_learning_lesson(const std::string& lesson_id);

    // Downloadable resources
    std::vector<LearningResource> list_learning_resources(const std::string& course_id);
    std::optional<LearningResource> get_learning_resource(const std::string& id);
    bool create_learning_resource(LearningResource& resource, std::string& error);
    bool delete_learning_resource(const std::string& id);

    // Quizzes. Answer keys are only loaded by authoring and grading code.
    std::optional<LessonQuiz> get_learning_quiz_for_lesson(const std::string& lesson_id,
                                                           bool include_answers = false);
    bool replace_learning_quiz(const std::string& lesson_id,
                               const std::string& course_id,
                               LessonQuiz& quiz,
                               std::string& error);
    bool delete_learning_quiz(const std::string& lesson_id);

    // Learning paths
    std::vector<LearningPath> list_learning_paths(bool published_only = false);
    std::optional<LearningPath> get_learning_path(const std::string& id);
    std::optional<LearningPath> get_learning_path_by_slug(const std::string& slug);
    std::vector<LearningCourse> list_learning_path_courses(const std::string& path_id,
                                                           bool published_only = true);

    // Enrollment and progress
    std::optional<Enrollment> get_learning_enrollment(const std::string& user_id,
                                                      const std::string& course_id);
    std::optional<Enrollment> get_learning_enrollment_by_id(const std::string& enrollment_id);
    std::vector<Enrollment> list_learning_enrollments(const std::string& user_id);
    bool enroll_in_learning_course(const std::string& user_id,
                                   const std::string& course_id,
                                   Enrollment& enrollment,
                                   bool& created,
                                   std::string& error);
    bool complete_learning_lesson(const std::string& enrollment_id,
                                  const std::string& course_id,
                                  const std::string& lesson_id,
                                  Enrollment& enrollment,
                                  std::string& error);
    bool touch_enrollment_lesson(const std::string& enrollment_id,
                                 const std::string& lesson_id);
    std::vector<std::string> list_completed_lesson_ids(const std::string& enrollment_id);

    // Quiz attempts (scores only; no free-form learner answers are retained)
    bool record_quiz_attempt(QuizAttempt& attempt);
    std::vector<QuizAttempt> list_quiz_attempts(const std::string& user_id,
                                                const std::string& course_id = "",
                                                const std::string& lesson_id = "");

    // Revision history and aggregate reporting
    bool create_course_revision(const std::string& course_id,
                                const std::string& created_by,
                                const std::string& note,
                                CourseRevision& revision,
                                std::string& error);
    std::vector<CourseRevision> list_course_revisions(const std::string& course_id);
    std::optional<CourseRevision> get_course_revision(const std::string& revision_id);
    nlohmann::json get_course_learning_analytics(const std::string& course_id);

    // Seeds the initial, focused launch catalog once. Safe to call on every
    // startup: a marker row prevents deleted/archived courses from returning.
    bool seed_learning_catalog();

private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;

    bool run_migrations();
    bool seed_default_admin();
};

} // namespace adcontrol
