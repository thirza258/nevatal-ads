#pragma once

#include <string>
#include <vector>
#include <optional>
#include "json.hpp"

namespace adcontrol {

struct User {
    std::string id;
    std::string email;
    std::string password_hash;
    std::string salt;
    // Back-office: "admin", "publisher", "analyst".
    // Learning: "learner", "instructor", "editor".
    std::string role;
    std::string publisher_id; // empty for platform administrators
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json(bool include_secrets = false) const {
        nlohmann::json j = {
            {"id", id},
            {"email", email},
            {"role", role},
            {"publisher_id", publisher_id},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
        if (include_secrets) {
            j["password_hash"] = password_hash;
            j["salt"] = salt;
        }
        return j;
    }
};

struct Session {
    std::string token;
    std::string user_id;
    std::string expires_at;
    std::string created_at;
};

struct Publisher {
    std::string id;
    std::string name;
    std::string owner_user_id;
    std::string owner_email; // derived from users for dashboard convenience
    std::string status;      // "pending", "active", "suspended", "rejected"
    std::string contact_email;
    std::string timezone;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"name", name},
            {"owner_user_id", owner_user_id},
            {"owner_email", owner_email},
            {"status", status},
            {"contact_email", contact_email},
            {"timezone", timezone},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

// A public "become a publisher" / "become a client" application. These arrive
// from the marketing site without an account, so they are stored for review and
// never auto-provision anything.
struct PartnerInquiry {
    std::string id;
    std::string kind; // "publisher" or "client"
    std::string name;
    std::string email;
    std::string company;
    std::string website;
    std::string message;
    std::string status; // "new", "reviewing", "closed"
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"kind", kind},
            {"name", name},
            {"email", email},
            {"company", company},
            {"website", website},
            {"message", message},
            {"status", status},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct Site {
    std::string id;
    std::string workspace_id;
    std::string publisher_id; // empty for platform-owned legacy sites
    std::string name;
    std::string primary_domain;
    std::vector<std::string> allowed_domains;
    std::string public_site_key;
    std::string status; // "active", "disabled"
    int config_version = 1;
    std::string verification_status = "verified"; // "unverified", "verified", "rejected"
    // The verification token is deliberately omitted from to_json(); it is only
    // returned by the authenticated verification endpoint.
    std::string verification_token;
    std::string verified_at;
    std::string monetization_status = "enabled"; // "pending", "enabled", "disabled"
    std::string default_disclosure_text;
    std::vector<std::string> allowed_ad_sources = {"house", "direct", "affiliate"};
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"workspace_id", workspace_id},
            {"publisher_id", publisher_id},
            {"name", name},
            {"primary_domain", primary_domain},
            {"allowed_domains", allowed_domains},
            {"public_site_key", public_site_key},
            {"status", status},
            {"config_version", config_version},
            {"verification_status", verification_status},
            {"verified_at", verified_at},
            {"monetization_status", monetization_status},
            {"default_disclosure_text", default_disclosure_text},
            {"allowed_ad_sources", allowed_ad_sources},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct Placement {
    std::string id;
    std::string site_id;
    std::string placement_key; // e.g. "header-banner"
    std::string name;
    std::string description;
    std::string format; // "responsive_banner", "image", "text_link", "native_card", "product_card"
    int width = 0;
    int height = 0;
    std::string fallback_behavior; // "hide", "blank", "custom"
    std::vector<std::string> allowed_source_types = {"house", "direct", "affiliate"};
    int max_items = 1;
    std::string layout_config = "{}";
    std::string disclosure_mode = "inherited"; // "required", "inherited", "disabled_only_if_not_applicable"
    std::string status; // "active", "disabled"
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        nlohmann::json layout = nlohmann::json::object();
        try {
            layout = nlohmann::json::parse(layout_config.empty() ? "{}" : layout_config);
        } catch (...) {
            layout = nlohmann::json::object();
        }
        return {
            {"id", id},
            {"site_id", site_id},
            {"placement_key", placement_key},
            {"name", name},
            {"description", description},
            {"format", format},
            {"width", width},
            {"height", height},
            {"fallback_behavior", fallback_behavior},
            {"allowed_source_types", allowed_source_types},
            {"max_items", max_items},
            {"layout_config", layout},
            {"disclosure_mode", disclosure_mode},
            {"status", status},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct Campaign {
    std::string id;
    std::string site_id;
    std::string placement_id;
    std::string name;
    std::string destination_url;
    std::string format; // "responsive_banner", "image", "text_link", "native_card", "product_card"
    std::string title;
    std::string body;
    std::string image_url;
    std::string alt_text;
    std::string cta_label;
    std::string status; // "draft", "scheduled", "active", "paused", "expired", "archived"
    std::string source_type = "house"; // "house", "direct", "affiliate", "external_network"
    std::string affiliate_offer_id;
    std::string disclosure_override;
    std::string starts_at; // ISO8601 or empty
    std::string ends_at;   // ISO8601 or empty
    int priority = 1;
    int weight = 100;
    std::string created_at;
    std::string updated_at;
    std::string published_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"site_id", site_id},
            {"placement_id", placement_id},
            {"name", name},
            {"destination_url", destination_url},
            {"format", format},
            {"title", title},
            {"body", body},
            {"image_url", image_url},
            {"alt_text", alt_text},
            {"cta_label", cta_label},
            {"status", status},
            {"source_type", source_type},
            {"affiliate_offer_id", affiliate_offer_id},
            {"disclosure_override", disclosure_override},
            {"starts_at", starts_at},
            {"ends_at", ends_at},
            {"priority", priority},
            {"weight", weight},
            {"created_at", created_at},
            {"updated_at", updated_at},
            {"published_at", published_at}
        };
    }
};

struct AffiliateOffer {
    std::string id;
    std::string publisher_id;
    std::string provider = "shopee_affiliate";
    std::string offer_name;
    std::string product_title;
    std::string product_image_url;
    std::string product_description;
    std::string merchant_name;
    std::string affiliate_url;
    std::string canonical_product_url;
    std::string price_text;
    std::string currency;
    std::string availability_checked_at;
    std::string disclosure_text;
    std::string status = "draft"; // "draft", "active", "paused", "expired", "rejected", "archived"
    std::string validation_status = "unverified"; // "valid", "unverified", "rejected"
    std::string validation_reason;
    std::string last_link_validation_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"publisher_id", publisher_id},
            {"provider", provider},
            {"offer_name", offer_name},
            {"product_title", product_title},
            {"product_image_url", product_image_url},
            {"product_description", product_description},
            {"merchant_name", merchant_name},
            {"affiliate_url", affiliate_url},
            {"canonical_product_url", canonical_product_url},
            {"price_text", price_text},
            {"currency", currency},
            {"availability_checked_at", availability_checked_at},
            {"disclosure_text", disclosure_text},
            {"status", status},
            {"validation_status", validation_status},
            {"validation_reason", validation_reason},
            {"last_link_validation_at", last_link_validation_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct AdEvent {
    std::string id;
    std::string site_id;
    std::string placement_id;
    std::string campaign_id;
    std::string source_type;
    std::string affiliate_offer_id;
    std::string event_type; // "impression", "click", "outbound_redirect"
    std::string event_id;   // deduplication key
    std::string occurred_at;
    std::string page_origin;
    std::string referrer;
    std::string user_agent_category;
    std::string ip_hash;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"site_id", site_id},
            {"placement_id", placement_id},
            {"campaign_id", campaign_id},
            {"source_type", source_type},
            {"affiliate_offer_id", affiliate_offer_id},
            {"event_type", event_type},
            {"event_id", event_id},
            {"occurred_at", occurred_at},
            {"page_origin", page_origin},
            {"referrer", referrer},
            {"user_agent_category", user_agent_category}
        };
    }
};

