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
    std::string source;
    std::string utm_source;
    std::string utm_medium;
    std::string utm_campaign;
    std::string utm_term;
    std::string utm_content;
    std::string referrer;
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
            {"source", source},
            {"utm_source", utm_source},
            {"utm_medium", utm_medium},
            {"utm_campaign", utm_campaign},
            {"utm_term", utm_term},
            {"utm_content", utm_content},
            {"referrer", referrer},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

// Administrator-managed SMTP transport for outbound email marketing. The
// password ciphertext is deliberately excluded from to_json(); callers only
// learn whether a password is stored.
struct EmailSettings {
    bool enabled = false;
    std::string host;
    int port = 587;
    std::string encryption = "starttls"; // "starttls", "ssl", or "none"
    std::string username;
    std::string password_cipher; // v1:<iv>:<tag>:<ciphertext>
    std::string from_email;
    std::string from_name;
    std::string reply_to;
    std::string updated_at;
    std::string updated_by;

    nlohmann::json to_json() const {
        return {
            {"enabled", enabled},
            {"host", host},
            {"port", port},
            {"encryption", encryption},
            {"username", username},
            {"password_set", !password_cipher.empty()},
            {"from_email", from_email},
            {"from_name", from_name},
            {"reply_to", reply_to},
            {"updated_at", updated_at},
            {"updated_by", updated_by}
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
    // Advertiser CPM in cents for direct campaigns. Campaigns without a CPM
    // still deliver; they simply do not generate publisher revenue share.
    int cpm_cents = 0;
    // 0 disables the daily budget. Budget is measured from same-day impressions
    // at the campaign CPM. Frequency capping is enforced by the browser SDK.
    int daily_budget_cents = 0;
    int frequency_cap_impressions = 0;
    int frequency_cap_window_hours = 24;
    std::string audience_segment_id;
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
            {"cpm_cents", cpm_cents},
            {"daily_budget_cents", daily_budget_cents},
            {"frequency_cap_impressions", frequency_cap_impressions},
            {"frequency_cap_window_hours", frequency_cap_window_hours},
            {"audience_segment_id", audience_segment_id},
            {"created_at", created_at},
            {"updated_at", updated_at},
            {"published_at", published_at}
        };
    }
};

struct AudienceSegment {
    std::string id;
    std::string site_id;
    std::string publisher_id;
    std::string name;
    std::string description;
    std::string rules = "{}";
    std::string status = "active";
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(rules.empty() ? "{}" : rules);
        } catch (...) {
            parsed = nlohmann::json::object();
        }
        return {
            {"id", id}, {"site_id", site_id}, {"publisher_id", publisher_id},
            {"name", name}, {"description", description}, {"rules", parsed},
            {"status", status}, {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct ExperimentVariant {
    std::string id;
    std::string experiment_id;
    std::string campaign_id;
    std::string name;
    int weight = 100;
    bool is_control = false;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"experiment_id", experiment_id}, {"campaign_id", campaign_id},
            {"name", name}, {"weight", weight}, {"is_control", is_control},
            {"created_at", created_at}
        };
    }
};

struct Experiment {
    std::string id;
    std::string site_id;
    std::string placement_id;
    std::string name;
    std::string description;
    std::string status = "draft"; // draft, running, paused, completed
    std::string objective = "ctr"; // ctr, conversion_rate, revenue
    int min_impressions = 100;
    int min_lift_percent = 10;
    bool auto_winner = true;
    std::string winner_campaign_id;
    std::string started_at;
    std::string ended_at;
    std::string created_at;
    std::string updated_at;
    std::vector<ExperimentVariant> variants;

    nlohmann::json to_json() const {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& variant : variants) rows.push_back(variant.to_json());
        return {
            {"id", id}, {"site_id", site_id}, {"placement_id", placement_id},
            {"name", name}, {"description", description}, {"status", status},
            {"objective", objective}, {"min_impressions", min_impressions},
            {"min_lift_percent", min_lift_percent}, {"auto_winner", auto_winner},
            {"winner_campaign_id", winner_campaign_id}, {"started_at", started_at},
            {"ended_at", ended_at}, {"created_at", created_at}, {"updated_at", updated_at},
            {"variants", rows}
        };
    }
};

// ---------------------------------------------------------------------------
// Monetization: plans, subscriptions, usage, invoices, and revenue share
// ---------------------------------------------------------------------------

