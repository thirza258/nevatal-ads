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
    // All deliverable campaigns for a site, grouped by placement. The delivery
    // handler applies audience, frequency, budget, and experiment selection.
    std::vector<std::pair<Placement, Campaign>> get_delivery_campaign_candidates(const std::string& site_id, const std::string& now_iso);
    long long get_campaign_impressions_between(const std::string& campaign_id,
                                               const std::string& start_iso,
                                               const std::string& end_iso);

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
    // Monetization: plans, subscriptions, usage, invoices, payments, revenue
    // -----------------------------------------------------------------------

    // Plans are operator-defined and seeded with Free/Pro/Business/Enterprise.
    std::vector<BillingPlan> list_billing_plans(bool active_only = false);
    std::optional<BillingPlan> get_billing_plan(const std::string& id);
    bool update_billing_plan(const BillingPlan& plan);

    // Every publisher is lazily assigned the Free plan until an administrator
    // or the publisher selects another plan.
    std::optional<PublisherSubscription> get_publisher_subscription(const std::string& publisher_id);
    std::optional<PublisherSubscription> ensure_publisher_subscription(const std::string& publisher_id,
                                                                      std::string& error);
    std::vector<PublisherSubscription> list_publisher_subscriptions();
    bool update_publisher_subscription_plan(const std::string& publisher_id,
                                            const std::string& plan_id,
                                            std::string& error);

    // Usage is measured in UTC calendar periods. Impressions are counted from
    // first-party ad_events; campaigns excludes archived rows.
    PublisherUsage get_publisher_usage(const std::string& publisher_id,
                                       const std::string& period_start,
                                       const std::string& period_end);
    long long count_publisher_impressions(const std::string& publisher_id,
                                          const std::string& period_start,
                                          const std::string& period_end);
    int count_publisher_campaigns(const std::string& publisher_id);

    // Invoices and manual payments. Settlement is provider-agnostic: an
    // administrator records an offline payment, and the invoice transitions to
    // paid/partially_paid. Course items grant a learner entitlement atomically.
    bool create_billing_invoice(BillingInvoice& invoice, std::string& error);
    std::optional<BillingInvoice> get_billing_invoice(const std::string& id);
    std::optional<BillingInvoice> find_billing_invoice(const std::string& publisher_id,
                                                       const std::string& user_id,
                                                       const std::string& context,
                                                       const std::string& period_start,
                                                       const std::string& period_end,
                                                       bool open_only = false);
    std::vector<BillingInvoice> list_billing_invoices(const std::optional<std::string>& publisher_id,
                                                      const std::optional<std::string>& user_id,
                                                      const std::optional<std::string>& context,
                                                      const std::optional<std::string>& status,
                                                      int limit = 100,
                                                      int offset = 0);
    bool record_billing_payment(BillingPayment& payment, std::string& error);
    std::vector<BillingPayment> list_billing_payments(const std::optional<std::string>& publisher_id,
                                                      const std::optional<std::string>& user_id,
                                                      const std::optional<std::string>& invoice_id = std::nullopt,
                                                      int limit = 100,
                                                      int offset = 0);
    bool void_billing_invoice(const std::string& invoice_id);

    // Paid learning-course access.
    bool grant_course_entitlement(const std::string& user_id,
                                  const std::string& course_id,
                                  const std::string& invoice_id,
                                  std::string& error);
    std::optional<CourseEntitlement> get_course_entitlement(const std::string& user_id,
                                                            const std::string& course_id);
    std::vector<CourseEntitlement> list_user_entitlements(const std::string& user_id);
    bool revoke_course_entitlement(const std::string& user_id, const std::string& course_id);

    // Publisher revenue share is computed from direct-campaign impressions at
    // the campaign CPM. Payouts are recorded manually after an offline transfer.
    PublisherRevenueSummary get_publisher_revenue(const std::string& publisher_id,
                                                  const std::string& period_start,
                                                  const std::string& period_end);
    bool create_publisher_payout(PublisherPayout& payout, std::string& error);
    std::vector<PublisherPayout> list_publisher_payouts(const std::optional<std::string>& publisher_id,
                                                        int limit = 100,
                                                        int offset = 0);

    // -----------------------------------------------------------------------
    // Audience segments and campaign experiments
    // -----------------------------------------------------------------------

    std::vector<AudienceSegment> list_audience_segments(
        const std::optional<std::string>& site_id = std::nullopt,
        const std::optional<std::string>& publisher_id = std::nullopt);
    std::optional<AudienceSegment> get_audience_segment(const std::string& id);
    bool create_audience_segment(AudienceSegment& segment, std::string& error);
    bool update_audience_segment(const AudienceSegment& segment);
    bool delete_audience_segment(const std::string& id);

    std::vector<Experiment> list_experiments(
        const std::optional<std::string>& site_id = std::nullopt,
        const std::optional<std::string>& placement_id = std::nullopt,
        const std::optional<std::string>& status = std::nullopt);
    std::optional<Experiment> get_experiment(const std::string& id);
    std::optional<Experiment> get_running_experiment_for_placement(const std::string& placement_id);
    bool create_experiment(Experiment& experiment, std::string& error);
    bool update_experiment(const Experiment& experiment, std::string& error);
    bool delete_experiment(const std::string& id);
    bool set_experiment_status(const std::string& id, const std::string& status);
    nlohmann::json get_experiment_metrics(const std::string& id);
    // Auto-allocates variant weights and declares winners once sample and lift
    // thresholds are met. Returns the number of experiments updated.
    int optimize_experiments(const std::string& now_iso);

    // -----------------------------------------------------------------------
    // Conversion tracking, attribution, and commission reconciliation
    // -----------------------------------------------------------------------

    std::vector<ConversionSource> list_conversion_sources(
        const std::optional<std::string>& publisher_id = std::nullopt,
        const std::optional<std::string>& site_id = std::nullopt,
        const std::optional<std::string>& status = std::nullopt);
    std::optional<ConversionSource> get_conversion_source(const std::string& id);
    std::optional<ConversionSource> get_conversion_source_by_public_key(const std::string& key);
    bool create_conversion_source(ConversionSource& source);
    bool update_conversion_source(const ConversionSource& source);

    // A duplicate (source_id, external_id) is idempotent: inserted=false and the
    // existing row's id is returned in the supplied event.
    bool record_conversion(ConversionEvent& event, bool& inserted, std::string& error);
    std::optional<ConversionEvent> get_conversion_event(const std::string& id);
    std::vector<ConversionEvent> list_conversion_events(
        const std::optional<std::string>& publisher_id,
        const std::optional<std::string>& site_id,
        const std::optional<std::string>& campaign_id,
        const std::optional<std::string>& status,
        const std::string& start_date = "",
        const std::string& end_date = "",
        int limit = 100,
        int offset = 0);
    bool update_conversion_reconciliation(const std::string& id,
                                          const std::string& status,
                                          const std::string& notes);
    nlohmann::json get_conversion_summary(
        const std::optional<std::string>& publisher_id,
        const std::optional<std::string>& site_id,
        const std::optional<std::string>& campaign_id,
        const std::string& start_date = "",
        const std::string& end_date = "");
    nlohmann::json get_conversion_reconciliation(
        const std::optional<std::string>& publisher_id,
        const std::optional<std::string>& site_id,
        const std::string& start_date = "",
        const std::string& end_date = "");

    // -----------------------------------------------------------------------
    // Marketing automation workflows, runs, and in-app notifications
    // -----------------------------------------------------------------------

    std::vector<AutomationWorkflow> list_automation_workflows(
        const std::optional<std::string>& publisher_id = std::nullopt,
        const std::optional<std::string>& site_id = std::nullopt,
        const std::optional<std::string>& status = std::nullopt);
    std::optional<AutomationWorkflow> get_automation_workflow(const std::string& id);
    bool create_automation_workflow(AutomationWorkflow& workflow, std::string& error);
    bool update_automation_workflow(const AutomationWorkflow& workflow, std::string& error);
    bool set_automation_workflow_status(const std::string& id, const std::string& status);
    bool delete_automation_workflow(const std::string& id);
    std::vector<AutomationStep> list_automation_steps(const std::string& workflow_id);

    std::vector<AutomationRun> list_automation_runs(
        const std::optional<std::string>& workflow_id = std::nullopt,
        const std::optional<std::string>& publisher_id = std::nullopt,
        int limit = 100);
    std::optional<AutomationRun> get_automation_run(const std::string& id);
    bool update_automation_run(const AutomationRun& run);
    bool append_automation_run_log(const std::string& run_id, const std::string& line);
    bool queue_manual_automation_run(const std::string& workflow_id,
                                     const std::string& payload_json,
                                     const std::string& trigger_key,
                                     std::string& run_id,
                                     std::string& error);

    // Queues a run for every active workflow matching the trigger event and
    // optional trigger filters (conversion, inquiry, manual). Idempotent for a
    // repeated trigger_key.
    int enqueue_automation_event(const std::string& trigger_type,
                                 const std::string& publisher_id,
                                 const std::string& site_id,
                                 const std::string& payload_json,
                                 const std::string& trigger_key,
                                 std::string& error);

    // Worker entry points: turn due schedules and performance conditions into
    // queued runs, then return runs ready for the executor.
    int create_due_scheduled_runs(const std::string& now_iso);
    int evaluate_performance_triggers(const std::string& now_iso);
    std::vector<AutomationRun> list_due_automation_runs(const std::string& now_iso, int limit = 20);

    // Metrics used by performance triggers and condition steps.
    nlohmann::json get_automation_metrics(const std::string& site_id,
                                          const std::string& campaign_id,
                                          int lookback_hours);

    bool create_notification(AppNotification& notification);
    std::optional<AppNotification> get_notification(const std::string& id);
    std::vector<AppNotification> list_notifications(
        const std::optional<std::string>& publisher_id,
        const std::optional<std::string>& user_id,
        int limit = 50);
    bool mark_notification_read(const std::string& id,
                                const std::optional<std::string>& user_id = std::nullopt);
    int count_unread_notifications(const std::optional<std::string>& publisher_id,
                                   const std::optional<std::string>& user_id);

    // -----------------------------------------------------------------------
    // CRM and lead management
    // -----------------------------------------------------------------------

    std::vector<CrmLead> list_crm_leads(const std::optional<std::string>& stage,
                                        const std::string& query = "",
                                        int limit = 200,
                                        int offset = 0);
    std::optional<CrmLead> get_crm_lead(const std::string& id);
    bool create_crm_lead(CrmLead& lead, std::string& error);
    bool update_crm_lead(const CrmLead& lead, std::string& error);
    bool delete_crm_lead(const std::string& id);
    // Converts a public partner inquiry into a scored lead exactly once.
    bool create_crm_lead_from_inquiry(const PartnerInquiry& inquiry,
                                      CrmLead& lead,
                                      bool& created,
                                      std::string& error);
    bool recalculate_crm_lead_score(const std::string& lead_id);

    std::vector<CrmActivity> list_crm_activities(const std::string& lead_id, int limit = 100);
    bool create_crm_activity(CrmActivity& activity, std::string& error);

    std::vector<CrmTask> list_crm_tasks(const std::optional<std::string>& lead_id = std::nullopt,
                                        const std::optional<std::string>& status = std::nullopt,
                                        int limit = 200);
    std::optional<CrmTask> get_crm_task(const std::string& id);
    bool create_crm_task(CrmTask& task, std::string& error);
    bool update_crm_task(const CrmTask& task, std::string& error);

    nlohmann::json get_crm_pipeline();
    nlohmann::json get_crm_summary();

    // -----------------------------------------------------------------------
    // SEO and GEO intelligence
    // -----------------------------------------------------------------------

    std::vector<SeoProject> list_seo_projects(
        const std::optional<std::string>& publisher_id = std::nullopt,
        const std::optional<std::string>& site_id = std::nullopt);
    std::optional<SeoProject> get_seo_project(const std::string& id);
    bool create_seo_project(SeoProject& project, std::string& error);
    bool update_seo_project(const SeoProject& project);
    bool delete_seo_project(const std::string& id);

    std::vector<SeoKeyword> list_seo_keywords(const std::string& project_id);
    std::optional<SeoKeyword> get_seo_keyword(const std::string& id);
    bool create_seo_keyword(SeoKeyword& keyword, std::string& error);
    bool create_seo_keywords(std::vector<SeoKeyword>& keywords, std::string& error);
    bool update_seo_keyword(const SeoKeyword& keyword);
    bool delete_seo_keyword(const std::string& id);
    bool record_seo_ranking(SeoKeyword& keyword, SeoRanking& ranking, std::string& error);
    std::vector<SeoRanking> list_seo_rankings(const std::string& keyword_id, int limit = 100);

    std::vector<SeoCompetitor> list_seo_competitors(const std::string& project_id);
    std::optional<SeoCompetitor> get_seo_competitor(const std::string& id);
    bool create_seo_competitor(SeoCompetitor& competitor, std::string& error);
    bool update_seo_competitor(const SeoCompetitor& competitor);
    bool delete_seo_competitor(const std::string& id);
    std::vector<SeoCompetitorKeyword> list_seo_competitor_keywords(const std::string& competitor_id);
    bool create_seo_competitor_keyword(SeoCompetitorKeyword& keyword, std::string& error);
    bool delete_seo_competitor_keyword(const std::string& id);

    std::vector<SeoBacklink> list_seo_backlinks(const std::string& project_id);
    std::optional<SeoBacklink> get_seo_backlink(const std::string& id);
    bool create_seo_backlink(SeoBacklink& backlink, std::string& error);
    bool update_seo_backlink(const SeoBacklink& backlink);
    bool delete_seo_backlink(const std::string& id);

    bool create_seo_audit(SeoAudit& audit, std::string& error);
    std::vector<SeoAudit> list_seo_audits(const std::string& project_id, int limit = 30);
    std::optional<SeoAudit> get_seo_audit(const std::string& id, bool include_issues = true);

    std::vector<GeoQuery> list_geo_queries(const std::string& project_id);
    std::optional<GeoQuery> get_geo_query(const std::string& id);
    bool create_geo_query(GeoQuery& query, std::string& error);
    bool update_geo_query(const GeoQuery& query);
    bool delete_geo_query(const std::string& id);
    bool create_geo_mention(GeoMention& mention, std::string& error);
    std::vector<GeoMention> list_geo_mentions(const std::string& query_id, int limit = 100);

    nlohmann::json get_seo_summary(const std::string& project_id);

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
    bool seed_billing_plans();
};

} // namespace adcontrol