// ---------------------------------------------------------------------------
// Phase 3 learning platform
// ---------------------------------------------------------------------------

struct LearningTopic {
    std::string id;
    std::string slug;
    std::string title;
    std::string description;
    std::string status = "published"; // "draft", "published", "archived"
    int sort_order = 0;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"slug", slug},
            {"title", title},
            {"description", description},
            {"status", status},
            {"sort_order", sort_order},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct LearningCourse {
    std::string id;
    std::string slug;
    std::string topic_id;
    std::string topic_slug;
    std::string topic_title;
    std::string title;
    std::string summary;
    std::string description_md;
    std::string level = "beginner"; // "beginner", "intermediate", "advanced"
    int duration_minutes = 0;
    std::vector<std::string> outcomes;
    std::string status = "draft"; // draft, in_review, approved, published, archived
    std::string author_id;
    std::string author_email;
    std::string reviewer_id;
    std::string reviewer_email;
    int version = 1;
    std::string published_at;
    std::string last_reviewed_at;
    std::string next_review_due_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"slug", slug},
            {"topic_id", topic_id},
            {"topic_slug", topic_slug},
            {"topic_title", topic_title},
            {"title", title},
            {"summary", summary},
            {"description_md", description_md},
            {"level", level},
            {"duration_minutes", duration_minutes},
            {"outcomes", outcomes},
            {"status", status},
            {"author_id", author_id},
            {"author_email", author_email},
            {"reviewer_id", reviewer_id},
            {"reviewer_email", reviewer_email},
            {"version", version},
            {"published_at", published_at},
            {"last_reviewed_at", last_reviewed_at},
            {"next_review_due_at", next_review_due_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct LearningModule {
    std::string id;
    std::string course_id;
    std::string title;
    std::string summary;
    int sort_order = 0;
    std::string status = "active"; // "active", "archived"
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"course_id", course_id},
            {"title", title},
            {"summary", summary},
            {"sort_order", sort_order},
            {"status", status},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct LearningLesson {
    std::string id;
    std::string course_id;
    std::string module_id;
    std::string slug;
    std::string title;
    std::string content_md;
    int estimated_minutes = 10;
    int sort_order = 0;
    bool is_preview = false;
    std::string status = "active"; // "active", "archived"
    std::string video_provider; // empty, "youtube", "vimeo"
    std::string video_id;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"course_id", course_id},
            {"module_id", module_id},
            {"slug", slug},
            {"title", title},
            {"content_md", content_md},
            {"estimated_minutes", estimated_minutes},
            {"sort_order", sort_order},
            {"is_preview", is_preview},
            {"status", status},
            {"video_provider", video_provider},
            {"video_id", video_id},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct LearningResource {
    std::string id;
    std::string course_id;
    std::string lesson_id; // empty for course-level
    std::string title;
    std::string description;
    std::string url;
    std::string kind = "link"; // "link", "download", "tool"
    int sort_order = 0;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"course_id", course_id},
            {"lesson_id", lesson_id},
            {"title", title},
            {"description", description},
            {"url", url},
            {"kind", kind},
            {"sort_order", sort_order},
            {"created_at", created_at}
        };
    }
};