struct BillingPlan {
    std::string id;   // free, pro, business, enterprise
    std::string name;
    std::string description;
    int price_cents = 0;
    std::string currency = "USD";
    std::string billing_interval = "month";
    // -1 means unlimited. A limit of 0 means the plan includes none.
    int included_campaigns = -1;
    int included_impressions = -1;
    // 0 disables overage and turns the impression allowance into a hard cap.
    int overage_per_1000_impressions_cents = 0;
    int revenue_share_percent = 0;
    std::vector<std::string> features;
    std::string status = "active"; // "active", "archived"
    int sort_order = 0;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"name", name},
            {"description", description},
            {"price_cents", price_cents},
            {"currency", currency},
            {"billing_interval", billing_interval},
            {"included_campaigns", included_campaigns},
            {"included_impressions", included_impressions},
            {"overage_per_1000_impressions_cents", overage_per_1000_impressions_cents},
            {"revenue_share_percent", revenue_share_percent},
            {"features", features},
            {"status", status},
            {"sort_order", sort_order},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct PublisherSubscription {
    std::string id;
    std::string publisher_id;
    std::string plan_id;
    std::string status = "active"; // "active", "past_due", "canceled"
    std::string current_period_start;
    std::string current_period_end;
    bool cancel_at_period_end = false;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"publisher_id", publisher_id},
            {"plan_id", plan_id},
            {"status", status},
            {"current_period_start", current_period_start},
            {"current_period_end", current_period_end},
            {"cancel_at_period_end", cancel_at_period_end},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct PublisherUsage {
    std::string period_start;
    std::string period_end;
    long long impressions = 0;
    int campaigns = 0;
    int included_impressions = -1;
    int included_campaigns = -1;
    long long overage_impressions = 0;
    long long overage_cents = 0;
    int revenue_share_percent = 0;
    bool hard_cap = false;
    bool impression_quota_exceeded = false;
    bool campaign_quota_exceeded = false;

    nlohmann::json to_json() const {
        return {
            {"period_start", period_start},
            {"period_end", period_end},
            {"impressions", impressions},
            {"campaigns", campaigns},
            {"included_impressions", included_impressions},
            {"included_campaigns", included_campaigns},
            {"overage_impressions", overage_impressions},
            {"overage_cents", overage_cents},
            {"revenue_share_percent", revenue_share_percent},
            {"hard_cap", hard_cap},
            {"impression_quota_exceeded", impression_quota_exceeded},
            {"campaign_quota_exceeded", campaign_quota_exceeded}
        };
    }
};

struct PublisherRevenueSummary {
    std::string period_start;
    std::string period_end;
    long long direct_impressions = 0;
    long long gross_cents = 0;
    int share_percent = 0;
    long long publisher_cents = 0;

    nlohmann::json to_json() const {
        return {
            {"period_start", period_start},
            {"period_end", period_end},
            {"direct_impressions", direct_impressions},
            {"gross_cents", gross_cents},
            {"share_percent", share_percent},
            {"publisher_cents", publisher_cents}
        };
    }
};

struct BillingInvoiceItem {
    std::string id;
    std::string invoice_id;
    std::string kind; // "subscription", "overage", "course", "adjustment"
    std::string description;
    int quantity = 1;
    int unit_amount_cents = 0;
    int amount_cents = 0;
    std::string course_id; // set for course purchases to grant entitlement
    std::string metadata = "{}";
    std::string created_at;

    nlohmann::json to_json() const {
        nlohmann::json meta;
        try {
            meta = nlohmann::json::parse(metadata.empty() ? "{}" : metadata);
        } catch (...) {
            meta = metadata;
        }
        return {
            {"id", id},
            {"invoice_id", invoice_id},
            {"kind", kind},
            {"description", description},
            {"quantity", quantity},
            {"unit_amount_cents", unit_amount_cents},
            {"amount_cents", amount_cents},
            {"course_id", course_id},
            {"metadata", meta},
            {"created_at", created_at}
        };
    }
};

struct BillingInvoice {
    std::string id;
    std::string publisher_id; // empty for learner course purchases
    std::string user_id;      // set for learner course purchases
    std::string context = "subscription"; // "subscription", "course", "manual"
    std::string period_start;
    std::string period_end;
    std::string currency = "USD";
    int subtotal_cents = 0;
    int tax_cents = 0;
    int total_cents = 0;
    int amount_paid_cents = 0;
    std::string status = "open"; // "open", "partially_paid", "paid", "overdue", "void"
    std::string due_at;
    std::string issued_at;
    std::string paid_at;
    std::string notes;
    std::string created_at;
    std::string updated_at;
    std::vector<BillingInvoiceItem> items;

    nlohmann::json to_json(bool include_items = true) const {
        nlohmann::json j = {
            {"id", id},
            {"publisher_id", publisher_id},
            {"user_id", user_id},
            {"context", context},
            {"period_start", period_start},
            {"period_end", period_end},
            {"currency", currency},
            {"subtotal_cents", subtotal_cents},
            {"tax_cents", tax_cents},
            {"total_cents", total_cents},
            {"amount_paid_cents", amount_paid_cents},
            {"balance_cents", total_cents > amount_paid_cents ? total_cents - amount_paid_cents : 0},
            {"status", status},
            {"due_at", due_at},
            {"issued_at", issued_at},
            {"paid_at", paid_at},
            {"notes", notes},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
        if (include_items) {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& item : items) rows.push_back(item.to_json());
            j["items"] = rows;
        }
        return j;
    }
};

struct BillingPayment {
    std::string id;
    std::string invoice_id;
    std::string publisher_id;
    std::string user_id;
    int amount_cents = 0;
    std::string currency = "USD";
    std::string method = "manual"; // "manual", "bank_transfer", "cash", "other"
    std::string reference;
    std::string notes;
    std::string received_at;
    std::string recorded_by;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"invoice_id", invoice_id},
            {"publisher_id", publisher_id},
            {"user_id", user_id},
            {"amount_cents", amount_cents},
            {"currency", currency},
            {"method", method},
            {"reference", reference},
            {"notes", notes},
            {"received_at", received_at},
            {"recorded_by", recorded_by},
            {"created_at", created_at}
        };
    }
};

struct CourseEntitlement {
    std::string id;
    std::string user_id;
    std::string course_id;
    std::string invoice_id;
    std::string status = "active"; // "active", "revoked"
    std::string granted_at;
    std::string revoked_at;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"user_id", user_id},
            {"course_id", course_id},
            {"invoice_id", invoice_id},
            {"status", status},
            {"granted_at", granted_at},
            {"revoked_at", revoked_at},
            {"created_at", created_at}
        };
    }
};

struct PublisherPayout {
    std::string id;
    std::string publisher_id;
    std::string period_start;
    std::string period_end;
    long long amount_cents = 0;
    std::string currency = "USD";
    std::string method = "manual";
    std::string reference;
    std::string status = "paid"; // "pending", "paid", "void"
    std::string paid_at;
    std::string recorded_by;
    std::string notes;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"publisher_id", publisher_id},
            {"period_start", period_start},
            {"period_end", period_end},
            {"amount_cents", amount_cents},
            {"currency", currency},
            {"method", method},
            {"reference", reference},
            {"status", status},
            {"paid_at", paid_at},
            {"recorded_by", recorded_by},
            {"notes", notes},
            {"created_at", created_at}
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
// Conversion tracking and attribution
// ---------------------------------------------------------------------------