struct QuizOption {
    std::string id;
    std::string question_id;
    std::string text;
    bool is_correct = false;
    int sort_order = 0;

    nlohmann::json to_json(bool include_answer_key = false) const {
        nlohmann::json j = {
            {"id", id},
            {"question_id", question_id},
            {"text", text},
            {"sort_order", sort_order}
        };
        if (include_answer_key) j["is_correct"] = is_correct;
        return j;
    }
};

struct QuizQuestion {
    std::string id;
    std::string quiz_id;
    std::string prompt;
    std::string explanation;
    int sort_order = 0;
    std::vector<QuizOption> options;
};

struct LessonQuiz {
    std::string id;
    std::string lesson_id;
    std::string course_id;
    std::string title;
    int passing_score = 70;
    std::vector<QuizQuestion> questions;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json(bool include_answer_key = false) const {
        nlohmann::json question_list = nlohmann::json::array();
        for (const auto& question : questions) {
            nlohmann::json options = nlohmann::json::array();
            for (const auto& option : question.options) {
                options.push_back(option.to_json(include_answer_key));
            }
            nlohmann::json q = {
                {"id", question.id},
                {"prompt", question.prompt},
                {"sort_order", question.sort_order},
                {"options", options}
            };
            // Explanations are only returned with a graded attempt or to an
            // author. They are never part of the learner's pre-attempt payload.
            if (include_answer_key) {
                q["explanation"] = question.explanation;
            }
            question_list.push_back(q);
        }
        return {
            {"id", id},
            {"lesson_id", lesson_id},
            {"course_id", course_id},
            {"title", title},
            {"passing_score", passing_score},
            {"questions", question_list},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct LearningPath {
    std::string id;
    std::string slug;
    std::string title;
    std::string description;
    std::string level = "beginner";
    std::string status = "draft"; // "draft", "published", "archived"
    int sort_order = 0;
    std::string published_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"slug", slug},
            {"title", title},
            {"description", description},
            {"level", level},
            {"status", status},
            {"sort_order", sort_order},
            {"published_at", published_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct Enrollment {
    std::string id;
    std::string user_id;
    std::string course_id;
    std::string status = "active"; // "active", "completed"
    std::string last_lesson_id;
    std::string enrolled_at;
    std::string completed_at;
    std::string updated_at;
    int completed_lessons = 0;
    int total_lessons = 0;

    nlohmann::json to_json() const {
        int percent = total_lessons == 0 ? 0
            : static_cast<int>((static_cast<long long>(completed_lessons) * 100) / total_lessons);
        if (status == "completed") percent = 100;
        return {
            {"id", id},
            {"course_id", course_id},
            {"status", status},
            {"last_lesson_id", last_lesson_id},
            {"enrolled_at", enrolled_at},
            {"completed_at", completed_at},
            {"updated_at", updated_at},
            {"completed_lessons", completed_lessons},
            {"total_lessons", total_lessons},
            {"progress_percent", percent}
        };
    }
};

struct QuizAttempt {
    std::string id;
    std::string enrollment_id;
    std::string user_id;
    std::string course_id;
    std::string lesson_id;
    std::string quiz_id;
    int score = 0;
    bool passed = false;
    int correct_count = 0;
    int total_questions = 0;
    std::string attempted_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"enrollment_id", enrollment_id},
            {"course_id", course_id},
            {"lesson_id", lesson_id},
            {"quiz_id", quiz_id},
            {"score", score},
            {"passed", passed},
            {"correct_count", correct_count},
            {"total_questions", total_questions},
            {"attempted_at", attempted_at}
        };
    }
};

struct CourseRevision {
    std::string id;
    std::string course_id;
    int version = 1;
    std::string created_by;
    std::string created_by_email;
    std::string note;
    std::string snapshot;
    std::string created_at;
};

struct AuditLog {
    std::string id;
    std::string actor;
    std::string action;      // e.g. "CREATE_SITE", "UPDATE_DESTINATION", "PAUSE_CAMPAIGN"
    std::string entity_type; // "site", "placement", "campaign", "key", "affiliate_offer", "publisher"
    std::string entity_id;
    std::string details;     // JSON string of changed values
    std::string created_at;

    nlohmann::json to_json() const {
        nlohmann::json details_json;
        try {
            details_json = nlohmann::json::parse(details);
        } catch (...) {
            details_json = details;
        }

        return {
            {"id", id},
            {"actor", actor},
            {"action", action},
            {"entity_type", entity_type},
            {"entity_id", entity_id},
            {"details", details_json},
            {"created_at", created_at}
        };
    }
};

} // namespace adcontrol