struct ConversionSource {
    std::string id;
    std::string site_id;
    std::string publisher_id;
    std::string name;
    std::string public_key;
    // AES-GCM ciphertext; the plaintext signing secret is returned only once at
    // creation and is never serialized to clients.
    std::string encrypted_secret;
    std::string default_campaign_id;
    std::string default_offer_id;
    std::string status = "active"; // "active", "disabled"
    std::string last_event_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"site_id", site_id},
            {"publisher_id", publisher_id},
            {"name", name},
            {"public_key", public_key},
            {"default_campaign_id", default_campaign_id},
            {"default_offer_id", default_offer_id},
            {"status", status},
            {"last_event_at", last_event_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct ConversionEvent {
    std::string id;
    std::string source_id;
    std::string site_id;
    std::string publisher_id;
    std::string campaign_id;
    std::string offer_id;
    std::string external_id;
    std::string event_name = "purchase";
    std::string order_id;
    int value_cents = 0;
    int commission_cents = 0;
    std::string currency = "USD";
    std::string status = "pending"; // pending, approved, rejected, reconciled
    std::string ingestion = "sdk";  // sdk, pixel, webhook, server
    std::string visitor_id;
    std::string page_url;
    std::string referrer;
    std::string utm_source;
    std::string utm_medium;
    std::string utm_campaign;
    std::string utm_term;
    std::string utm_content;
    std::string first_touch = "{}";
    std::string last_touch = "{}";
    std::string clicked_campaign_id;
    std::string occurred_at;
    std::string reconciled_at;
    std::string notes;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        auto parse_touch = [](const std::string& raw) -> nlohmann::json {
            try {
                auto parsed = nlohmann::json::parse(raw.empty() ? "{}" : raw);
                return parsed.is_object() ? parsed : nlohmann::json::object();
            } catch (...) {
                return nlohmann::json::object();
            }
        };
        return {
            {"id", id},
            {"source_id", source_id},
            {"site_id", site_id},
            {"publisher_id", publisher_id},
            {"campaign_id", campaign_id},
            {"offer_id", offer_id},
            {"external_id", external_id},
            {"event_name", event_name},
            {"order_id", order_id},
            {"value_cents", value_cents},
            {"commission_cents", commission_cents},
            {"currency", currency},
            {"status", status},
            {"ingestion", ingestion},
            {"visitor_id", visitor_id},
            {"page_url", page_url},
            {"referrer", referrer},
            {"utm_source", utm_source},
            {"utm_medium", utm_medium},
            {"utm_campaign", utm_campaign},
            {"utm_term", utm_term},
            {"utm_content", utm_content},
            {"first_touch", parse_touch(first_touch)},
            {"last_touch", parse_touch(last_touch)},
            {"clicked_campaign_id", clicked_campaign_id},
            {"occurred_at", occurred_at},
            {"reconciled_at", reconciled_at},
            {"notes", notes},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

// ---------------------------------------------------------------------------
// Marketing automation
// ---------------------------------------------------------------------------

struct AutomationStep {
    std::string id;
    std::string workflow_id;
    std::string type; // "condition", "delay", "action"
    std::string config = "{}";
    int position = 0;
    std::string created_at;

    nlohmann::json to_json() const {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(config.empty() ? "{}" : config);
        } catch (...) {
            parsed = nlohmann::json::object();
        }
        return {
            {"id", id},
            {"workflow_id", workflow_id},
            {"type", type},
            {"config", parsed},
            {"position", position},
            {"created_at", created_at}
        };
    }
};

struct AutomationWorkflow {
    std::string id;
    std::string publisher_id;
    std::string site_id;
    std::string name;
    std::string description;
    std::string status = "draft"; // draft, active, paused
    std::string trigger_type = "manual"; // schedule, performance, conversion, inquiry, manual
    std::string trigger_config = "{}";
    std::string last_triggered_at;
    std::string next_run_at;
    std::string created_by;
    std::string created_at;
    std::string updated_at;
    std::vector<AutomationStep> steps;

    nlohmann::json to_json() const {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(trigger_config.empty() ? "{}" : trigger_config);
        } catch (...) {
            parsed = nlohmann::json::object();
        }
        nlohmann::json step_rows = nlohmann::json::array();
        for (const auto& step : steps) step_rows.push_back(step.to_json());
        return {
            {"id", id},
            {"publisher_id", publisher_id},
            {"site_id", site_id},
            {"name", name},
            {"description", description},
            {"status", status},
            {"trigger_type", trigger_type},
            {"trigger_config", parsed},
            {"last_triggered_at", last_triggered_at},
            {"next_run_at", next_run_at},
            {"created_by", created_by},
            {"created_at", created_at},
            {"updated_at", updated_at},
            {"steps", step_rows}
        };
    }
};

struct AutomationRun {
    std::string id;
    std::string workflow_id;
    std::string trigger_key;
    std::string status = "queued"; // queued, running, waiting, completed, failed, skipped
    std::string trigger_payload = "{}";
    int current_step = 0;
    std::string resume_at;
    std::string started_at;
    std::string completed_at;
    std::string error;
    std::string log;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(trigger_payload.empty() ? "{}" : trigger_payload);
        } catch (...) {
            payload = nlohmann::json::object();
        }
        return {
            {"id", id},
            {"workflow_id", workflow_id},
            {"trigger_key", trigger_key},
            {"status", status},
            {"trigger_payload", payload},
            {"current_step", current_step},
            {"resume_at", resume_at},
            {"started_at", started_at},
            {"completed_at", completed_at},
            {"error", error},
            {"log", log},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct AppNotification {
    std::string id;
    std::string user_id;
    std::string publisher_id;
    std::string kind = "info"; // info, warning, success, error
    std::string title;
    std::string body;
    std::string link;
    std::string read_at;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"user_id", user_id},
            {"publisher_id", publisher_id},
            {"kind", kind},
            {"title", title},
            {"body", body},
            {"link", link},
            {"read_at", read_at},
            {"created_at", created_at}
        };
    }
};

// ---------------------------------------------------------------------------
// CRM and lead management
// ---------------------------------------------------------------------------

struct CrmLead {
    std::string id;
    std::string inquiry_id;
    std::string name;
    std::string email;
    std::string company;
    std::string website;
    std::string phone;
    std::string source = "manual";
    std::string source_detail;
    std::string stage = "new"; // new, contacted, qualified, proposal, negotiation, won, lost
    int score = 0;
    std::string owner_user_id;
    std::string owner_email;
    int value_cents = 0;
    std::string currency = "USD";
    std::vector<std::string> tags;
    std::string notes;
    std::string utm_source;
    std::string utm_medium;
    std::string utm_campaign;
    std::string utm_term;
    std::string utm_content;
    std::string referrer;
    std::string last_contact_at;
    std::string next_follow_up_at;
    std::string converted_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"inquiry_id", inquiry_id},
            {"name", name},
            {"email", email},
            {"company", company},
            {"website", website},
            {"phone", phone},
            {"source", source},
            {"source_detail", source_detail},
            {"stage", stage},
            {"score", score},
            {"owner_user_id", owner_user_id},
            {"owner_email", owner_email},
            {"value_cents", value_cents},
            {"currency", currency},
            {"tags", tags},
            {"notes", notes},
            {"utm_source", utm_source},
            {"utm_medium", utm_medium},
            {"utm_campaign", utm_campaign},
            {"utm_term", utm_term},
            {"utm_content", utm_content},
            {"referrer", referrer},
            {"last_contact_at", last_contact_at},
            {"next_follow_up_at", next_follow_up_at},
            {"converted_at", converted_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

struct CrmActivity {
    std::string id;
    std::string lead_id;
    std::string type = "note"; // note, email, call, meeting, stage_change, system
    std::string subject;
    std::string body;
    std::string occurred_at;
    std::string created_by;
    std::string metadata = "{}";
    std::string created_at;

    nlohmann::json to_json() const {
        nlohmann::json meta;
        try {
            meta = nlohmann::json::parse(metadata.empty() ? "{}" : metadata);
        } catch (...) {
            meta = nlohmann::json::object();
        }
        return {
            {"id", id},
            {"lead_id", lead_id},
            {"type", type},
            {"subject", subject},
            {"body", body},
            {"occurred_at", occurred_at},
            {"created_by", created_by},
            {"metadata", meta},
            {"created_at", created_at}
        };
    }
};

struct CrmTask {
    std::string id;
    std::string lead_id;
    std::string title;
    std::string due_at;
    std::string status = "open"; // open, done, cancelled
    std::string assigned_to;
    std::string notes;
    std::string completed_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"lead_id", lead_id},
            {"title", title},
            {"due_at", due_at},
            {"status", status},
            {"assigned_to", assigned_to},
            {"notes", notes},
            {"completed_at", completed_at},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

// ---------------------------------------------------------------------------
// SEO and GEO intelligence
// ---------------------------------------------------------------------------

struct SeoProject {
    std::string id;
    std::string site_id;
    std::string publisher_id;
    std::string name;
    std::string domain;
    std::string country = "US";
    std::string language = "en";
    std::string status = "active";
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"site_id", site_id}, {"publisher_id", publisher_id},
            {"name", name}, {"domain", domain}, {"country", country},
            {"language", language}, {"status", status},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct SeoKeyword {
    std::string id;
    std::string project_id;
    std::string keyword;
    std::string target_url;
    std::string country = "US";
    std::string device = "desktop";
    std::string intent = "informational";
    int priority = 2;
    int search_volume = 0;
    int difficulty = 0;
    int current_position = 0;
    int previous_position = 0;
    std::string last_checked_at;
    std::string source = "manual";
    std::vector<std::string> tags;
    std::string status = "active";
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"project_id", project_id}, {"keyword", keyword},
            {"target_url", target_url}, {"country", country}, {"device", device},
            {"intent", intent}, {"priority", priority}, {"search_volume", search_volume},
            {"difficulty", difficulty}, {"current_position", current_position},
            {"previous_position", previous_position}, {"last_checked_at", last_checked_at},
            {"source", source}, {"tags", tags}, {"status", status},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct SeoRanking {
    std::string id;
    std::string keyword_id;
    int position = 0;
    std::string url;
    std::string checked_at;
    std::string source = "manual";
    std::string note;
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"keyword_id", keyword_id}, {"position", position},
            {"url", url}, {"checked_at", checked_at}, {"source", source},
            {"note", note}, {"created_at", created_at}
        };
    }
};

struct SeoCompetitor {
    std::string id;
    std::string project_id;
    std::string domain;
    std::string name;
    std::string notes;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"project_id", project_id}, {"domain", domain},
            {"name", name}, {"notes", notes},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct SeoCompetitorKeyword {
    std::string id;
    std::string competitor_id;
    std::string keyword;
    int position = 0;
    int search_volume = 0;
    std::string url;
    std::string checked_at;
    std::string source = "manual";
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"competitor_id", competitor_id}, {"keyword", keyword},
            {"position", position}, {"search_volume", search_volume}, {"url", url},
            {"checked_at", checked_at}, {"source", source}, {"created_at", created_at}
        };
    }
};

struct SeoBacklink {
    std::string id;
    std::string project_id;
    std::string source_url;
    std::string target_url;
    std::string anchor_text;
    std::string domain;
    std::string status = "active"; // active, lost, pending
    bool nofollow = false;
    std::string first_seen_at;
    std::string last_checked_at;
    std::string notes;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"project_id", project_id}, {"source_url", source_url},
            {"target_url", target_url}, {"anchor_text", anchor_text}, {"domain", domain},
            {"status", status}, {"nofollow", nofollow}, {"first_seen_at", first_seen_at},
            {"last_checked_at", last_checked_at}, {"notes", notes},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct SeoAuditIssue {
    std::string id;
    std::string audit_id;
    std::string severity = "medium"; // critical, high, medium, low, info
    std::string category;
    std::string code;
    std::string message;
    std::string recommendation;
    std::string page_url;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"audit_id", audit_id}, {"severity", severity},
            {"category", category}, {"code", code}, {"message", message},
            {"recommendation", recommendation}, {"page_url", page_url}
        };
    }
};

struct SeoAudit {
    std::string id;
    std::string project_id;
    std::string target_url;
    std::string status = "completed";
    int score = 0;
    std::string checked_at;
    std::string summary = "{}";
    std::string created_at;
    std::vector<SeoAuditIssue> issues;

    nlohmann::json to_json(bool include_issues = false) const {
        nlohmann::json summary_json;
        try {
            summary_json = nlohmann::json::parse(summary.empty() ? "{}" : summary);
        } catch (...) {
            summary_json = nlohmann::json::object();
        }
        nlohmann::json j = {
            {"id", id}, {"project_id", project_id}, {"target_url", target_url},
            {"status", status}, {"score", score}, {"checked_at", checked_at},
            {"summary", summary_json}, {"created_at", created_at}
        };
        if (include_issues) {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& issue : issues) rows.push_back(issue.to_json());
            j["issues"] = rows;
        }
        return j;
    }
};

struct GeoQuery {
    std::string id;
    std::string project_id;
    std::string query_text;
    std::vector<std::string> engines;
    std::string country = "US";
    std::string status = "active";
    std::string last_checked_at;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"project_id", project_id}, {"query_text", query_text},
            {"engines", engines}, {"country", country}, {"status", status},
            {"last_checked_at", last_checked_at},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }
};

struct GeoMention {
    std::string id;
    std::string query_id;
    std::string engine;
    bool mentioned = false;
    int position = 0;
    std::string sentiment;
    std::string snippet;
    std::string checked_at;
    std::string source = "manual";
    std::string created_at;

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"query_id", query_id}, {"engine", engine},
            {"mentioned", mentioned}, {"position", position}, {"sentiment", sentiment},
            {"snippet", snippet}, {"checked_at", checked_at}, {"source", source},
            {"created_at", created_at}
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
    // Paid courses create a manual invoice and require an active entitlement
    // before enrollment. Free courses keep their original behavior.
    int price_cents = 0;
    std::string currency = "USD";
    std::string access_type = "free"; // "free", "paid"
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
            {"price_cents", price_cents},
            {"currency", currency},
            {"access_type", access_type},
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
