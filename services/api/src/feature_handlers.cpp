#include "handlers.hpp"
#include "automation.hpp"
#include "smtp.hpp"
#include "utils.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <openssl/crypto.h>

namespace adcontrol {

namespace {

constexpr size_t CONVERSION_MAX_BODY_BYTES = 16 * 1024;
constexpr const char* CONVERSION_GIF =
    "GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff!"
    "\xf9\x04\x01\x00\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02"
    "\x02D\x01\x00;";

void json_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(nlohmann::json({{"error", message}}).dump(), "application/json");
}

void set_no_store(httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
}

bool parse_json_body(const httplib::Request& req, httplib::Response& res,
                     nlohmann::json& body, size_t max_bytes = CONVERSION_MAX_BODY_BYTES) {
    if (req.body.size() > max_bytes) {
        json_error(res, 413, "Request body too large");
        return false;
    }
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        json_error(res, 400, "Invalid JSON");
        return false;
    }
    if (!body.is_object()) {
        json_error(res, 400, "JSON body must be an object");
        return false;
    }
    return true;
}

bool parse_int(const std::string& value, int& out) {
    if (value.empty()) return false;
    try {
        size_t pos = 0;
        long parsed = std::stol(value, &pos);
        if (pos != value.size()) return false;
        out = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool is_writer(const User& user) {
    return user.role == "admin" || user.role == "publisher";
}

bool is_admin(const User& user) {
    return user.role == "admin";
}

bool can_access_site(const User& user, const Site& site) {
    if (is_admin(user)) return true;
    return !user.publisher_id.empty() && site.publisher_id == user.publisher_id;
}

bool scope_for_read(const User& user, const httplib::Request& req,
                    std::optional<std::string>& publisher_id, httplib::Response& res) {
    if (is_admin(user)) {
        if (req.has_param("publisher_id")) publisher_id = req.get_param_value("publisher_id");
        return true;
    }
    if (user.publisher_id.empty()) {
        json_error(res, 403, "Forbidden: account is not linked to a publisher");
        return false;
    }
    publisher_id = user.publisher_id;
    if (req.has_param("publisher_id") && req.get_param_value("publisher_id") != user.publisher_id) {
        json_error(res, 403, "Forbidden: publisher scope mismatch");
        return false;
    }
    return true;
}

std::string lower_trim(std::string value) {
    return utils::to_lower_copy(utils::trim_copy(value));
}

std::string truncate_utf8(const std::string& value, size_t max_bytes) {
    if (value.size() <= max_bytes) return value;
    size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) --cut;
    return value.substr(0, cut);
}

std::string safe_string(const nlohmann::json& body, const char* key, size_t max_bytes = 512) {
    if (!body.contains(key) || body[key].is_null()) return "";
    std::string value;
    try {
        if (body[key].is_string()) value = body[key].get<std::string>();
        else if (body[key].is_number()) value = body[key].dump();
    } catch (...) {
        return "";
    }
    return truncate_utf8(utils::trim_copy(value), max_bytes);
}

std::string normalized_url_field(const nlohmann::json& body, const char* key) {
    std::string value = safe_string(body, key, 2048);
    if (value.empty()) return "";
    return utils::is_safe_url(value) ? value : "";
}

int amount_cents(const nlohmann::json& body, const char* cents_key, const char* decimal_key) {
    if (body.contains(cents_key)) {
        try {
            if (body[cents_key].is_number_integer()) {
                return std::max(0, body[cents_key].get<int>());
            }
            if (body[cents_key].is_string()) {
                int parsed = 0;
                if (parse_int(body[cents_key].get<std::string>(), parsed)) return std::max(0, parsed);
            }
        } catch (...) {}
    }
    if (body.contains(decimal_key)) {
        try {
            double parsed = body[decimal_key].is_string()
                ? std::stod(body[decimal_key].get<std::string>())
                : body[decimal_key].get<double>();
            if (parsed >= 0 && parsed < 100000000.0) {
                return static_cast<int>(std::llround(parsed * 100.0));
            }
        } catch (...) {}
    }
    return 0;
}

std::string touch_json(const nlohmann::json& body, const char* key,
                       const nlohmann::json& fallback) {
    nlohmann::json touch = nlohmann::json::object();
    if (body.contains(key) && body[key].is_object()) {
        touch = body[key];
    } else {
        touch = fallback;
    }
    std::string serialized = touch.dump();
    if (serialized.size() > 4096) return "{}";
    return serialized;
}

std::string touch_field(const nlohmann::json& touch, const char* key, size_t max_len = 160) {
    if (!touch.is_object() || !touch.contains(key)) return "";
    if (!touch[key].is_string()) return "";
    return truncate_utf8(utils::trim_copy(touch[key].get<std::string>()), max_len);
}

nlohmann::json touch_from_event(const ConversionEvent& event) {
    nlohmann::json touch = {
        {"utm_source", event.utm_source},
        {"utm_medium", event.utm_medium},
        {"utm_campaign", event.utm_campaign},
        {"utm_term", event.utm_term},
        {"utm_content", event.utm_content},
        {"page_url", event.page_url},
        {"referrer", event.referrer},
        {"occurred_at", event.occurred_at}
    };
    return touch;
}

bool build_conversion(Database& db, const ConversionSource& source,
                      const nlohmann::json& body, const std::string& ingestion,
                      const std::string& fallback_external_id,
                      bool require_external_id, ConversionEvent& event,
                      std::string& error) {
    event = ConversionEvent();
    event.source_id = source.id;
    event.site_id = source.site_id;
    event.publisher_id = source.publisher_id;
    event.ingestion = ingestion;

    std::string campaign_id = safe_string(body, "campaign_id", 80);
    if (campaign_id.empty()) campaign_id = source.default_campaign_id;
    if (!campaign_id.empty()) {
        auto campaign = db.get_campaign(campaign_id);
        if (!campaign || campaign->site_id != source.site_id) {
            error = "campaign_id does not belong to this conversion source";
            return false;
        }
        event.campaign_id = campaign_id;
        if (event.offer_id.empty() && campaign->source_type == "affiliate") {
            event.offer_id = campaign->affiliate_offer_id;
        }
    }

    std::string offer_id = safe_string(body, "offer_id", 80);
    if (!offer_id.empty()) event.offer_id = offer_id;
    if (event.offer_id.empty()) event.offer_id = source.default_offer_id;

    event.external_id = safe_string(body, "external_id", 160);
    if (event.external_id.empty()) event.external_id = safe_string(body, "order_id", 160);
    if (event.external_id.empty()) event.external_id = fallback_external_id;
    if (event.external_id.empty()) {
        if (require_external_id) {
            error = "external_id or order_id is required for idempotent conversion ingestion";
            return false;
        }
        event.external_id = "auto-" + utils::generate_uuid();
    }
    static const std::regex external_re(R"(^[A-Za-z0-9._:@+\-]{1,160}$)");
    if (!std::regex_match(event.external_id, external_re)) {
        error = "external_id contains unsupported characters";
        return false;
    }

    event.event_name = lower_trim(safe_string(body, "event_name", 64));
    if (event.event_name.empty()) event.event_name = "purchase";
    static const std::regex event_name_re(R"(^[a-z0-9][a-z0-9_-]{0,63}$)");
    if (!std::regex_match(event.event_name, event_name_re)) {
        error = "event_name must use lowercase letters, digits, underscore, or hyphen";
        return false;
    }
    event.order_id = safe_string(body, "order_id", 160);
    event.value_cents = amount_cents(body, "value_cents", "value");
    event.commission_cents = amount_cents(body, "commission_cents", "commission");
    event.currency = utils::to_upper_copy(safe_string(body, "currency", 3));
    if (event.currency.empty()) event.currency = "USD";
    static const std::regex currency_re(R"(^[A-Z]{3}$)");
    if (!std::regex_match(event.currency, currency_re)) {
        error = "currency must be a three-letter ISO code";
        return false;
    }

    event.visitor_id = safe_string(body, "visitor_id", 128);
    static const std::regex visitor_re(R"(^[A-Za-z0-9._:\-]{8,128}$)");
    if (!event.visitor_id.empty() && !std::regex_match(event.visitor_id, visitor_re)) {
        event.visitor_id.clear();
    }

    event.page_url = normalized_url_field(body, "page_url");
    event.referrer = normalized_url_field(body, "referrer");
    event.utm_source = safe_string(body, "utm_source", 160);
    event.utm_medium = safe_string(body, "utm_medium", 160);
    event.utm_campaign = safe_string(body, "utm_campaign", 160);
    event.utm_term = safe_string(body, "utm_term", 160);
    event.utm_content = safe_string(body, "utm_content", 160);
    event.clicked_campaign_id = safe_string(body, "clicked_campaign_id", 80);
    if (!event.clicked_campaign_id.empty()) {
        auto clicked = db.get_campaign(event.clicked_campaign_id);
        if (!clicked || clicked->site_id != source.site_id) event.clicked_campaign_id.clear();
    }

    std::string occurred = safe_string(body, "occurred_at", 40);
    event.occurred_at = occurred.empty() ? utils::get_current_iso8601()
                                         : utils::normalize_iso8601_utc(occurred);
    if (event.occurred_at.empty()) event.occurred_at = utils::get_current_iso8601();

    nlohmann::json fallback_touch = touch_from_event(event);
    event.first_touch = touch_json(body, "first_touch", fallback_touch);
    event.last_touch = touch_json(body, "last_touch", fallback_touch);
    if (event.utm_source.empty() && event.utm_medium.empty() && event.utm_campaign.empty()) {
        try {
            auto touch = nlohmann::json::parse(event.last_touch);
            event.utm_source = touch_field(touch, "utm_source");
            event.utm_medium = touch_field(touch, "utm_medium");
            event.utm_campaign = touch_field(touch, "utm_campaign");
            event.utm_term = touch_field(touch, "utm_term");
            event.utm_content = touch_field(touch, "utm_content");
        } catch (...) {}
    }
    event.notes = safe_string(body, "notes", 1000);
    return true;
}

bool signature_matches(const std::string& secret, const std::string& body,
                       const std::string& provided) {
    if (secret.empty() || provided.empty()) return false;
    std::string value = provided;
    const std::string prefix = "sha256=";
    if (value.rfind(prefix, 0) == 0) value = value.substr(prefix.size());
    const std::string expected = utils::hmac_sha256_hex(secret, body);
    if (value.size() != expected.size()) return false;
    return CRYPTO_memcmp(value.data(), expected.data(), value.size()) == 0;
}

bool valid_conversion_status(const std::string& status) {
    return status == "pending" || status == "approved" || status == "rejected" ||
           status == "reconciled";
}

bool valid_source_status(const std::string& status) {
    return status == "active" || status == "disabled";
}

void audit(Database& db, const std::string& actor, const std::string& action,
           const std::string& entity_type, const std::string& entity_id,
           const nlohmann::json& details) {
    AuditLog log;
    log.actor = actor;
    log.action = action;
    log.entity_type = entity_type;
    log.entity_id = entity_id;
    log.details = details.dump();
    db.record_audit_log(log);
}

bool is_valid_automation_status(const std::string& status) {
    return status == "draft" || status == "active" || status == "paused";
}

bool is_valid_trigger_type(const std::string& type) {
    return type == "schedule" || type == "performance" || type == "conversion" ||
           type == "inquiry" || type == "manual";
}

bool is_valid_step_type(const std::string& type) {
    return type == "condition" || type == "delay" || type == "action";
}

bool is_valid_action_type(const std::string& type) {
    return type == "pause_campaign" || type == "activate_campaign" ||
           type == "send_notification" || type == "webhook";
}

bool action_campaign_in_scope(Database& db, const User& user, const AutomationWorkflow& workflow,
                              const std::string& campaign_id, std::string& error) {
    auto campaign = db.get_campaign(campaign_id);
    if (!campaign) {
        error = "Campaign not found";
        return false;
    }
    if (!workflow.site_id.empty() && campaign->site_id != workflow.site_id) {
        error = "Campaign does not belong to the workflow site";
        return false;
    }
    auto site = db.get_site(campaign->site_id);
    if (!site || !can_access_site(user, *site)) {
        error = "Campaign is outside your tenant";
        return false;
    }
    if (!workflow.publisher_id.empty() && site->publisher_id != workflow.publisher_id) {
        error = "Campaign does not belong to the workflow publisher";
        return false;
    }
    return true;
}

bool parse_automation_workflow(Database& db, const User& user, const nlohmann::json& body,
                               bool creating, AutomationWorkflow& workflow,
                               std::string& error) {
    auto fail = [&](const std::string& message) {
        error = message;
        return false;
    };

    if (body.contains("name") || creating) {
        workflow.name = safe_string(body, "name", 120);
        if (workflow.name.size() < 2) return fail("name must be between 2 and 120 characters");
    }
    if (body.contains("description")) {
        workflow.description = safe_string(body, "description", 1000);
    }

    if (body.contains("site_id") || creating) {
        const std::string site_id = safe_string(body, "site_id", 80);
        if (site_id.empty()) {
            if (!is_admin(user)) return fail("a publisher workflow must target a site");
            workflow.site_id.clear();
            workflow.publisher_id.clear();
        } else {
            auto site = db.get_site(site_id);
            if (!site || !can_access_site(user, *site)) return fail("Site not found");
            workflow.site_id = site->id;
            workflow.publisher_id = site->publisher_id;
        }
    }
    if (!is_admin(user)) {
        if (workflow.site_id.empty()) return fail("a publisher workflow must target a site");
        if (workflow.publisher_id != user.publisher_id) return fail("Site is outside your tenant");
    }

    if (body.contains("status") || creating) {
        const std::string status = lower_trim(safe_string(body, "status", 20));
        workflow.status = status.empty() ? "draft" : status;
        if (!is_valid_automation_status(workflow.status)) {
            return fail("status must be draft, active, or paused");
        }
    }
    if (body.contains("trigger_type") || creating) {
        const std::string trigger_type = lower_trim(safe_string(body, "trigger_type", 30));
        workflow.trigger_type = trigger_type.empty() ? "manual" : trigger_type;
        if (!is_valid_trigger_type(workflow.trigger_type)) {
            return fail("trigger_type must be schedule, performance, conversion, inquiry, or manual");
        }
    }

    if (body.contains("trigger_config") || creating) {
        if (body.contains("trigger_config") && !body["trigger_config"].is_object()) {
            return fail("trigger_config must be an object");
        }
        workflow.trigger_config = body.contains("trigger_config")
            ? body["trigger_config"].dump() : "{}";
    }
    nlohmann::json trigger_config;
    try {
        trigger_config = nlohmann::json::parse(workflow.trigger_config.empty() ? "{}" : workflow.trigger_config);
    } catch (...) {
        return fail("trigger_config is not valid JSON");
    }
    if (!trigger_config.is_object()) return fail("trigger_config must be an object");

    if (workflow.trigger_type == "schedule") {
        std::string run_at = safe_string(trigger_config, "run_at", 40);
        std::string normalized = utils::normalize_iso8601_utc(run_at);
        if (normalized.empty()) return fail("schedule trigger requires a valid run_at timestamp");
        std::string recurrence = lower_trim(trigger_config.value("recurrence", "none"));
        if (recurrence != "none" && recurrence != "hourly" && recurrence != "daily" &&
            recurrence != "weekly") {
            return fail("recurrence must be none, hourly, daily, or weekly");
        }
        trigger_config["run_at"] = normalized;
        trigger_config["recurrence"] = recurrence;
        workflow.next_run_at = normalized;
    } else if (workflow.trigger_type == "performance") {
        std::string metric = lower_trim(trigger_config.value("metric", "ctr"));
        if (metric != "ctr" && metric != "conversion_rate" && metric != "impressions" &&
            metric != "clicks" && metric != "conversions" && metric != "commission_cents") {
            return fail("performance metric is not supported");
        }
        std::string comparator = lower_trim(trigger_config.value("comparator", "lt"));
        if (comparator != "lt" && comparator != "lte" && comparator != "gt" &&
            comparator != "gte" && comparator != "eq") {
            return fail("performance comparator is not supported");
        }
        if (!trigger_config.contains("threshold") || !trigger_config["threshold"].is_number()) {
            return fail("performance trigger requires a numeric threshold");
        }
        int lookback_hours = trigger_config.value("lookback_hours", 24);
        if (lookback_hours < 1 || lookback_hours > 720) {
            return fail("lookback_hours must be between 1 and 720");
        }
        int cooldown_minutes = trigger_config.value("cooldown_minutes", 60);
        if (cooldown_minutes < 1 || cooldown_minutes > 10080) {
            return fail("cooldown_minutes must be between 1 and 10080");
        }
        int min_impressions = trigger_config.value("min_impressions", 100);
        if (min_impressions < 0) return fail("min_impressions cannot be negative");
        std::string scope_site = workflow.site_id;
        if (trigger_config.contains("site_id") && trigger_config["site_id"].is_string()) {
            scope_site = trigger_config["site_id"].get<std::string>();
        }
        std::string scope_campaign = trigger_config.value("campaign_id", "");
        if (!scope_campaign.empty()) {
            if (!action_campaign_in_scope(db, user, workflow, scope_campaign, error)) return false;
        }
        if (scope_site.empty() && scope_campaign.empty()) {
            return fail("performance trigger requires a site_id or campaign_id scope");
        }
        if (!scope_site.empty() && !is_admin(user)) {
            auto site = db.get_site(scope_site);
            if (!site || !can_access_site(user, *site)) return fail("performance site is outside your tenant");
        }
        trigger_config["metric"] = metric;
        trigger_config["comparator"] = comparator;
        trigger_config["lookback_hours"] = lookback_hours;
        trigger_config["cooldown_minutes"] = cooldown_minutes;
        trigger_config["min_impressions"] = min_impressions;
    } else if (workflow.trigger_type == "conversion") {
        std::string campaign_id = trigger_config.value("campaign_id", "");
        if (!campaign_id.empty() &&
            !action_campaign_in_scope(db, user, workflow, campaign_id, error)) {
            return false;
        }
    }

    if (body.contains("steps") || creating) {
        if (body.contains("steps") && !body["steps"].is_array()) {
            return fail("steps must be an array");
        }
        workflow.steps.clear();
        if (body.contains("steps")) {
            if (body["steps"].size() > 25) return fail("at most 25 steps are allowed");
            for (size_t index = 0; index < body["steps"].size(); ++index) {
                const auto& raw = body["steps"][index];
                if (!raw.is_object()) return fail("each step must be an object");
                AutomationStep step;
                step.type = lower_trim(raw.value("type", ""));
                if (!is_valid_step_type(step.type)) {
                    return fail("step type must be condition, delay, or action");
                }
                if (raw.contains("config") && !raw["config"].is_object()) {
                    return fail("step config must be an object");
                }
                nlohmann::json config = raw.contains("config") ? raw["config"] : nlohmann::json::object();
                if (step.type == "delay") {
                    int amount = 1;
                    try { amount = config.value("amount", 1); } catch (...) {}
                    if (amount < 1 || amount > 100000) return fail("delay amount must be between 1 and 100000");
                    const std::string unit = lower_trim(config.value("unit", "minutes"));
                    if (unit != "seconds" && unit != "minutes" && unit != "hours" && unit != "days") {
                        return fail("delay unit must be seconds, minutes, hours, or days");
                    }
                    config["amount"] = amount;
                    config["unit"] = unit;
                } else if (step.type == "condition") {
                    const std::string metric = lower_trim(config.value("metric", "ctr"));
                    if (metric != "ctr" && metric != "conversion_rate" && metric != "impressions" &&
                        metric != "clicks" && metric != "conversions" && metric != "commission_cents") {
                        return fail("condition metric is not supported");
                    }
                    const std::string comparator = lower_trim(config.value("comparator", "lt"));
                    if (comparator != "lt" && comparator != "lte" && comparator != "gt" &&
                        comparator != "gte" && comparator != "eq") {
                        return fail("condition comparator is not supported");
                    }
                    if (!config.contains("threshold") || !config["threshold"].is_number()) {
                        return fail("condition requires a numeric threshold");
                    }
                } else {
                    const std::string action = lower_trim(config.value("action", config.value("action_type", "")));
                    if (!is_valid_action_type(action)) {
                        return fail("action must be pause_campaign, activate_campaign, send_notification, or webhook");
                    }
                    config["action"] = action;
                    if (action == "pause_campaign" || action == "activate_campaign") {
                        const std::string campaign_id = config.value("campaign_id", "");
                        if (campaign_id.empty()) return fail("campaign actions require a campaign_id");
                        if (!action_campaign_in_scope(db, user, workflow, campaign_id, error)) return false;
                    } else if (action == "webhook") {
                        const std::string url = config.value("url", "");
                        if (url.size() < 12 || url.size() > 2048 ||
                            utils::to_lower_copy(url).rfind("https://", 0) != 0 ||
                            !utils::is_safe_url(url)) {
                            return fail("webhook actions require a public HTTPS URL");
                        }
                    } else if (action == "send_notification") {
                        std::string title = config.value("title", "");
                        if (title.empty()) return fail("notification actions require a title");
                        if (title.size() > 160) title = title.substr(0, 160);
                        config["title"] = title;
                        if (config.contains("body") && config["body"].is_string() &&
                            config["body"].get<std::string>().size() > 2000) {
                            return fail("notification body must be 2000 characters or fewer");
                        }
                    }
                }
                step.config = config.dump();
                step.position = static_cast<int>(index);
                workflow.steps.push_back(step);
            }
        }
    }
    if (workflow.status == "active" && workflow.steps.empty()) {
        return fail("an active workflow requires at least one step");
    }
    workflow.trigger_config = trigger_config.dump();
    return true;
}

bool can_manage_automation_workflow(const User& user, const AutomationWorkflow& workflow) {
    if (is_admin(user)) return true;
    return !user.publisher_id.empty() && workflow.publisher_id == user.publisher_id;
}

void audit_automation(Database& db, const User& user, const std::string& action,
                      const std::string& id, const nlohmann::json& details) {
    AuditLog log;
    log.actor = user.email;
    log.action = action;
    log.entity_type = "automation_workflow";
    log.entity_id = id;
    log.details = details.dump();
    db.record_audit_log(log);
}

bool is_valid_crm_stage(const std::string& stage) {
    return stage == "new" || stage == "contacted" || stage == "qualified" ||
           stage == "proposal" || stage == "negotiation" || stage == "won" ||
           stage == "lost";
}

bool is_valid_crm_task_status(const std::string& status) {
    return status == "open" || status == "done" || status == "cancelled";
}

std::string html_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string html_with_line_breaks(const std::string& text) {
    std::string escaped = html_escape(text);
    std::string out;
    for (char c : escaped) {
        if (c == '\n') out += "<br>";
        else out += c;
    }
    return out;
}

bool load_smtp_transport(Database& db, const std::string& secret_key,
                         smtp::Settings& transport, std::string& error) {
    auto stored = db.get_email_settings();
    if (!stored || stored->host.empty() || stored->from_email.empty()) {
        error = "SMTP is not configured. Save the mail transport in Settings first.";
        return false;
    }
    if (!stored->enabled) {
        error = "SMTP is disabled. Enable it in Settings before sending.";
        return false;
    }
    std::string password;
    if (!stored->password_cipher.empty()) {
        auto decrypted = utils::decrypt_secret(stored->password_cipher, secret_key);
        if (!decrypted) {
            error = "The stored SMTP password could not be decrypted. Re-enter it in Settings.";
            return false;
        }
        password = *decrypted;
    }
    if (!stored->username.empty() && password.empty()) {
        error = "The SMTP username has no stored password. Re-enter it in Settings.";
        return false;
    }
    transport.enabled = true;
    transport.host = stored->host;
    transport.port = stored->port;
    transport.encryption = stored->encryption;
    transport.username = stored->username;
    transport.password = password;
    transport.from_email = stored->from_email;
    transport.from_name = stored->from_name;
    transport.reply_to = stored->reply_to;
    return true;
}

void audit_crm(Database& db, const User& user, const std::string& action,
               const std::string& id, const nlohmann::json& details) {
    AuditLog log;
    log.actor = user.email;
    log.action = action;
    log.entity_type = "crm_lead";
    log.entity_id = id;
    log.details = details.dump();
    db.record_audit_log(log);
}

bool can_access_seo_project(const User& user, const SeoProject& project) {
    if (is_admin(user)) return true;
    return !user.publisher_id.empty() && project.publisher_id == user.publisher_id;
}

void audit_seo(Database& db, const User& user, const std::string& action,
               const std::string& id, const nlohmann::json& details) {
    AuditLog log;
    log.actor = user.email;
    log.action = action;
    log.entity_type = "seo_project";
    log.entity_id = id;
    log.details = details.dump();
    db.record_audit_log(log);
}

bool is_public_ipv4(const sockaddr_in* addr) {
    const uint32_t ip = ntohl(addr->sin_addr.s_addr);
    if ((ip & 0xFF000000U) == 0x00000000U) return false;
    if ((ip & 0xFF000000U) == 0x0A000000U) return false;
    if ((ip & 0xFF000000U) == 0x7F000000U) return false;
    if ((ip & 0xFFFF0000U) == 0xA9FE0000U) return false;
    if ((ip & 0xFFF00000U) == 0xAC100000U) return false;
    if ((ip & 0xFFFF0000U) == 0xC0A80000U) return false;
    if ((ip & 0xFFC00000U) == 0x64400000U) return false;
    if ((ip & 0xFFFE0000U) == 0xC6120000U) return false;
    if ((ip & 0xFFFFFF00U) == 0xC0000200U) return false;
    if ((ip & 0xFFFFFF00U) == 0xC6336400U) return false;
    if ((ip & 0xFFFFFF00U) == 0xCB007100U) return false;
    if ((ip & 0xF0000000U) == 0xE0000000U) return false;
    if (ip == 0xFFFFFFFFU) return false;
    return true;
}

bool is_public_ipv6(const sockaddr_in6* addr) {
    const unsigned char* bytes = addr->sin6_addr.s6_addr;
    if (IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_UNSPECIFIED(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_SITELOCAL(&addr->sin6_addr)) return false;
    if ((bytes[0] & 0xFE) == 0xFC) return false;
    if (bytes[0] == 0xFF) return false;
    if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0D && bytes[3] == 0xB8) return false;
    if (IN6_IS_ADDR_V4MAPPED(&addr->sin6_addr)) {
        sockaddr_in mapped{};
        mapped.sin_family = AF_INET;
        std::memcpy(&mapped.sin_addr, bytes + 12, 4);
        return is_public_ipv4(&mapped);
    }
    return true;
}

bool safe_seo_target(const std::string& url, const Site& site,
                     std::string& host, std::string& path, std::string& pinned_ip,
                     std::string& error) {
    if (url.size() < 12 || url.size() > 4096) {
        error = "URL is required and must be 4096 characters or fewer";
        return false;
    }
    if (utils::to_lower_copy(url).rfind("https://", 0) != 0) {
        error = "SEO audits only fetch HTTPS URLs";
        return false;
    }
    const std::string rest = url.substr(8);
    const size_t path_start = rest.find_first_of("/?#");
    const std::string authority = rest.substr(0, path_start);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find('[') != std::string::npos) {
        error = "URL must contain a public hostname without credentials";
        return false;
    }
    host = authority;
    std::string port = "443";
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
    }
    host = utils::to_lower_copy(host);
    if (host.empty() || host.find('.') == std::string::npos ||
        host == "localhost" || host.rfind("localhost.", 0) == 0) {
        error = "URL must reference a public hostname";
        return false;
    }
    std::vector<std::string> domains = site.allowed_domains.empty()
        ? std::vector<std::string>{site.primary_domain} : site.allowed_domains;
    if (!utils::is_domain_allowed(host, domains)) {
        error = "URL host is not part of this site's allowlist";
        return false;
    }
    if (!utils::is_valid_domain_entry(host)) {
        error = "URL host is invalid";
        return false;
    }
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &resolved) != 0 || !resolved) {
        error = "URL host could not be resolved";
        return false;
    }
    bool found = false;
    for (addrinfo* it = resolved; it; it = it->ai_next) {
        if (it->ai_family == AF_INET) {
            if (!is_public_ipv4(reinterpret_cast<const sockaddr_in*>(it->ai_addr))) {
                found = false;
                break;
            }
            char buffer[INET_ADDRSTRLEN] = {0};
            const auto* in = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
            if (inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer))) {
                pinned_ip = buffer;
                found = true;
            }
        } else if (it->ai_family == AF_INET6 && pinned_ip.empty()) {
            if (!is_public_ipv6(reinterpret_cast<const sockaddr_in6*>(it->ai_addr))) {
                found = false;
                break;
            }
            char buffer[INET6_ADDRSTRLEN] = {0};
            const auto* in6 = reinterpret_cast<const sockaddr_in6*>(it->ai_addr);
            if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer))) {
                pinned_ip = buffer;
                found = true;
            }
        }
    }
    freeaddrinfo(resolved);
    if (!found || pinned_ip.empty()) {
        error = "URL host does not resolve to a public address";
        return false;
    }
    path = path_start == std::string::npos ? "/" : rest.substr(path_start);
    const size_t fragment = path.find('#');
    if (fragment != std::string::npos) path = path.substr(0, fragment);
    if (path.empty()) path = "/";
    return true;
}

struct FetchedPage {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

FetchedPage fetch_public_page(const std::string& url, const Site& site) {
    FetchedPage page;
    std::string host;
    std::string path;
    std::string pinned_ip;
    if (!safe_seo_target(url, site, host, path, pinned_ip, page.error)) return page;
    httplib::Client client("https://" + host);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(10, 0);
    client.set_write_timeout(10, 0);
    client.set_follow_location(false);
    client.enable_server_certificate_verification(true);
    client.set_hostname_addr_map({{host, pinned_ip}});
    auto result = client.Get(path.c_str());
    if (!result) {
        page.error = "The page could not be fetched";
        return page;
    }
    page.status = result->status;
    page.body = result->body.substr(0, 2 * 1024 * 1024);
    page.ok = result->status >= 200 && result->status < 300;
    return page;
}

std::string regex_capture(const std::string& text, const std::string& pattern) {
    try {
        std::regex re(pattern, std::regex::icase);
        std::smatch match;
        if (std::regex_search(text, match, re) && match.size() > 1) {
            return utils::trim_copy(match[1].str());
        }
    } catch (...) {}
    return "";
}

std::string meta_content(const std::string& html, const std::string& name) {
    try {
        std::regex re("<meta\\b[^>]*(?:name|property)=[\"']" + name +
                      "[\"'][^>]*content=[\"']([^\"']*)[\"'][^>]*>", std::regex::icase);
        std::smatch match;
        if (std::regex_search(html, match, re) && match.size() > 1) return match[1].str();
        std::regex reversed("<meta\\b[^>]*content=[\"']([^\"']*)[\"'][^>]*(?:name|property)=[\"']" +
                            name + "[\"'][^>]*>", std::regex::icase);
        if (std::regex_search(html, match, reversed) && match.size() > 1) return match[1].str();
    } catch (...) {}
    return "";
}

int count_pattern(const std::string& text, const std::string& pattern) {
    try {
        std::regex re(pattern, std::regex::icase);
        return static_cast<int>(std::distance(
            std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
    } catch (...) {
        return 0;
    }
}

std::string strip_html(const std::string& html) {
    std::string text = std::regex_replace(html, std::regex("<script[\\s\\S]*?</script>", std::regex::icase), " ");
    text = std::regex_replace(text, std::regex("<style[\\s\\S]*?</style>", std::regex::icase), " ");
    text = std::regex_replace(text, std::regex("<[^>]+>"), " ");
    text = std::regex_replace(text, std::regex("\\s+"), " ");
    return utils::trim_copy(text);
}

int word_count(const std::string& text) {
    std::istringstream stream(text);
    std::string word;
    int count = 0;
    while (stream >> word) ++count;
    return count;
}

void add_issue(SeoAudit& audit, const std::string& severity, const std::string& category,
               const std::string& code, const std::string& message,
               const std::string& recommendation, const std::string& page_url,
               int penalty) {
    SeoAuditIssue issue;
    issue.audit_id = audit.id;
    issue.severity = severity;
    issue.category = category;
    issue.code = code;
    issue.message = message;
    issue.recommendation = recommendation;
    issue.page_url = page_url;
    audit.issues.push_back(issue);
    audit.score = std::max(0, audit.score - penalty);
}

nlohmann::json analyze_html(const std::string& html, const std::string& url,
                            int http_status, bool robots_ok, bool sitemap_ok,
                            SeoAudit& audit) {
    const std::string title = regex_capture(html, "<title\\b[^>]*>([\\s\\S]*?)</title>");
    const std::string description = meta_content(html, "description");
    const std::string canonical = regex_capture(
        html, "<link\\b[^>]*rel=[\"']canonical[\"'][^>]*href=[\"']([^\"']*)[\"'][^>]*>");
    const int h1_count = count_pattern(html, "<h1\\b[^>]*>");
    const int h2_count = count_pattern(html, "<h2\\b[^>]*>");
    const int img_count = count_pattern(html, "<img\\b[^>]*>");
    int missing_alt = 0;
    try {
        std::regex img_re("<img\\b[^>]*>", std::regex::icase);
        for (std::sregex_iterator it(html.begin(), html.end(), img_re), end; it != end; ++it) {
            if (!std::regex_search(it->str(), std::regex("\\balt\\s*=", std::regex::icase))) ++missing_alt;
        }
    } catch (...) {}
    const bool has_json_ld = html.find("application/ld+json") != std::string::npos;
    const bool has_og = html.find("property=\"og:") != std::string::npos ||
                        html.find("property='og:") != std::string::npos;
    const bool has_viewport = !meta_content(html, "viewport").empty();
    const bool has_lang = count_pattern(html, "<html\\b[^>]*\\blang\\s*=") > 0;
    const std::string robots_meta = utils::to_lower_copy(meta_content(html, "robots"));
    const bool noindex = robots_meta.find("noindex") != std::string::npos;
    const std::string text = strip_html(html);
    const int words = word_count(text);
    int internal_links = 0;
    int external_links = 0;
    try {
        std::regex link_re("<a\\b[^>]*href=[\"']([^\"']*)[\"']", std::regex::icase);
        for (std::sregex_iterator it(html.begin(), html.end(), link_re), end; it != end; ++it) {
            const std::string href = it->str(1);
            if (href.rfind("#", 0) == 0 || href.rfind("mailto:", 0) == 0 ||
                href.rfind("tel:", 0) == 0 || href.rfind("javascript:", 0) == 0) continue;
            if (href.find("://") == std::string::npos || href.rfind("/", 0) == 0) {
                ++internal_links;
            } else if (utils::to_lower_copy(href).find(utils::normalize_host(url)) != std::string::npos) {
                ++internal_links;
            } else {
                ++external_links;
            }
        }
    } catch (...) {}

    audit.score = 100;
    if (http_status < 200 || http_status >= 300) {
        add_issue(audit, "critical", "availability", "http_status",
                  "The page did not return a successful HTTP status.",
                  "Restore a 2xx response for the audited URL.", url, 40);
    }
    if (title.empty()) {
        add_issue(audit, "high", "content", "missing_title",
                  "The page has no title tag.", "Add a unique, descriptive title of 10–65 characters.", url, 12);
    } else if (title.size() < 10 || title.size() > 65) {
        add_issue(audit, "medium", "content", "title_length",
                  "The title length is outside the recommended 10–65 character range.",
                  "Rewrite the title so it is concise and descriptive.", url, 4);
    }
    if (description.empty()) {
        add_issue(audit, "medium", "content", "missing_meta_description",
                  "The page has no meta description.", "Add a 50–160 character meta description.", url, 8);
    } else if (description.size() < 50 || description.size() > 160) {
        add_issue(audit, "low", "content", "meta_description_length",
                  "The meta description length is outside the recommended range.",
                  "Keep the description between 50 and 160 characters.", url, 3);
    }
    if (canonical.empty()) {
        add_issue(audit, "low", "technical", "missing_canonical",
                  "No canonical URL was found.", "Add a rel=canonical link for indexable pages.", url, 4);
    }
    if (h1_count != 1) {
        add_issue(audit, "high", "content", "heading_structure",
                  "The page should contain exactly one H1.",
                  "Use one descriptive H1 and structure lower headings hierarchically.", url, 8);
    }
    if (!has_viewport) {
        add_issue(audit, "high", "mobile", "missing_viewport",
                  "No viewport meta tag was found.", "Add a responsive viewport meta tag.", url, 7);
    }
    if (!has_lang) {
        add_issue(audit, "low", "technical", "missing_lang",
                  "The html element has no lang attribute.", "Declare the page language.", url, 2);
    }
    if (noindex) {
        add_issue(audit, "critical", "indexing", "noindex",
                  "The page is explicitly marked noindex.",
                  "Remove noindex if this page should appear in search.", url, 20);
    }
    if (missing_alt > 0) {
        add_issue(audit, "medium", "accessibility", "missing_alt",
                  std::to_string(missing_alt) + " image(s) have no alt attribute.",
                  "Add meaningful alt text to informative images and empty alt to decorative images.", url, 5);
    }
    if (!has_json_ld) {
        add_issue(audit, "info", "structured_data", "missing_json_ld",
                  "No JSON-LD structured data was detected.",
                  "Add relevant Schema.org markup where it describes the page accurately.", url, 2);
    }
    if (!has_og) {
        add_issue(audit, "low", "social", "missing_open_graph",
                  "No Open Graph metadata was detected.", "Add og:title, og:description, and og:image.", url, 2);
    }
    if (words < 300) {
        add_issue(audit, "medium", "content", "thin_content",
                  "The page has fewer than 300 words of visible text.",
                  "Add substantive, useful content that satisfies search intent.", url, 6);
    }
    if (!robots_ok) {
        add_issue(audit, "medium", "technical", "robots_missing",
                  "robots.txt could not be fetched successfully.",
                  "Serve a valid robots.txt at the site root.", url, 3);
    }
    if (!sitemap_ok) {
        add_issue(audit, "medium", "technical", "sitemap_missing",
                  "sitemap.xml could not be fetched successfully.",
                  "Publish a valid XML sitemap at /sitemap.xml.", url, 4);
    }

    int critical = 0, high = 0, medium = 0, low = 0, info = 0;
    for (const auto& issue : audit.issues) {
        if (issue.severity == "critical") ++critical;
        else if (issue.severity == "high") ++high;
        else if (issue.severity == "medium") ++medium;
        else if (issue.severity == "low") ++low;
        else ++info;
    }
    return {
        {"http_status", http_status},
        {"title", truncate_utf8(title, 200)},
        {"title_length", title.size()},
        {"meta_description", truncate_utf8(description, 300)},
        {"meta_description_length", description.size()},
        {"canonical", truncate_utf8(canonical, 500)},
        {"h1_count", h1_count},
        {"h2_count", h2_count},
        {"word_count", words},
        {"image_count", img_count},
        {"images_missing_alt", missing_alt},
        {"internal_links", internal_links},
        {"external_links", external_links},
        {"has_json_ld", has_json_ld},
        {"has_open_graph", has_og},
        {"has_viewport", has_viewport},
        {"has_lang", has_lang},
        {"noindex", noindex},
        {"robots_ok", robots_ok},
        {"sitemap_ok", sitemap_ok},
        {"issue_counts", {
            {"critical", critical}, {"high", high}, {"medium", medium},
            {"low", low}, {"info", info}
        }}
    };
}

std::string canonical_origin(const SeoProject& project) {
    return "https://" + project.domain + "/";
}

std::string guess_intent(const std::string& keyword) {
    const std::string lower = utils::to_lower_copy(keyword);
    if (lower.find("buy") != std::string::npos || lower.find("price") != std::string::npos ||
        lower.find("cost") != std::string::npos) return "commercial";
    if (lower.find("best") != std::string::npos || lower.find("vs") != std::string::npos ||
        lower.find("review") != std::string::npos) return "commercial";
    if (lower.find("near me") != std::string::npos || lower.find("login") != std::string::npos) return "transactional";
    return "informational";
}

std::optional<SeoProject> seo_project_for_user(Database& db, const User& user,
                                                const std::string& project_id,
                                                httplib::Response& res) {
    auto project = db.get_seo_project(project_id);
    if (!project || !can_access_seo_project(user, *project)) {
        json_error(res, 404, "SEO project not found");
        return std::nullopt;
    }
    return project;
}

bool valid_seo_intent(const std::string& intent) {
    return intent == "informational" || intent == "commercial" ||
           intent == "transactional" || intent == "navigational";
}

bool valid_seo_device(const std::string& device) {
    return device == "desktop" || device == "mobile" || device == "all";
}

bool seo_url_in_site(const std::string& url, const Site& site, std::string& error) {
    if (url.empty()) return true;
    if (url.size() > 4096 || !utils::is_safe_url(url)) {
        error = "URL must be a valid http(s) URL";
        return false;
    }
    std::vector<std::string> domains = site.allowed_domains.empty()
        ? std::vector<std::string>{site.primary_domain} : site.allowed_domains;
    if (!utils::is_domain_allowed(url, domains)) {
        error = "URL host is outside this site's allowlist";
        return false;
    }
    return true;
}

} // namespace

void ApiServer::register_feature_routes(httplib::Server& svr, const std::string& sdk_path) {
    // Public conversion SDK. It is intentionally separate from the ad delivery
    // SDK so an advertiser landing page can track conversions without loading
    // or rendering any ad placements.
    svr.Get("/sdk/v1/conversion.js", [sdk_path](const httplib::Request&, httplib::Response& res) {
        std::filesystem::path path = sdk_path.empty()
            ? std::filesystem::path("adcontrol-conversion.js")
            : std::filesystem::path(sdk_path).parent_path() / "adcontrol-conversion.js";
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            json_error(res, 404, "Conversion SDK asset not found");
            return;
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Cache-Control", "public, max-age=300");
        res.set_content(content, "application/javascript; charset=utf-8");
    });

    // -----------------------------------------------------------------------
    // Public conversion ingestion (pixel, browser SDK, signed webhook)
    // -----------------------------------------------------------------------
    svr.Post("/conversion/v1/events", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_header("Vary", "Origin");
        set_no_store(res);

        const std::string ip = client_ip(req);
        if (!allow_request("conversion:" + ip, 240, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        std::string source_key = safe_string(body, "source_key", 120);
        if (source_key.empty()) source_key = safe_string(body, "key", 120);
        if (source_key.empty()) {
            json_error(res, 400, "source_key is required");
            return;
        }

        auto source = db_->get_conversion_source_by_public_key(source_key);
        if (!source || source->status != "active") {
            json_error(res, 404, "Conversion source not found or disabled");
            return;
        }
        ConversionEvent event;
        std::string error;
        if (!build_conversion(*db_, *source, body, "sdk", "", false, event, error)) {
            json_error(res, 400, error);
            return;
        }
        bool inserted = false;
        if (!db_->record_conversion(event, inserted, error)) {
            json_error(res, 500, "Could not record conversion");
            return;
        }
        if (inserted) {
            std::string automation_error;
            nlohmann::json payload = {
                {"conversion_id", event.id},
                {"event_name", event.event_name},
                {"campaign_id", event.campaign_id},
                {"offer_id", event.offer_id},
                {"value_cents", event.value_cents},
                {"commission_cents", event.commission_cents},
                {"currency", event.currency},
                {"utm_source", event.utm_source},
                {"utm_campaign", event.utm_campaign},
                {"site_id", event.site_id}
            };
            db_->enqueue_automation_event("conversion", event.publisher_id, event.site_id,
                                          payload.dump(), "conversion:" + event.id, automation_error);
        }
        res.status = 202;
        res.set_content(nlohmann::json({
            {"status", inserted ? "recorded" : "duplicate"},
            {"conversion_id", event.id}
        }).dump(), "application/json");
    });

    svr.Get("/conversion/v1/pixel.gif", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        res.set_header("Content-Type", "image/gif");
        const std::string ip = client_ip(req);
        if (!allow_request("conversion-pixel:" + ip, 600, 60)) {
            res.set_content(CONVERSION_GIF, "image/gif");
            return;
        }
        nlohmann::json body = nlohmann::json::object();
        for (const char* key : {"source_key", "event_name", "external_id", "order_id",
                                "currency", "campaign_id", "offer_id", "visitor_id",
                                "page_url", "referrer", "utm_source", "utm_medium",
                                "utm_campaign", "utm_term", "utm_content", "occurred_at"}) {
            if (req.has_param(key)) body[key] = req.get_param_value(key);
        }
        for (const char* key : {"value_cents", "commission_cents"}) {
            if (req.has_param(key)) {
                int parsed = 0;
                if (parse_int(req.get_param_value(key), parsed)) body[key] = parsed;
            }
        }
        auto source = db_->get_conversion_source_by_public_key(safe_string(body, "source_key", 120));
        if (source && source->status == "active") {
            ConversionEvent event;
            std::string error;
            if (build_conversion(*db_, *source, body, "pixel", "", false, event, error)) {
                bool inserted = false;
                if (db_->record_conversion(event, inserted, error) && inserted) {
                    std::string automation_error;
                    nlohmann::json payload = {
                        {"conversion_id", event.id},
                        {"event_name", event.event_name},
                        {"campaign_id", event.campaign_id},
                        {"offer_id", event.offer_id},
                        {"value_cents", event.value_cents},
                        {"commission_cents", event.commission_cents},
                        {"currency", event.currency},
                        {"site_id", event.site_id}
                    };
                    db_->enqueue_automation_event("conversion", event.publisher_id, event.site_id,
                                                  payload.dump(), "conversion:" + event.id, automation_error);
                }
            }
        }
        res.set_content(CONVERSION_GIF, "image/gif");
    });

    svr.Post(R"(/conversion/v1/webhooks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        const std::string ip = client_ip(req);
        if (!allow_request("conversion-webhook:" + ip, 600, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto source = db_->get_conversion_source_by_public_key(req.matches[1]);
        if (!source || source->status != "active") {
            json_error(res, 401, "Invalid conversion source");
            return;
        }
        auto secret = utils::decrypt_secret(source->encrypted_secret, secret_key_);
        std::string signature;
        if (req.has_header("X-AdControl-Signature")) {
            signature = req.get_header_value("X-AdControl-Signature");
        } else if (req.has_header("AdControl-Signature")) {
            signature = req.get_header_value("AdControl-Signature");
        }
        if (!secret || !signature_matches(*secret, req.body, signature)) {
            json_error(res, 401, "Invalid webhook signature");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        ConversionEvent event;
        std::string error;
        if (!build_conversion(*db_, *source, body, "webhook", "", true, event, error)) {
            json_error(res, 400, error);
            return;
        }
        bool inserted = false;
        if (!db_->record_conversion(event, inserted, error)) {
            json_error(res, 500, "Could not record conversion");
            return;
        }
        if (inserted) {
            std::string automation_error;
            nlohmann::json payload = {
                {"conversion_id", event.id},
                {"event_name", event.event_name},
                {"campaign_id", event.campaign_id},
                {"offer_id", event.offer_id},
                {"value_cents", event.value_cents},
                {"commission_cents", event.commission_cents},
                {"currency", event.currency},
                {"site_id", event.site_id}
            };
            db_->enqueue_automation_event("conversion", event.publisher_id, event.site_id,
                                          payload.dump(), "conversion:" + event.id, automation_error);
        }
        res.status = inserted ? 202 : 200;
        res.set_content(nlohmann::json({
            {"status", inserted ? "recorded" : "duplicate"},
            {"conversion_id", event.id}
        }).dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Conversion source management
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/conversion/sources", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        if (req.has_param("site_id")) {
            site_id = req.get_param_value("site_id");
            auto site = db_->get_site(*site_id);
            if (!site || !can_access_site(*user, *site)) {
                json_error(res, 404, "Site not found");
                return;
            }
        }
        std::optional<std::string> status;
        if (req.has_param("status")) status = lower_trim(req.get_param_value("status"));
        auto sources = db_->list_conversion_sources(publisher_id, site_id, status);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& source : sources) rows.push_back(source.to_json());
        res.set_content(nlohmann::json({{"sources", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v1/conversion/sources", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;

        std::string site_id = safe_string(body, "site_id", 80);
        std::string name = safe_string(body, "name", 120);
        if (site_id.empty() || name.empty()) {
            json_error(res, 400, "site_id and name are required");
            return;
        }
        auto site = db_->get_site(site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }

        ConversionSource source;
        source.site_id = site->id;
        source.publisher_id = site->publisher_id;
        source.name = name;
        source.default_campaign_id = safe_string(body, "default_campaign_id", 80);
        source.default_offer_id = safe_string(body, "default_offer_id", 80);
        if (!source.default_campaign_id.empty()) {
            auto campaign = db_->get_campaign(source.default_campaign_id);
            if (!campaign || campaign->site_id != site->id) {
                json_error(res, 400, "default_campaign_id does not belong to this site");
                return;
            }
        }
        if (!source.default_offer_id.empty()) {
            auto offer = db_->get_affiliate_offer(source.default_offer_id);
            if (!offer || offer->publisher_id != site->publisher_id) {
                json_error(res, 400, "default_offer_id does not belong to this site's publisher");
                return;
            }
        }
        const std::string signing_secret = utils::random_hex(32);
        source.encrypted_secret = utils::encrypt_secret(signing_secret, secret_key_);
        if (source.encrypted_secret.empty()) {
            json_error(res, 500, "Could not secure the webhook signing key");
            return;
        }
        if (!db_->create_conversion_source(source)) {
            json_error(res, 500, "Could not create conversion source");
            return;
        }
        audit(*db_, user->email, "CREATE_CONVERSION_SOURCE", "conversion_source", source.id,
              {{"site_id", source.site_id}, {"name", source.name}});

        nlohmann::json response = source.to_json();
        response["signing_secret"] = signing_secret;
        response["webhook_url"] = "/conversion/v1/webhooks/" + source.public_key;
        response["pixel_url"] = "/conversion/v1/pixel.gif?source_key=" + source.public_key;
        res.status = 201;
        res.set_content(nlohmann::json({
            {"source", response},
            {"notice", "Store the signing secret now. It is not returned again."}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/conversion/sources/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto source = db_->get_conversion_source(req.matches[1]);
        if (!source) {
            json_error(res, 404, "Conversion source not found");
            return;
        }
        if (!is_admin(*user) && (user->publisher_id.empty() ||
                                 source->publisher_id != user->publisher_id)) {
            json_error(res, 404, "Conversion source not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;
        if (body.contains("name")) source->name = safe_string(body, "name", 120);
        if (body.contains("status")) source->status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("default_campaign_id")) {
            source->default_campaign_id = safe_string(body, "default_campaign_id", 80);
        }
        if (body.contains("default_offer_id")) {
            source->default_offer_id = safe_string(body, "default_offer_id", 80);
        }
        if (source->name.empty() || !valid_source_status(source->status)) {
            json_error(res, 400, "name must not be empty and status must be active or disabled");
            return;
        }
        if (!source->default_campaign_id.empty()) {
            auto campaign = db_->get_campaign(source->default_campaign_id);
            if (!campaign || campaign->site_id != source->site_id) {
                json_error(res, 400, "default_campaign_id does not belong to this site");
                return;
            }
        }
        if (!source->default_offer_id.empty()) {
            auto offer = db_->get_affiliate_offer(source->default_offer_id);
            if (!offer || offer->publisher_id != source->publisher_id) {
                json_error(res, 400, "default_offer_id does not belong to this publisher");
                return;
            }
        }
        if (!db_->update_conversion_source(*source)) {
            json_error(res, 500, "Could not update conversion source");
            return;
        }
        audit(*db_, user->email, "UPDATE_CONVERSION_SOURCE", "conversion_source", source->id,
              {{"status", source->status}});
        res.set_content(nlohmann::json({{"source", source->to_json()}}).dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Conversion reporting and reconciliation
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/conversions/summary", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        std::optional<std::string> campaign_id;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
        if (req.has_param("campaign_id")) campaign_id = req.get_param_value("campaign_id");
        std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        auto summary = db_->get_conversion_summary(publisher_id, site_id, campaign_id,
                                                  start_date, end_date);
        res.set_content(summary.dump(), "application/json");
    });

    svr.Get("/api/v1/conversions/reconciliation", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
        std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        auto report = db_->get_conversion_reconciliation(publisher_id, site_id, start_date, end_date);
        res.set_content(report.dump(), "application/json");
    });

    svr.Get("/api/v1/conversions", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        std::optional<std::string> campaign_id;
        std::optional<std::string> status;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
        if (req.has_param("campaign_id")) campaign_id = req.get_param_value("campaign_id");
        if (req.has_param("status")) status = lower_trim(req.get_param_value("status"));
        if (status && !valid_conversion_status(*status)) {
            json_error(res, 400, "status must be pending, approved, rejected, or reconciled");
            return;
        }
        int limit = 100;
        int offset = 0;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        if (req.has_param("offset") && !parse_int(req.get_param_value("offset"), offset)) {
            json_error(res, 400, "offset must be an integer");
            return;
        }
        limit = std::max(1, std::min(limit, 500));
        offset = std::max(0, offset);
        std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        auto events = db_->list_conversion_events(publisher_id, site_id, campaign_id, status,
                                                  start_date, end_date, limit, offset);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& event : events) rows.push_back(event.to_json());
        res.set_content(nlohmann::json({{"conversions", rows}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/conversions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto event = db_->get_conversion_event(req.matches[1]);
        if (!event) {
            json_error(res, 404, "Conversion not found");
            return;
        }
        if (!is_admin(*user) && (user->publisher_id.empty() ||
                                 event->publisher_id != user->publisher_id)) {
            json_error(res, 404, "Conversion not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;
        std::string status = lower_trim(safe_string(body, "status", 20));
        std::string notes = safe_string(body, "notes", 1000);
        if (!valid_conversion_status(status)) {
            json_error(res, 400, "status must be pending, approved, rejected, or reconciled");
            return;
        }
        if (!db_->update_conversion_reconciliation(event->id, status, notes)) {
            json_error(res, 500, "Could not update conversion");
            return;
        }
        audit(*db_, user->email, "UPDATE_CONVERSION_STATUS", "conversion", event->id,
              {{"from", event->status}, {"to", status}});
        auto updated = db_->get_conversion_event(event->id);
        res.set_content(nlohmann::json({{"conversion", updated ? updated->to_json() : event->to_json()}}).dump(),
                        "application/json");
    });

    // -----------------------------------------------------------------------
    // Marketing automation
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/automation/workflows", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
        std::optional<std::string> status;
        if (req.has_param("status")) status = lower_trim(req.get_param_value("status"));
        auto workflows = db_->list_automation_workflows(publisher_id, site_id, status);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& workflow : workflows) rows.push_back(workflow.to_json());
        res.set_content(nlohmann::json({{"workflows", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v1/automation/workflows", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 64 * 1024)) return;
        AutomationWorkflow workflow;
        workflow.created_by = user->email;
        std::string error;
        if (!parse_automation_workflow(*db_, *user, body, true, workflow, error)) {
            json_error(res, 400, error);
            return;
        }
        if (!db_->create_automation_workflow(workflow, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_automation(*db_, *user, "CREATE_AUTOMATION_WORKFLOW", workflow.id,
                         {{"name", workflow.name}, {"trigger_type", workflow.trigger_type}});
        res.status = 201;
        res.set_content(nlohmann::json({{"workflow", workflow.to_json()}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/automation/workflows/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto workflow = db_->get_automation_workflow(req.matches[1]);
        if (!workflow || !can_manage_automation_workflow(*user, *workflow)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        res.set_content(nlohmann::json({{"workflow", workflow->to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/automation/workflows/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto existing = db_->get_automation_workflow(req.matches[1]);
        if (!existing || !can_manage_automation_workflow(*user, *existing)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 64 * 1024)) return;
        AutomationWorkflow workflow = *existing;
        std::string error;
        if (!parse_automation_workflow(*db_, *user, body, false, workflow, error)) {
            json_error(res, 400, error);
            return;
        }
        if (!db_->update_automation_workflow(workflow, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_automation(*db_, *user, "UPDATE_AUTOMATION_WORKFLOW", workflow.id,
                         {{"status", workflow.status}});
        auto updated = db_->get_automation_workflow(workflow.id);
        res.set_content(nlohmann::json({{"workflow", updated ? updated->to_json() : workflow.to_json()}}).dump(),
                        "application/json");
    });

    svr.Post(R"(/api/v1/automation/workflows/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto workflow = db_->get_automation_workflow(req.matches[1]);
        if (!workflow || !can_manage_automation_workflow(*user, *workflow)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 4 * 1024)) return;
        const std::string status = lower_trim(safe_string(body, "status", 20));
        if (!is_valid_automation_status(status)) {
            json_error(res, 400, "status must be draft, active, or paused");
            return;
        }
        if (status == "active" && workflow->steps.empty()) {
            json_error(res, 400, "An active workflow requires at least one step");
            return;
        }
        if (!db_->set_automation_workflow_status(workflow->id, status)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        audit_automation(*db_, *user, "AUTOMATION_WORKFLOW_STATUS", workflow->id,
                         {{"from", workflow->status}, {"to", status}});
        auto updated = db_->get_automation_workflow(workflow->id);
        res.set_content(nlohmann::json({{"workflow", updated ? updated->to_json() : workflow->to_json()}}).dump(),
                        "application/json");
    });

    svr.Delete(R"(/api/v1/automation/workflows/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto workflow = db_->get_automation_workflow(req.matches[1]);
        if (!workflow || !can_manage_automation_workflow(*user, *workflow)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        if (!db_->delete_automation_workflow(workflow->id)) {
            json_error(res, 500, "Could not delete workflow");
            return;
        }
        audit_automation(*db_, *user, "DELETE_AUTOMATION_WORKFLOW", workflow->id,
                         {{"name", workflow->name}});
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/automation/workflows/([^/]+)/run)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto workflow = db_->get_automation_workflow(req.matches[1]);
        if (!workflow || !can_manage_automation_workflow(*user, *workflow)) {
            json_error(res, 404, "Workflow not found");
            return;
        }
        nlohmann::json body = nlohmann::json::object();
        if (!req.body.empty() && !parse_json_body(req, res, body, 16 * 1024)) return;
        const std::string trigger_key = "manual:" + utils::generate_uuid();
        std::string run_id;
        std::string error;
        if (!db_->queue_manual_automation_run(workflow->id, body.dump(), trigger_key, run_id, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_automation(*db_, *user, "RUN_AUTOMATION_WORKFLOW", workflow->id,
                         {{"run_id", run_id}});
        res.status = 202;
        res.set_content(nlohmann::json({
            {"status", "queued"},
            {"run_id", run_id},
            {"message", "The automation worker will execute this run within 30 seconds."}
        }).dump(), "application/json");
    });

    svr.Get("/api/v1/automation/runs", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> workflow_id;
        if (req.has_param("workflow_id")) {
            workflow_id = req.get_param_value("workflow_id");
            auto workflow = db_->get_automation_workflow(*workflow_id);
            if (!workflow || !can_manage_automation_workflow(*user, *workflow)) {
                json_error(res, 404, "Workflow not found");
                return;
            }
        }
        int limit = 100;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        auto runs = db_->list_automation_runs(workflow_id, publisher_id, limit);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& run : runs) rows.push_back(run.to_json());
        res.set_content(nlohmann::json({{"runs", rows}}).dump(), "application/json");
    });

    svr.Get("/api/v1/automation/metrics", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::string site_id = req.has_param("site_id") ? req.get_param_value("site_id") : "";
        std::string campaign_id = req.has_param("campaign_id") ? req.get_param_value("campaign_id") : "";
        int lookback = 24;
        if (req.has_param("lookback_hours") && !parse_int(req.get_param_value("lookback_hours"), lookback)) {
            json_error(res, 400, "lookback_hours must be an integer");
            return;
        }
        lookback = std::max(1, std::min(lookback, 720));
        if (!campaign_id.empty()) {
            auto campaign = db_->get_campaign(campaign_id);
            auto site = campaign ? db_->get_site(campaign->site_id) : std::nullopt;
            if (!campaign || !site || !can_access_site(*user, *site)) {
                json_error(res, 404, "Campaign not found");
                return;
            }
            site_id = campaign->site_id;
        } else if (!site_id.empty()) {
            auto site = db_->get_site(site_id);
            if (!site || !can_access_site(*user, *site)) {
                json_error(res, 404, "Site not found");
                return;
            }
        } else {
            json_error(res, 400, "site_id or campaign_id is required");
            return;
        }
        res.set_content(db_->get_automation_metrics(site_id, campaign_id, lookback).dump(),
                        "application/json");
    });

    svr.Get("/api/v1/automation/notifications", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        std::optional<std::string> user_id;
        if (is_admin(*user)) {
            if (req.has_param("publisher_id")) publisher_id = req.get_param_value("publisher_id");
        } else {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden: account is not linked to a publisher");
                return;
            }
            publisher_id = user->publisher_id;
            user_id = user->id;
        }
        auto notifications = db_->list_notifications(publisher_id, user_id, 100);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& notification : notifications) rows.push_back(notification.to_json());
        int unread = db_->count_unread_notifications(publisher_id, user_id);
        res.set_content(nlohmann::json({{"notifications", rows}, {"unread", unread}}).dump(),
                        "application/json");
    });

    svr.Post(R"(/api/v1/automation/notifications/([^/]+)/read)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user)) {
            json_error(res, 403, "Forbidden");
            return;
        }
        auto notification = db_->get_notification(req.matches[1]);
        if (!notification) {
            json_error(res, 404, "Notification not found");
            return;
        }
        if (!is_admin(*user) &&
            (user->publisher_id.empty() ||
             (!notification->publisher_id.empty() && notification->publisher_id != user->publisher_id))) {
            json_error(res, 404, "Notification not found");
            return;
        }
        db_->mark_notification_read(notification->id, user->id);
        res.set_content(nlohmann::json({{"status", "read"}}).dump(), "application/json");
    });

    svr.Post("/api/v1/automation/tick", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;
        int processed = run_automation_tick(*db_, 20);
        res.set_content(nlohmann::json({
            {"status", "ok"},
            {"processed_runs", processed},
            {"timestamp", utils::get_current_iso8601()}
        }).dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // CRM and lead management (administrator back office)
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/crm/leads", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        std::optional<std::string> stage;
        if (req.has_param("stage")) {
            stage = lower_trim(req.get_param_value("stage"));
            if (!is_valid_crm_stage(*stage)) {
                json_error(res, 400, "stage is not valid");
                return;
            }
        }
        const std::string query = req.has_param("q") ? utils::trim_copy(req.get_param_value("q")) : "";
        int limit = 200;
        int offset = 0;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        if (req.has_param("offset") && !parse_int(req.get_param_value("offset"), offset)) {
            json_error(res, 400, "offset must be an integer");
            return;
        }
        auto leads = db_->list_crm_leads(stage, query, limit, offset);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& lead : leads) rows.push_back(lead.to_json());
        res.set_content(nlohmann::json({
            {"leads", rows},
            {"pipeline", db_->get_crm_pipeline()},
            {"summary", db_->get_crm_summary()}
        }).dump(), "application/json");
    });

    svr.Post("/api/v1/crm/leads", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        CrmLead lead;
        lead.name = safe_string(body, "name", 120);
        lead.email = utils::to_lower_copy(safe_string(body, "email", 254));
        lead.company = safe_string(body, "company", 160);
        lead.website = safe_string(body, "website", 2048);
        lead.phone = safe_string(body, "phone", 40);
        lead.source = lower_trim(safe_string(body, "source", 80));
        if (lead.source.empty()) lead.source = "manual";
        lead.source_detail = safe_string(body, "source_detail", 160);
        lead.stage = lower_trim(safe_string(body, "stage", 20));
        if (lead.stage.empty()) lead.stage = "new";
        lead.notes = safe_string(body, "notes", 4000);
        lead.owner_user_id = safe_string(body, "owner_user_id", 80);
        lead.currency = utils::to_upper_copy(safe_string(body, "currency", 3));
        if (lead.currency.empty()) lead.currency = "USD";
        if (!is_valid_crm_stage(lead.stage)) {
            json_error(res, 400, "stage is not valid");
            return;
        }
        if (!smtp::is_valid_email(lead.email)) {
            json_error(res, 400, "A valid email address is required");
            return;
        }
        if (!lead.website.empty() && !utils::is_safe_url(lead.website)) {
            json_error(res, 400, "website must be a valid http(s) URL");
            return;
        }
        if (body.contains("value_cents")) {
            if (!body["value_cents"].is_number_integer()) {
                json_error(res, 400, "value_cents must be an integer");
                return;
            }
            lead.value_cents = std::max(0, body["value_cents"].get<int>());
        }
        if (body.contains("tags") && body["tags"].is_array()) {
            for (const auto& item : body["tags"]) {
                if (item.is_string() && lead.tags.size() < 20) {
                    lead.tags.push_back(item.get<std::string>().substr(0, 40));
                }
            }
        }
        if (!lead.owner_user_id.empty()) {
            auto owner = db_->get_user_by_id(lead.owner_user_id);
            if (!owner) {
                json_error(res, 400, "owner_user_id does not reference a user");
                return;
            }
        }
        std::string follow_up = safe_string(body, "next_follow_up_at", 40);
        if (!follow_up.empty()) {
            lead.next_follow_up_at = utils::normalize_iso8601_utc(follow_up);
            if (lead.next_follow_up_at.empty()) {
                json_error(res, 400, "next_follow_up_at must be an ISO-8601 timestamp");
                return;
            }
        }
        std::string error;
        if (!db_->create_crm_lead(lead, error)) {
            json_error(res, 400, error);
            return;
        }
        CrmActivity activity;
        activity.lead_id = lead.id;
        activity.type = "system";
        activity.subject = "Lead created";
        activity.body = "Lead added manually from the AdControl CRM.";
        activity.created_by = admin->email;
        db_->create_crm_activity(activity, error);
        audit_crm(*db_, *admin, "CREATE_CRM_LEAD", lead.id,
                  {{"email", lead.email}, {"source", lead.source}});
        auto stored = db_->get_crm_lead(lead.id);
        res.status = 201;
        res.set_content(nlohmann::json({{"lead", stored ? stored->to_json() : lead.to_json()}}).dump(),
                        "application/json");
    });

    svr.Get(R"(/api/v1/crm/leads/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto lead = db_->get_crm_lead(req.matches[1]);
        if (!lead) {
            json_error(res, 404, "Lead not found");
            return;
        }
        auto activities = db_->list_crm_activities(lead->id, 200);
        auto tasks = db_->list_crm_tasks(lead->id, std::nullopt, 200);
        nlohmann::json activity_rows = nlohmann::json::array();
        for (const auto& activity : activities) activity_rows.push_back(activity.to_json());
        nlohmann::json task_rows = nlohmann::json::array();
        for (const auto& task : tasks) task_rows.push_back(task.to_json());
        res.set_content(nlohmann::json({
            {"lead", lead->to_json()},
            {"activities", activity_rows},
            {"tasks", task_rows}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/crm/leads/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto existing = db_->get_crm_lead(req.matches[1]);
        if (!existing) {
            json_error(res, 404, "Lead not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        CrmLead lead = *existing;
        const std::string previous_stage = lead.stage;
        if (body.contains("name")) lead.name = safe_string(body, "name", 120);
        if (body.contains("email")) lead.email = utils::to_lower_copy(safe_string(body, "email", 254));
        if (body.contains("company")) lead.company = safe_string(body, "company", 160);
        if (body.contains("website")) lead.website = safe_string(body, "website", 2048);
        if (body.contains("phone")) lead.phone = safe_string(body, "phone", 40);
        if (body.contains("source")) lead.source = lower_trim(safe_string(body, "source", 80));
        if (body.contains("source_detail")) lead.source_detail = safe_string(body, "source_detail", 160);
        if (body.contains("stage")) lead.stage = lower_trim(safe_string(body, "stage", 20));
        if (body.contains("notes")) lead.notes = safe_string(body, "notes", 4000);
        if (body.contains("owner_user_id")) lead.owner_user_id = safe_string(body, "owner_user_id", 80);
        if (body.contains("currency")) lead.currency = utils::to_upper_copy(safe_string(body, "currency", 3));
        if (body.contains("value_cents")) {
            if (!body["value_cents"].is_number_integer()) {
                json_error(res, 400, "value_cents must be an integer");
                return;
            }
            lead.value_cents = std::max(0, body["value_cents"].get<int>());
        }
        if (body.contains("tags")) {
            if (!body["tags"].is_array()) {
                json_error(res, 400, "tags must be an array");
                return;
            }
            lead.tags.clear();
            for (const auto& item : body["tags"]) {
                if (item.is_string() && lead.tags.size() < 20) {
                    lead.tags.push_back(item.get<std::string>().substr(0, 40));
                }
            }
        }
        if (body.contains("next_follow_up_at")) {
            const std::string raw = safe_string(body, "next_follow_up_at", 40);
            lead.next_follow_up_at = raw.empty() ? "" : utils::normalize_iso8601_utc(raw);
            if (!raw.empty() && lead.next_follow_up_at.empty()) {
                json_error(res, 400, "next_follow_up_at must be an ISO-8601 timestamp");
                return;
            }
        }
        if (lead.name.empty() || !smtp::is_valid_email(lead.email) ||
            !is_valid_crm_stage(lead.stage)) {
            json_error(res, 400, "name, a valid email, and stage are required");
            return;
        }
        if (!lead.website.empty() && !utils::is_safe_url(lead.website)) {
            json_error(res, 400, "website must be a valid http(s) URL");
            return;
        }
        if (!lead.owner_user_id.empty() && !db_->get_user_by_id(lead.owner_user_id)) {
            json_error(res, 400, "owner_user_id does not reference a user");
            return;
        }
        std::string error;
        if (!db_->update_crm_lead(lead, error)) {
            json_error(res, 400, error);
            return;
        }
        if (previous_stage != lead.stage) {
            CrmActivity activity;
            activity.lead_id = lead.id;
            activity.type = "stage_change";
            activity.subject = "Pipeline stage changed";
            activity.body = previous_stage + " → " + lead.stage;
            activity.created_by = admin->email;
            activity.metadata = nlohmann::json({{"from", previous_stage}, {"to", lead.stage}}).dump();
            db_->create_crm_activity(activity, error);
        }
        audit_crm(*db_, *admin, "UPDATE_CRM_LEAD", lead.id,
                  {{"stage", lead.stage}, {"previous_stage", previous_stage}});
        auto updated = db_->get_crm_lead(lead.id);
        res.set_content(nlohmann::json({{"lead", updated ? updated->to_json() : lead.to_json()}}).dump(),
                        "application/json");
    });

    svr.Delete(R"(/api/v1/crm/leads/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto lead = db_->get_crm_lead(req.matches[1]);
        if (!lead) {
            json_error(res, 404, "Lead not found");
            return;
        }
        if (!db_->delete_crm_lead(lead->id)) {
            json_error(res, 500, "Could not delete lead");
            return;
        }
        audit_crm(*db_, *admin, "DELETE_CRM_LEAD", lead->id, {{"email", lead->email}});
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/crm/leads/([^/]+)/activities)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto lead = db_->get_crm_lead(req.matches[1]);
        if (!lead) {
            json_error(res, 404, "Lead not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        CrmActivity activity;
        activity.lead_id = lead->id;
        activity.type = lower_trim(safe_string(body, "type", 20));
        if (activity.type != "note" && activity.type != "call" && activity.type != "meeting") {
            json_error(res, 400, "type must be note, call, or meeting");
            return;
        }
        activity.subject = safe_string(body, "subject", 200);
        activity.body = safe_string(body, "body", 8000);
        if (activity.subject.empty() && activity.body.empty()) {
            json_error(res, 400, "subject or body is required");
            return;
        }
        const std::string occurred = safe_string(body, "occurred_at", 40);
        if (!occurred.empty()) {
            activity.occurred_at = utils::normalize_iso8601_utc(occurred);
            if (activity.occurred_at.empty()) {
                json_error(res, 400, "occurred_at must be an ISO-8601 timestamp");
                return;
            }
        }
        activity.created_by = admin->email;
        std::string error;
        if (!db_->create_crm_activity(activity, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_crm(*db_, *admin, "CRM_ACTIVITY", lead->id, {{"type", activity.type}});
        res.status = 201;
        res.set_content(nlohmann::json({{"activity", activity.to_json()}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/crm/leads/([^/]+)/email)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto lead = db_->get_crm_lead(req.matches[1]);
        if (!lead) {
            json_error(res, 404, "Lead not found");
            return;
        }
        if (!allow_request("crm-email:" + admin->id, 30, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Too many emails. Wait a minute and try again.");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 64 * 1024)) return;
        const std::string subject = safe_string(body, "subject", 200);
        const std::string message = safe_string(body, "body", 20000);
        if (subject.empty() || message.empty()) {
            json_error(res, 400, "subject and body are required");
            return;
        }
        if (!smtp::is_valid_email(lead->email)) {
            json_error(res, 400, "Lead does not have a valid email address");
            return;
        }
        smtp::Settings transport;
        std::string error;
        if (!load_smtp_transport(*db_, secret_key_, transport, error)) {
            json_error(res, 400, error);
            return;
        }
        const std::string html =
            "<p>" + html_with_line_breaks(message) + "</p>"
            "<p style=\"color:#64748b;font-size:12px\">Sent from the AdControl CRM.</p>";
        auto sent = smtp::send(transport, lead->email, subject, html);
        const std::string recipient_domain = lead->email.substr(lead->email.rfind('@') + 1);
        AuditLog al;
        al.actor = admin->email;
        al.action = "CRM_SEND_EMAIL";
        al.entity_type = "crm_lead";
        al.entity_id = lead->id;
        al.details = nlohmann::json({{"ok", sent.ok}, {"recipient_domain", recipient_domain}}).dump();
        db_->record_audit_log(al);
        if (!sent.ok) {
            json_error(res, 502, "Email failed: " + sent.error);
            return;
        }
        CrmActivity activity;
        activity.lead_id = lead->id;
        activity.type = "email";
        activity.subject = subject;
        activity.body = message;
        activity.created_by = admin->email;
        activity.metadata = nlohmann::json({{"to", lead->email}, {"status", "sent"}}).dump();
        if (!db_->create_crm_activity(activity, error)) {
            json_error(res, 500, "Email was sent but the CRM activity could not be saved");
            return;
        }
        auto updated = db_->get_crm_lead(lead->id);
        res.set_content(nlohmann::json({
            {"status", "sent"},
            {"activity", activity.to_json()},
            {"lead", updated ? updated->to_json() : lead->to_json()}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/crm/leads/([^/]+)/tasks)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto lead = db_->get_crm_lead(req.matches[1]);
        if (!lead) {
            json_error(res, 404, "Lead not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        CrmTask task;
        task.lead_id = lead->id;
        task.title = safe_string(body, "title", 200);
        task.due_at = utils::normalize_iso8601_utc(safe_string(body, "due_at", 40));
        task.assigned_to = safe_string(body, "assigned_to", 254);
        if (task.assigned_to.empty()) task.assigned_to = admin->email;
        task.notes = safe_string(body, "notes", 2000);
        if (task.title.empty() || task.due_at.empty()) {
            json_error(res, 400, "title and a valid due_at are required");
            return;
        }
        std::string error;
        if (!db_->create_crm_task(task, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_crm(*db_, *admin, "CRM_TASK_CREATED", lead->id, {{"task_id", task.id}});
        auto updated = db_->get_crm_lead(lead->id);
        res.status = 201;
        res.set_content(nlohmann::json({
            {"task", task.to_json()},
            {"lead", updated ? updated->to_json() : lead->to_json()}
        }).dump(), "application/json");
    });

    svr.Get("/api/v1/crm/tasks", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        std::optional<std::string> lead_id;
        std::optional<std::string> status;
        if (req.has_param("lead_id")) lead_id = req.get_param_value("lead_id");
        if (req.has_param("status")) status = lower_trim(req.get_param_value("status"));
        if (status && !is_valid_crm_task_status(*status)) {
            json_error(res, 400, "status must be open, done, or cancelled");
            return;
        }
        int limit = 200;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        auto tasks = db_->list_crm_tasks(lead_id, status, limit);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& task : tasks) rows.push_back(task.to_json());
        res.set_content(nlohmann::json({{"tasks", rows}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/crm/tasks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto task = db_->get_crm_task(req.matches[1]);
        if (!task) {
            json_error(res, 404, "Task not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        if (body.contains("title")) task->title = safe_string(body, "title", 200);
        if (body.contains("due_at")) {
            task->due_at = utils::normalize_iso8601_utc(safe_string(body, "due_at", 40));
        }
        if (body.contains("status")) task->status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("assigned_to")) task->assigned_to = safe_string(body, "assigned_to", 254);
        if (body.contains("notes")) task->notes = safe_string(body, "notes", 2000);
        if (task->title.empty() || task->due_at.empty() || !is_valid_crm_task_status(task->status)) {
            json_error(res, 400, "title, a valid due_at, and status are required");
            return;
        }
        std::string error;
        if (!db_->update_crm_task(*task, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_crm(*db_, *admin, "CRM_TASK_UPDATED", task->lead_id,
                  {{"task_id", task->id}, {"status", task->status}});
        auto updated = db_->get_crm_task(task->id);
        res.set_content(nlohmann::json({{"task", updated ? updated->to_json() : task->to_json()}}).dump(),
                        "application/json");
    });

    svr.Get("/api/v1/crm/summary", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        res.set_content(nlohmann::json({
            {"pipeline", db_->get_crm_pipeline()},
            {"summary", db_->get_crm_summary()}
        }).dump(), "application/json");
    });

    svr.Get("/api/v1/crm/owners", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto users = db_->list_learning_users();
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& user : users) {
            if (user.role == "admin") rows.push_back(user.to_json());
        }
        res.set_content(nlohmann::json({{"owners", rows}}).dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // SEO projects, keyword tracking, and rank history
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/seo/projects", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> publisher_id;
        if (!scope_for_read(*user, req, publisher_id, res)) return;
        std::optional<std::string> site_id;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
        auto projects = db_->list_seo_projects(publisher_id, site_id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& project : projects) rows.push_back(project.to_json());
        res.set_content(nlohmann::json({{"projects", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v1/seo/projects", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        const std::string site_id = safe_string(body, "site_id", 80);
        auto site = db_->get_site(site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        SeoProject project;
        project.site_id = site->id;
        project.publisher_id = site->publisher_id;
        project.name = safe_string(body, "name", 120);
        project.domain = utils::normalize_host(safe_string(body, "domain", 253));
        if (project.domain.empty()) project.domain = utils::normalize_host(site->primary_domain);
        project.country = utils::to_upper_copy(safe_string(body, "country", 2));
        project.language = utils::to_lower_copy(safe_string(body, "language", 2));
        project.status = lower_trim(safe_string(body, "status", 20));
        if (project.country.empty()) project.country = "US";
        if (project.language.empty()) project.language = "en";
        if (project.status.empty()) project.status = "active";
        std::string error;
        if (project.name.size() < 2 || project.domain.empty() ||
            !utils::is_valid_domain_entry(project.domain) ||
            !seo_url_in_site("https://" + project.domain + "/", *site, error)) {
            json_error(res, 400, error.empty() ? "name and a valid site domain are required" : error);
            return;
        }
        if (project.country.size() != 2 || project.language.size() != 2 ||
            (project.status != "active" && project.status != "paused")) {
            json_error(res, 400, "country/language must be two-letter codes and status must be active or paused");
            return;
        }
        if (!db_->create_seo_project(project, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_seo(*db_, *user, "CREATE_SEO_PROJECT", project.id,
                  {{"site_id", project.site_id}, {"domain", project.domain}});
        res.status = 201;
        res.set_content(nlohmann::json({{"project", project.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/seo/projects/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoProject updated = *project;
        if (body.contains("name")) updated.name = safe_string(body, "name", 120);
        if (body.contains("domain")) updated.domain = utils::normalize_host(safe_string(body, "domain", 253));
        if (body.contains("country")) updated.country = utils::to_upper_copy(safe_string(body, "country", 2));
        if (body.contains("language")) updated.language = utils::to_lower_copy(safe_string(body, "language", 2));
        if (body.contains("status")) updated.status = lower_trim(safe_string(body, "status", 20));
        auto site = db_->get_site(updated.site_id);
        std::string error;
        if (updated.name.size() < 2 || !site || updated.domain.empty() ||
            !utils::is_valid_domain_entry(updated.domain) ||
            !seo_url_in_site("https://" + updated.domain + "/", *site, error) ||
            updated.country.size() != 2 || updated.language.size() != 2 ||
            (updated.status != "active" && updated.status != "paused")) {
            json_error(res, 400, error.empty() ? "name, domain, country, language, or status is invalid" : error);
            return;
        }
        if (!db_->update_seo_project(updated)) {
            json_error(res, 500, "Could not update SEO project");
            return;
        }
        audit_seo(*db_, *user, "UPDATE_SEO_PROJECT", updated.id, {{"status", updated.status}});
        res.set_content(nlohmann::json({{"project", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/projects/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        if (!db_->delete_seo_project(project->id)) {
            json_error(res, 500, "Could not delete SEO project");
            return;
        }
        audit_seo(*db_, *user, "DELETE_SEO_PROJECT", project->id, {{"domain", project->domain}});
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/seo/projects/([^/]+)/summary)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        res.set_content(nlohmann::json({
            {"project", project->to_json()},
            {"summary", db_->get_seo_summary(project->id)}
        }).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/seo/projects/([^/]+)/keywords)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto keywords = db_->list_seo_keywords(project->id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& keyword : keywords) rows.push_back(keyword.to_json());
        res.set_content(nlohmann::json({{"keywords", rows}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/keywords)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto site = db_->get_site(project->site_id);
        if (!site) {
            json_error(res, 404, "Site not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoKeyword keyword;
        keyword.project_id = project->id;
        keyword.keyword = utils::trim_copy(safe_string(body, "keyword", 160));
        keyword.target_url = safe_string(body, "target_url", 4096);
        keyword.country = utils::to_upper_copy(safe_string(body, "country", 2));
        keyword.device = lower_trim(safe_string(body, "device", 20));
        keyword.intent = lower_trim(safe_string(body, "intent", 20));
        keyword.source = lower_trim(safe_string(body, "source", 40));
        keyword.status = lower_trim(safe_string(body, "status", 20));
        std::string error;
        if (keyword.keyword.size() < 2 ||
            !seo_url_in_site(keyword.target_url, *site, error) ||
            !keyword.intent.empty() && !valid_seo_intent(keyword.intent) ||
            !keyword.device.empty() && !valid_seo_device(keyword.device)) {
            json_error(res, 400, error.empty() ? "keyword or target URL is invalid" : error);
            return;
        }
        if (keyword.country.empty()) keyword.country = project->country;
        if (keyword.device.empty()) keyword.device = "desktop";
        if (keyword.intent.empty()) keyword.intent = guess_intent(keyword.keyword);
        if (keyword.source.empty()) keyword.source = "manual";
        if (keyword.status.empty()) keyword.status = "active";
        if (body.contains("priority") && body["priority"].is_number_integer()) {
            keyword.priority = std::max(1, std::min(5, body["priority"].get<int>()));
        }
        if (body.contains("search_volume") && body["search_volume"].is_number_integer()) {
            keyword.search_volume = std::max(0, body["search_volume"].get<int>());
        }
        if (body.contains("difficulty") && body["difficulty"].is_number_integer()) {
            keyword.difficulty = std::max(0, std::min(100, body["difficulty"].get<int>()));
        }
        if (body.contains("tags") && body["tags"].is_array()) {
            for (const auto& item : body["tags"]) {
                if (item.is_string() && keyword.tags.size() < 20) {
                    keyword.tags.push_back(item.get<std::string>().substr(0, 40));
                }
            }
        }
        if (!db_->create_seo_keyword(keyword, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_seo(*db_, *user, "CREATE_SEO_KEYWORD", project->id, {{"keyword", keyword.keyword}});
        res.status = 201;
        res.set_content(nlohmann::json({{"keyword", keyword.to_json()}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/keywords/bulk)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto site = db_->get_site(project->site_id);
        if (!site) {
            json_error(res, 404, "Site not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 128 * 1024)) return;
        if (!body.contains("keywords") || !body["keywords"].is_array()) {
            json_error(res, 400, "keywords must be an array");
            return;
        }
        if (body["keywords"].size() > 100) {
            json_error(res, 400, "at most 100 keywords can be added at once");
            return;
        }
        std::vector<SeoKeyword> keywords;
        for (const auto& raw : body["keywords"]) {
            if (!raw.is_object()) continue;
            SeoKeyword keyword;
            keyword.project_id = project->id;
            keyword.keyword = utils::trim_copy(safe_string(raw, "keyword", 160));
            keyword.target_url = safe_string(raw, "target_url", 4096);
            keyword.country = project->country;
            keyword.device = "desktop";
            keyword.intent = lower_trim(safe_string(raw, "intent", 20));
            keyword.priority = 2;
            keyword.search_volume = 0;
            keyword.difficulty = 0;
            keyword.source = "research";
            keyword.status = "active";
            if (keyword.intent.empty()) keyword.intent = guess_intent(keyword.keyword);
            std::string error;
            if (keyword.keyword.size() < 2 || !seo_url_in_site(keyword.target_url, *site, error)) continue;
            keywords.push_back(keyword);
        }
        if (keywords.empty()) {
            json_error(res, 400, "no valid keywords were supplied");
            return;
        }
        std::string error;
        if (!db_->create_seo_keywords(keywords, error)) {
            json_error(res, 400, error);
            return;
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& keyword : keywords) rows.push_back(keyword.to_json());
        audit_seo(*db_, *user, "BULK_CREATE_SEO_KEYWORDS", project->id,
                  {{"count", static_cast<int>(keywords.size())}});
        res.status = 201;
        res.set_content(nlohmann::json({{"keywords", rows}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/research)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;
        const std::string seed = utils::to_lower_copy(utils::trim_copy(safe_string(body, "seed", 120)));
        if (seed.size() < 2) {
            json_error(res, 400, "seed must be at least 2 characters");
            return;
        }
        int count = 20;
        if (body.contains("count") && body["count"].is_number_integer()) {
            count = std::max(1, std::min(100, body["count"].get<int>()));
        }
        const std::vector<std::string> prefixes = {
            "", "how to ", "best ", "what is ", "why ", "cheap ", "free "
        };
        const std::vector<std::string> suffixes = {
            "", " for beginners", " 2026", " cost", " examples", " guide",
            " tools", " near me", " vs alternatives", " for small business",
            " checklist", " strategy", " mistakes"
        };
        std::set<std::string> seen;
        nlohmann::json suggestions = nlohmann::json::array();
        for (const auto& prefix : prefixes) {
            for (const auto& suffix : suffixes) {
                std::string phrase = utils::trim_copy(prefix + seed + suffix);
                if (phrase.empty() || seen.count(phrase)) continue;
                seen.insert(phrase);
                suggestions.push_back({
                    {"keyword", phrase},
                    {"intent", guess_intent(phrase)},
                    {"source", "generated"},
                    {"search_volume", 0},
                    {"difficulty", 0}
                });
                if (static_cast<int>(suggestions.size()) >= count) break;
            }
            if (static_cast<int>(suggestions.size()) >= count) break;
        }
        res.set_content(nlohmann::json({
            {"seed", seed},
            {"suggestions", suggestions},
            {"note", "Suggestions are generated locally. Import verified volume and difficulty data from your provider when available."}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/seo/keywords/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto keyword = db_->get_seo_keyword(req.matches[1]);
        if (!keyword) {
            json_error(res, 404, "Keyword not found");
            return;
        }
        auto project = seo_project_for_user(*db_, *user, keyword->project_id, res);
        if (!project) return;
        auto site = db_->get_site(project->site_id);
        if (!site) {
            json_error(res, 404, "Site not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoKeyword updated = *keyword;
        if (body.contains("keyword")) updated.keyword = utils::trim_copy(safe_string(body, "keyword", 160));
        if (body.contains("target_url")) updated.target_url = safe_string(body, "target_url", 4096);
        if (body.contains("country")) updated.country = utils::to_upper_copy(safe_string(body, "country", 2));
        if (body.contains("device")) updated.device = lower_trim(safe_string(body, "device", 20));
        if (body.contains("intent")) updated.intent = lower_trim(safe_string(body, "intent", 20));
        if (body.contains("priority") && body["priority"].is_number_integer()) {
            updated.priority = std::max(1, std::min(5, body["priority"].get<int>()));
        }
        if (body.contains("search_volume") && body["search_volume"].is_number_integer()) {
            updated.search_volume = std::max(0, body["search_volume"].get<int>());
        }
        if (body.contains("difficulty") && body["difficulty"].is_number_integer()) {
            updated.difficulty = std::max(0, std::min(100, body["difficulty"].get<int>()));
        }
        if (body.contains("status")) updated.status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("tags") && body["tags"].is_array()) {
            updated.tags.clear();
            for (const auto& item : body["tags"]) {
                if (item.is_string() && updated.tags.size() < 20) {
                    updated.tags.push_back(item.get<std::string>().substr(0, 40));
                }
            }
        }
        std::string error;
        if (updated.keyword.size() < 2 || !seo_url_in_site(updated.target_url, *site, error) ||
            !valid_seo_intent(updated.intent) || !valid_seo_device(updated.device)) {
            json_error(res, 400, error.empty() ? "invalid keyword, URL, intent, or device" : error);
            return;
        }
        if (!db_->update_seo_keyword(updated)) {
            json_error(res, 500, "Could not update keyword");
            return;
        }
        res.set_content(nlohmann::json({{"keyword", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/keywords/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto keyword = db_->get_seo_keyword(req.matches[1]);
        if (!keyword) {
            json_error(res, 404, "Keyword not found");
            return;
        }
        auto project = seo_project_for_user(*db_, *user, keyword->project_id, res);
        if (!project) return;
        if (!db_->delete_seo_keyword(keyword->id)) {
            json_error(res, 500, "Could not delete keyword");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/seo/keywords/([^/]+)/rankings)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto keyword = db_->get_seo_keyword(req.matches[1]);
        if (!keyword) {
            json_error(res, 404, "Keyword not found");
            return;
        }
        auto project = seo_project_for_user(*db_, *user, keyword->project_id, res);
        if (!project) return;
        auto rankings = db_->list_seo_rankings(keyword->id, 200);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& ranking : rankings) rows.push_back(ranking.to_json());
        res.set_content(nlohmann::json({
            {"keyword", keyword->to_json()},
            {"rankings", rows}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/keywords/([^/]+)/rankings)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto keyword = db_->get_seo_keyword(req.matches[1]);
        if (!keyword) {
            json_error(res, 404, "Keyword not found");
            return;
        }
        auto project = seo_project_for_user(*db_, *user, keyword->project_id, res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;
        SeoRanking ranking;
        ranking.keyword_id = keyword->id;
        ranking.position = 0;
        if (!body.contains("position") || !body["position"].is_number_integer() ||
            body["position"].get<int>() < 0 || body["position"].get<int>() > 1000) {
            json_error(res, 400, "position must be an integer between 0 and 1000 (0 means not ranking)");
            return;
        }
        ranking.position = body["position"].get<int>();
        ranking.url = safe_string(body, "url", 4096);
        ranking.note = safe_string(body, "note", 1000);
        ranking.source = lower_trim(safe_string(body, "source", 30));
        if (ranking.source.empty()) ranking.source = "manual";
        const std::string checked = safe_string(body, "checked_at", 40);
        if (!checked.empty()) {
            ranking.checked_at = utils::normalize_iso8601_utc(checked);
            if (ranking.checked_at.empty()) {
                json_error(res, 400, "checked_at must be an ISO-8601 timestamp");
                return;
            }
        }
        std::string error;
        SeoKeyword updated = *keyword;
        if (!db_->record_seo_ranking(updated, ranking, error)) {
            json_error(res, 400, error);
            return;
        }
        audit_seo(*db_, *user, "SEO_RANK_RECORDED", project->id,
                  {{"keyword", keyword->keyword}, {"position", ranking.position}});
        res.status = 201;
        res.set_content(nlohmann::json({
            {"keyword", updated.to_json()},
            {"ranking", ranking.to_json()}
        }).dump(), "application/json");
    });

    // Competitors and keyword gap data
    svr.Get(R"(/api/v1/seo/projects/([^/]+)/competitors)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto competitors = db_->list_seo_competitors(project->id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& competitor : competitors) {
            nlohmann::json row = competitor.to_json();
            nlohmann::json keyword_rows = nlohmann::json::array();
            for (const auto& keyword : db_->list_seo_competitor_keywords(competitor.id)) {
                keyword_rows.push_back(keyword.to_json());
            }
            row["keywords"] = keyword_rows;
            rows.push_back(row);
        }
        res.set_content(nlohmann::json({{"competitors", rows}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/competitors)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoCompetitor competitor;
        competitor.project_id = project->id;
        competitor.domain = utils::normalize_host(safe_string(body, "domain", 253));
        competitor.name = safe_string(body, "name", 120);
        competitor.notes = safe_string(body, "notes", 1000);
        if (!utils::is_valid_domain_entry(competitor.domain)) {
            json_error(res, 400, "A valid competitor domain is required");
            return;
        }
        std::string error;
        if (!db_->create_seo_competitor(competitor, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"competitor", competitor.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/seo/competitors/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto competitor = db_->get_seo_competitor(req.matches[1]);
        if (!competitor) {
            json_error(res, 404, "Competitor not found");
            return;
        }
        auto project = seo_project_for_user(*db_, *user, competitor->project_id, res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoCompetitor updated = *competitor;
        if (body.contains("domain")) updated.domain = utils::normalize_host(safe_string(body, "domain", 253));
        if (body.contains("name")) updated.name = safe_string(body, "name", 120);
        if (body.contains("notes")) updated.notes = safe_string(body, "notes", 1000);
        if (!utils::is_valid_domain_entry(updated.domain)) {
            json_error(res, 400, "A valid competitor domain is required");
            return;
        }
        if (!db_->update_seo_competitor(updated)) {
            json_error(res, 500, "Could not update competitor");
            return;
        }
        res.set_content(nlohmann::json({{"competitor", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/competitors/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto competitor = db_->get_seo_competitor(req.matches[1]);
        if (!competitor) {
            json_error(res, 404, "Competitor not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, competitor->project_id, res)) return;
        if (!db_->delete_seo_competitor(competitor->id)) {
            json_error(res, 500, "Could not delete competitor");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/competitors/([^/]+)/keywords)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto competitor = db_->get_seo_competitor(req.matches[1]);
        if (!competitor) {
            json_error(res, 404, "Competitor not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, competitor->project_id, res)) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoCompetitorKeyword keyword;
        keyword.competitor_id = competitor->id;
        keyword.keyword = utils::trim_copy(safe_string(body, "keyword", 160));
        keyword.url = safe_string(body, "url", 4096);
        keyword.source = lower_trim(safe_string(body, "source", 30));
        if (keyword.source.empty()) keyword.source = "manual";
        if (keyword.keyword.size() < 2) {
            json_error(res, 400, "keyword is required");
            return;
        }
        if (body.contains("position") && body["position"].is_number_integer()) {
            keyword.position = std::max(0, std::min(1000, body["position"].get<int>()));
        }
        if (body.contains("search_volume") && body["search_volume"].is_number_integer()) {
            keyword.search_volume = std::max(0, body["search_volume"].get<int>());
        }
        const std::string checked = safe_string(body, "checked_at", 40);
        if (!checked.empty()) keyword.checked_at = utils::normalize_iso8601_utc(checked);
        std::string error;
        if (!db_->create_seo_competitor_keyword(keyword, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"keyword", keyword.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/competitors/([^/]+)/keywords/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto competitor = db_->get_seo_competitor(req.matches[1]);
        if (!competitor) {
            json_error(res, 404, "Competitor not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, competitor->project_id, res)) return;
        if (!db_->delete_seo_competitor_keyword(req.matches[2])) {
            json_error(res, 404, "Competitor keyword not found");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // Backlink inventory and monitoring status
    svr.Get(R"(/api/v1/seo/projects/([^/]+)/backlinks)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto backlinks = db_->list_seo_backlinks(project->id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& backlink : backlinks) rows.push_back(backlink.to_json());
        res.set_content(nlohmann::json({{"backlinks", rows}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/backlinks)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoBacklink backlink;
        backlink.project_id = project->id;
        backlink.source_url = safe_string(body, "source_url", 4096);
        backlink.target_url = safe_string(body, "target_url", 4096);
        backlink.anchor_text = safe_string(body, "anchor_text", 300);
        backlink.domain = utils::normalize_host(safe_string(body, "domain", 253));
        if (backlink.domain.empty()) backlink.domain = utils::normalize_host(backlink.source_url);
        backlink.status = lower_trim(safe_string(body, "status", 20));
        backlink.notes = safe_string(body, "notes", 1000);
        if (body.contains("nofollow")) backlink.nofollow = body["nofollow"].is_boolean() && body["nofollow"].get<bool>();
        if (backlink.status.empty()) backlink.status = "active";
        if (!utils::is_safe_url(backlink.source_url)) {
            json_error(res, 400, "source_url must be a valid http(s) URL");
            return;
        }
        if (!backlink.target_url.empty() && !utils::is_safe_url(backlink.target_url)) {
            json_error(res, 400, "target_url must be a valid http(s) URL or empty");
            return;
        }
        if (backlink.status != "active" && backlink.status != "lost" && backlink.status != "pending") {
            json_error(res, 400, "status must be active, lost, or pending");
            return;
        }
        const std::string checked = safe_string(body, "last_checked_at", 40);
        if (!checked.empty()) backlink.last_checked_at = utils::normalize_iso8601_utc(checked);
        std::string error;
        if (!db_->create_seo_backlink(backlink, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"backlink", backlink.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/seo/backlinks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto backlink = db_->get_seo_backlink(req.matches[1]);
        if (!backlink) {
            json_error(res, 404, "Backlink not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, backlink->project_id, res)) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        SeoBacklink updated = *backlink;
        if (body.contains("source_url")) updated.source_url = safe_string(body, "source_url", 4096);
        if (body.contains("target_url")) updated.target_url = safe_string(body, "target_url", 4096);
        if (body.contains("anchor_text")) updated.anchor_text = safe_string(body, "anchor_text", 300);
        if (body.contains("domain")) updated.domain = utils::normalize_host(safe_string(body, "domain", 253));
        if (body.contains("status")) updated.status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("nofollow")) updated.nofollow = body["nofollow"].is_boolean() && body["nofollow"].get<bool>();
        if (body.contains("notes")) updated.notes = safe_string(body, "notes", 1000);
        if (body.contains("last_checked_at")) {
            const std::string checked = safe_string(body, "last_checked_at", 40);
            updated.last_checked_at = checked.empty() ? "" : utils::normalize_iso8601_utc(checked);
        }
        if (!utils::is_safe_url(updated.source_url) ||
            (!updated.target_url.empty() && !utils::is_safe_url(updated.target_url)) ||
            (updated.status != "active" && updated.status != "lost" && updated.status != "pending")) {
            json_error(res, 400, "source_url, target_url, or status is invalid");
            return;
        }
        if (!db_->update_seo_backlink(updated)) {
            json_error(res, 500, "Could not update backlink");
            return;
        }
        res.set_content(nlohmann::json({{"backlink", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/backlinks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto backlink = db_->get_seo_backlink(req.matches[1]);
        if (!backlink) {
            json_error(res, 404, "Backlink not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, backlink->project_id, res)) return;
        if (!db_->delete_seo_backlink(backlink->id)) {
            json_error(res, 500, "Could not delete backlink");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // Technical SEO audit and content optimization
    svr.Post(R"(/api/v1/seo/projects/([^/]+)/audit)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto site = db_->get_site(project->site_id);
        if (!site) {
            json_error(res, 404, "Site not found");
            return;
        }
        if (!allow_request("seo-audit:" + user->id, 10, 300)) {
            res.set_header("Retry-After", "300");
            json_error(res, 429, "Too many audits. Wait a few minutes and try again.");
            return;
        }
        nlohmann::json body = nlohmann::json::object();
        if (!req.body.empty() && !parse_json_body(req, res, body, 8 * 1024)) return;
        std::string target_url = safe_string(body, "target_url", 4096);
        if (target_url.empty()) target_url = "https://" + project->domain + "/";
        std::string host;
        std::string path;
        std::string pinned_ip;
        std::string error;
        if (!safe_seo_target(target_url, *site, host, path, pinned_ip, error)) {
            json_error(res, 400, error);
            return;
        }
        SeoAudit audit;
        audit.project_id = project->id;
        audit.target_url = target_url;
        audit.checked_at = utils::get_current_iso8601();
        FetchedPage page = fetch_public_page(target_url, *site);
        if (page.error.empty()) {
            FetchedPage robots = fetch_public_page("https://" + project->domain + "/robots.txt", *site);
            FetchedPage sitemap = fetch_public_page("https://" + project->domain + "/sitemap.xml", *site);
            audit.summary = analyze_html(page.body, target_url, page.status,
                                         robots.ok, sitemap.ok, audit).dump();
            audit.status = page.ok ? "completed" : "completed";
        } else {
            audit.status = "failed";
            audit.score = 0;
            add_issue(audit, "critical", "availability", "fetch_failed",
                      page.error, "Check the URL, DNS, TLS certificate, and server availability.",
                      target_url, 100);
            audit.summary = nlohmann::json({
                {"http_status", page.status},
                {"error", page.error},
                {"issue_counts", {{"critical", 1}, {"high", 0}, {"medium", 0}, {"low", 0}, {"info", 0}}}
            }).dump();
        }
        if (!db_->create_seo_audit(audit, error)) {
            json_error(res, 500, error);
            return;
        }
        audit_seo(*db_, *user, "RUN_SEO_AUDIT", project->id,
                  {{"target_url", target_url}, {"score", audit.score}});
        auto saved = db_->get_seo_audit(audit.id, true);
        res.status = 201;
        res.set_content(nlohmann::json({{"audit", saved ? saved->to_json(true) : audit.to_json(true)}}).dump(),
                        "application/json");
    });

    svr.Get(R"(/api/v1/seo/projects/([^/]+)/audits)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto audits = db_->list_seo_audits(project->id, 50);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& audit : audits) rows.push_back(audit.to_json(false));
        res.set_content(nlohmann::json({{"audits", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/seo/audits/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto audit = db_->get_seo_audit(req.matches[1], true);
        if (!audit) {
            json_error(res, 404, "Audit not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, audit->project_id, res)) return;
        res.set_content(nlohmann::json({{"audit", audit->to_json(true)}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/optimize)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto site = db_->get_site(project->site_id);
        if (!site) {
            json_error(res, 404, "Site not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 8 * 1024)) return;
        const std::string keyword = utils::to_lower_copy(utils::trim_copy(safe_string(body, "keyword", 160)));
        std::string target_url = safe_string(body, "target_url", 4096);
        if (target_url.empty()) target_url = "https://" + project->domain + "/";
        if (keyword.size() < 2) {
            json_error(res, 400, "keyword is required");
            return;
        }
        FetchedPage page = fetch_public_page(target_url, *site);
        if (page.error.empty() == false) {
            json_error(res, 502, page.error);
            return;
        }
        const std::string title = regex_capture(page.body, "<title\\b[^>]*>([\\s\\S]*?)</title>");
        const std::string description = meta_content(page.body, "description");
        const std::string h1 = regex_capture(page.body, "<h1\\b[^>]*>([\\s\\S]*?)</h1>");
        const std::string text = strip_html(page.body);
        const std::string lower_text = utils::to_lower_copy(text);
        const std::string lower_title = utils::to_lower_copy(title);
        const std::string lower_description = utils::to_lower_copy(description);
        const std::string lower_h1 = utils::to_lower_copy(h1);
        int occurrences = 0;
        size_t pos = 0;
        while ((pos = lower_text.find(keyword, pos)) != std::string::npos) {
            ++occurrences;
            pos += keyword.size();
        }
        const int words = word_count(text);
        const double density = words > 0 ? (static_cast<double>(occurrences) / words) * 100.0 : 0.0;
        nlohmann::json suggestions = nlohmann::json::array();
        int score = 100;
        if (lower_title.find(keyword) == std::string::npos) {
            suggestions.push_back("Include the target keyword in the title tag.");
            score -= 15;
        }
        if (lower_description.find(keyword) == std::string::npos) {
            suggestions.push_back("Mention the target keyword naturally in the meta description.");
            score -= 10;
        }
        if (lower_h1.find(keyword) == std::string::npos) {
            suggestions.push_back("Use the target keyword in the single H1.");
            score -= 15;
        }
        if (occurrences == 0) {
            suggestions.push_back("The keyword does not appear in the visible body copy.");
            score -= 30;
        } else if (density < 0.5) {
            suggestions.push_back("Keyword density is very low; add a natural contextual mention.");
            score -= 8;
        } else if (density > 3.0) {
            suggestions.push_back("Keyword density is high; reduce repetition and use related terms.");
            score -= 10;
        }
        if (words < 300) {
            suggestions.push_back("Add more substantive content; fewer than 300 words is usually thin.");
            score -= 10;
        }
        if (suggestions.empty()) suggestions.push_back("The page already covers the target keyword well.");
        res.set_content(nlohmann::json({
            {"keyword", keyword},
            {"target_url", target_url},
            {"score", std::max(0, score)},
            {"title", truncate_utf8(title, 200)},
            {"meta_description", truncate_utf8(description, 300)},
            {"h1", truncate_utf8(h1, 200)},
            {"word_count", words},
            {"keyword_occurrences", occurrences},
            {"keyword_density_percent", density},
            {"suggestions", suggestions}
        }).dump(), "application/json");
    });

    // GEO visibility across AI search engines
    svr.Get(R"(/api/v1/seo/projects/([^/]+)/geo)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        auto queries = db_->list_geo_queries(project->id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& query : queries) {
            nlohmann::json row = query.to_json();
            auto mentions = db_->list_geo_mentions(query.id, 100);
            nlohmann::json mention_rows = nlohmann::json::array();
            int mentioned = 0;
            for (const auto& mention : mentions) {
                if (mention.mentioned) ++mentioned;
                mention_rows.push_back(mention.to_json());
            }
            row["mentions"] = mention_rows;
            row["checks"] = static_cast<int>(mentions.size());
            row["mention_rate_percent"] = !mentions.empty()
                ? (static_cast<double>(mentioned) / mentions.size()) * 100.0 : 0.0;
            rows.push_back(row);
        }
        res.set_content(nlohmann::json({{"queries", rows}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/projects/([^/]+)/geo)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto project = seo_project_for_user(*db_, *user, req.matches[1], res);
        if (!project) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        GeoQuery query;
        query.project_id = project->id;
        query.query_text = safe_string(body, "query_text", 300);
        query.country = utils::to_upper_copy(safe_string(body, "country", 2));
        if (query.country.empty()) query.country = project->country;
        query.status = lower_trim(safe_string(body, "status", 20));
        if (query.status.empty()) query.status = "active";
        if (query.query_text.size() < 3) {
            json_error(res, 400, "query_text is required");
            return;
        }
        const std::set<std::string> allowed = {
            "chatgpt", "perplexity", "google_ai_overview", "gemini", "claude", "copilot", "grok"
        };
        if (body.contains("engines") && body["engines"].is_array()) {
            for (const auto& item : body["engines"]) {
                if (!item.is_string()) continue;
                std::string engine = lower_trim(item.get<std::string>());
                if (allowed.count(engine) && query.engines.size() < 10) {
                    if (std::find(query.engines.begin(), query.engines.end(), engine) == query.engines.end()) {
                        query.engines.push_back(engine);
                    }
                }
            }
        }
        if (query.engines.empty()) query.engines = {"chatgpt", "perplexity", "google_ai_overview"};
        std::string error;
        if (!db_->create_geo_query(query, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"query", query.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/seo/geo-queries/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto query = db_->get_geo_query(req.matches[1]);
        if (!query) {
            json_error(res, 404, "GEO query not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, query->project_id, res)) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        GeoQuery updated = *query;
        if (body.contains("query_text")) updated.query_text = safe_string(body, "query_text", 300);
        if (body.contains("country")) updated.country = utils::to_upper_copy(safe_string(body, "country", 2));
        if (body.contains("status")) updated.status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("engines") && body["engines"].is_array()) {
            updated.engines.clear();
            for (const auto& item : body["engines"]) {
                if (item.is_string() && updated.engines.size() < 10) {
                    updated.engines.push_back(lower_trim(item.get<std::string>()).substr(0, 40));
                }
            }
        }
        if (updated.query_text.size() < 3 || updated.engines.empty()) {
            json_error(res, 400, "query_text and at least one engine are required");
            return;
        }
        if (!db_->update_geo_query(updated)) {
            json_error(res, 500, "Could not update GEO query");
            return;
        }
        res.set_content(nlohmann::json({{"query", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/seo/geo-queries/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto query = db_->get_geo_query(req.matches[1]);
        if (!query) {
            json_error(res, 404, "GEO query not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, query->project_id, res)) return;
        if (!db_->delete_geo_query(query->id)) {
            json_error(res, 500, "Could not delete GEO query");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/seo/geo-queries/([^/]+)/mentions)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto query = db_->get_geo_query(req.matches[1]);
        if (!query) {
            json_error(res, 404, "GEO query not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, query->project_id, res)) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        GeoMention mention;
        mention.query_id = query->id;
        mention.engine = lower_trim(safe_string(body, "engine", 40));
        if (mention.engine.empty()) {
            json_error(res, 400, "engine is required");
            return;
        }
        mention.mentioned = body.contains("mentioned") && body["mentioned"].is_boolean() && body["mentioned"].get<bool>();
        mention.sentiment = lower_trim(safe_string(body, "sentiment", 20));
        mention.snippet = safe_string(body, "snippet", 2000);
        mention.source = lower_trim(safe_string(body, "source", 30));
        if (mention.source.empty()) mention.source = "manual";
        if (body.contains("position") && body["position"].is_number_integer()) {
            mention.position = std::max(0, std::min(100, body["position"].get<int>()));
        }
        const std::string checked = safe_string(body, "checked_at", 40);
        if (!checked.empty()) {
            mention.checked_at = utils::normalize_iso8601_utc(checked);
            if (mention.checked_at.empty()) {
                json_error(res, 400, "checked_at must be an ISO-8601 timestamp");
                return;
            }
        }
        std::string error;
        if (!db_->create_geo_mention(mention, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"mention", mention.to_json()}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/seo/geo-queries/([^/]+)/mentions)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto query = db_->get_geo_query(req.matches[1]);
        if (!query) {
            json_error(res, 404, "GEO query not found");
            return;
        }
        if (!seo_project_for_user(*db_, *user, query->project_id, res)) return;
        auto mentions = db_->list_geo_mentions(query->id, 200);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& mention : mentions) rows.push_back(mention.to_json());
        res.set_content(nlohmann::json({{"mentions", rows}}).dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Audience segments and A/B experiments
    // -----------------------------------------------------------------------
    svr.Get("/api/v1/audience-segments", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> site_id;
        std::optional<std::string> publisher_id;
        if (req.has_param("site_id")) {
            site_id = req.get_param_value("site_id");
            auto site = db_->get_site(*site_id);
            if (!site || !can_access_site(*user, *site)) {
                json_error(res, 404, "Site not found");
                return;
            }
        }
        if (is_admin(*user)) {
            if (req.has_param("publisher_id")) publisher_id = req.get_param_value("publisher_id");
        } else {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden");
                return;
            }
            publisher_id = user->publisher_id;
        }
        auto segments = db_->list_audience_segments(site_id, publisher_id);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& segment : segments) rows.push_back(segment.to_json());
        res.set_content(nlohmann::json({{"segments", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v1/audience-segments", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        const std::string site_id = safe_string(body, "site_id", 80);
        auto site = db_->get_site(site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        AudienceSegment segment;
        segment.site_id = site->id;
        segment.publisher_id = site->publisher_id;
        segment.name = safe_string(body, "name", 120);
        segment.description = safe_string(body, "description", 500);
        segment.status = lower_trim(safe_string(body, "status", 20));
        if (segment.status.empty()) segment.status = "active";
        if (!body.contains("rules") || !body["rules"].is_object()) {
            json_error(res, 400, "rules must be an object");
            return;
        }
        nlohmann::json rules = body["rules"];
        if (rules.size() > 20) {
            json_error(res, 400, "rules may contain at most 20 keys");
            return;
        }
        static const std::set<std::string> allowed = {
            "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
            "device", "referrer_contains"
        };
        for (auto it = rules.begin(); it != rules.end(); ++it) {
            if (!allowed.count(it.key())) {
                json_error(res, 400, "Unsupported audience rule: " + it.key());
                return;
            }
            const bool valid = it.value().is_string() ||
                (it.value().is_array() && std::all_of(it.value().begin(), it.value().end(),
                    [](const nlohmann::json& item) { return item.is_string(); }));
            if (!valid) {
                json_error(res, 400, "Audience rule values must be strings or arrays of strings");
                return;
            }
        }
        segment.rules = rules.dump();
        if (segment.name.size() < 2 || (segment.status != "active" && segment.status != "paused")) {
            json_error(res, 400, "name and status are required");
            return;
        }
        std::string error;
        if (!db_->create_audience_segment(segment, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"segment", segment.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/audience-segments/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto segment = db_->get_audience_segment(req.matches[1]);
        if (!segment) {
            json_error(res, 404, "Audience segment not found");
            return;
        }
        auto site = db_->get_site(segment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Audience segment not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        if (body.contains("name")) segment->name = safe_string(body, "name", 120);
        if (body.contains("description")) segment->description = safe_string(body, "description", 500);
        if (body.contains("status")) segment->status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("rules")) {
            if (!body["rules"].is_object()) {
                json_error(res, 400, "rules must be an object");
                return;
            }
            segment->rules = body["rules"].dump();
        }
        if (segment->name.size() < 2 ||
            (segment->status != "active" && segment->status != "paused")) {
            json_error(res, 400, "name and status are required");
            return;
        }
        if (!db_->update_audience_segment(*segment)) {
            json_error(res, 500, "Could not update audience segment");
            return;
        }
        res.set_content(nlohmann::json({{"segment", segment->to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/audience-segments/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto segment = db_->get_audience_segment(req.matches[1]);
        if (!segment) {
            json_error(res, 404, "Audience segment not found");
            return;
        }
        auto site = db_->get_site(segment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Audience segment not found");
            return;
        }
        if (!db_->delete_audience_segment(segment->id)) {
            json_error(res, 500, "Could not delete audience segment");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Get("/api/v1/experiments", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        if (!is_writer(*user) && user->role != "analyst") {
            json_error(res, 403, "Forbidden");
            return;
        }
        std::optional<std::string> site_id;
        std::optional<std::string> placement_id;
        std::optional<std::string> status;
        if (req.has_param("site_id")) {
            site_id = req.get_param_value("site_id");
            auto site = db_->get_site(*site_id);
            if (!site || !can_access_site(*user, *site)) {
                json_error(res, 404, "Site not found");
                return;
            }
        } else if (!is_admin(*user)) {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden");
                return;
            }
            const auto projects = db_->list_sites(user->publisher_id);
            if (projects.empty()) {
                res.set_content(nlohmann::json({{"experiments", nlohmann::json::array()}}).dump(),
                                "application/json");
                return;
            }
        }
        if (req.has_param("placement_id")) placement_id = req.get_param_value("placement_id");
        if (req.has_param("status")) status = lower_trim(req.get_param_value("status"));
        auto experiments = db_->list_experiments(site_id, placement_id, status);
        if (!is_admin(*user)) {
            experiments.erase(std::remove_if(experiments.begin(), experiments.end(),
                [&](const Experiment& experiment) {
                    auto site = db_->get_site(experiment.site_id);
                    return !site || !can_access_site(*user, *site);
                }), experiments.end());
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& experiment : experiments) {
            nlohmann::json row = experiment.to_json();
            row["metrics"] = db_->get_experiment_metrics(experiment.id);
            rows.push_back(row);
        }
        res.set_content(nlohmann::json({{"experiments", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v1/experiments", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 64 * 1024)) return;
        const std::string site_id = safe_string(body, "site_id", 80);
        const std::string placement_id = safe_string(body, "placement_id", 80);
        auto site = db_->get_site(site_id);
        auto placement = db_->get_placement(placement_id);
        if (!site || !placement || placement->site_id != site->id || !can_access_site(*user, *site)) {
            json_error(res, 400, "site_id and placement_id must reference an accessible placement");
            return;
        }
        Experiment experiment;
        experiment.site_id = site->id;
        experiment.placement_id = placement->id;
        experiment.name = safe_string(body, "name", 120);
        experiment.description = safe_string(body, "description", 1000);
        experiment.status = lower_trim(safe_string(body, "status", 20));
        if (experiment.status.empty()) experiment.status = "draft";
        experiment.objective = lower_trim(safe_string(body, "objective", 30));
        if (experiment.objective.empty()) experiment.objective = "ctr";
        if (body.contains("min_impressions") && body["min_impressions"].is_number_integer()) {
            experiment.min_impressions = std::max(1, body["min_impressions"].get<int>());
        }
        if (body.contains("min_lift_percent") && body["min_lift_percent"].is_number_integer()) {
            experiment.min_lift_percent = std::max(0, std::min(1000, body["min_lift_percent"].get<int>()));
        }
        if (body.contains("auto_winner")) experiment.auto_winner = body["auto_winner"].is_boolean() && body["auto_winner"].get<bool>();
        if (!body.contains("variants") || !body["variants"].is_array()) {
            json_error(res, 400, "variants must be an array");
            return;
        }
        if (body["variants"].size() < 2 || body["variants"].size() > 10) {
            json_error(res, 400, "an experiment requires 2 to 10 variants");
            return;
        }
        std::set<std::string> seen_campaigns;
        for (const auto& raw : body["variants"]) {
            if (!raw.is_object()) continue;
            const std::string campaign_id = safe_string(raw, "campaign_id", 80);
            if (campaign_id.empty() || seen_campaigns.count(campaign_id)) continue;
            auto campaign = db_->get_campaign(campaign_id);
            if (!campaign || campaign->site_id != site->id ||
                campaign->placement_id != placement->id) {
                json_error(res, 400, "every variant must be a campaign on this placement");
                return;
            }
            ExperimentVariant variant;
            variant.campaign_id = campaign_id;
            variant.name = safe_string(raw, "name", 120);
            if (variant.name.empty()) variant.name = campaign->name;
            variant.weight = 100;
            if (raw.contains("weight") && raw["weight"].is_number_integer()) {
                variant.weight = std::max(1, std::min(10000, raw["weight"].get<int>()));
            }
            variant.is_control = raw.contains("is_control") && raw["is_control"].is_boolean() &&
                                 raw["is_control"].get<bool>();
            seen_campaigns.insert(campaign_id);
            experiment.variants.push_back(variant);
        }
        if (experiment.variants.size() < 2 || experiment.name.size() < 2 ||
            (experiment.status != "draft" && experiment.status != "running" && experiment.status != "paused") ||
            (experiment.objective != "ctr" && experiment.objective != "conversion_rate" &&
             experiment.objective != "revenue")) {
            json_error(res, 400, "name, objective, status, and at least two unique campaigns are required");
            return;
        }
        std::string error;
        if (!db_->create_experiment(experiment, error)) {
            json_error(res, 400, error);
            return;
        }
        res.status = 201;
        res.set_content(nlohmann::json({{"experiment", experiment.to_json()}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/experiments/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;
        auto experiment = db_->get_experiment(req.matches[1]);
        if (!experiment) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        auto site = db_->get_site(experiment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        res.set_content(nlohmann::json({
            {"experiment", experiment->to_json()},
            {"metrics", db_->get_experiment_metrics(experiment->id)}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/experiments/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto existing = db_->get_experiment(req.matches[1]);
        if (!existing) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        auto site = db_->get_site(existing->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 64 * 1024)) return;
        Experiment experiment = *existing;
        if (body.contains("name")) experiment.name = safe_string(body, "name", 120);
        if (body.contains("description")) experiment.description = safe_string(body, "description", 1000);
        if (body.contains("status")) experiment.status = lower_trim(safe_string(body, "status", 20));
        if (body.contains("objective")) experiment.objective = lower_trim(safe_string(body, "objective", 30));
        if (body.contains("min_impressions") && body["min_impressions"].is_number_integer()) {
            experiment.min_impressions = std::max(1, body["min_impressions"].get<int>());
        }
        if (body.contains("min_lift_percent") && body["min_lift_percent"].is_number_integer()) {
            experiment.min_lift_percent = std::max(0, std::min(1000, body["min_lift_percent"].get<int>()));
        }
        if (body.contains("auto_winner")) experiment.auto_winner = body["auto_winner"].is_boolean() && body["auto_winner"].get<bool>();
        if (body.contains("variants") && body["variants"].is_array()) {
            experiment.variants.clear();
            std::set<std::string> seen;
            for (const auto& raw : body["variants"]) {
                if (!raw.is_object()) continue;
                std::string campaign_id = safe_string(raw, "campaign_id", 80);
                if (campaign_id.empty() || seen.count(campaign_id)) continue;
                auto campaign = db_->get_campaign(campaign_id);
                if (!campaign || campaign->site_id != experiment.site_id ||
                    campaign->placement_id != experiment.placement_id) continue;
                ExperimentVariant variant;
                variant.campaign_id = campaign_id;
                variant.name = safe_string(raw, "name", 120);
                if (variant.name.empty()) variant.name = campaign->name;
                variant.weight = 100;
                if (raw.contains("weight") && raw["weight"].is_number_integer()) {
                    variant.weight = std::max(1, std::min(10000, raw["weight"].get<int>()));
                }
                variant.is_control = raw.contains("is_control") && raw["is_control"].is_boolean() &&
                                     raw["is_control"].get<bool>();
                seen.insert(campaign_id);
                experiment.variants.push_back(variant);
            }
        }
        if (experiment.name.size() < 2 || experiment.variants.size() < 2 ||
            (experiment.status != "draft" && experiment.status != "running" &&
             experiment.status != "paused" && experiment.status != "completed") ||
            (experiment.objective != "ctr" && experiment.objective != "conversion_rate" &&
             experiment.objective != "revenue")) {
            json_error(res, 400, "invalid experiment fields or fewer than two variants");
            return;
        }
        std::string error;
        if (!db_->update_experiment(experiment, error)) {
            json_error(res, 400, error);
            return;
        }
        auto updated = db_->get_experiment(experiment.id);
        res.set_content(nlohmann::json({{"experiment", updated ? updated->to_json() : experiment.to_json()}}).dump(),
                        "application/json");
    });

    svr.Post(R"(/api/v1/experiments/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto experiment = db_->get_experiment(req.matches[1]);
        if (!experiment) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        auto site = db_->get_site(experiment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 4 * 1024)) return;
        const std::string status = lower_trim(safe_string(body, "status", 20));
        if (status != "running" && status != "paused" && status != "completed" && status != "draft") {
            json_error(res, 400, "status must be draft, running, paused, or completed");
            return;
        }
        if (!db_->set_experiment_status(experiment->id, status)) {
            json_error(res, 500, "Could not update experiment status");
            return;
        }
        auto updated = db_->get_experiment(experiment->id);
        res.set_content(nlohmann::json({{"experiment", updated ? updated->to_json() : experiment->to_json()}}).dump(),
                        "application/json");
    });

    svr.Delete(R"(/api/v1/experiments/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto experiment = db_->get_experiment(req.matches[1]);
        if (!experiment) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        auto site = db_->get_site(experiment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        if (!db_->delete_experiment(experiment->id)) {
            json_error(res, 500, "Could not delete experiment");
            return;
        }
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/experiments/([^/]+)/optimize)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto experiment = db_->get_experiment(req.matches[1]);
        if (!experiment) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        auto site = db_->get_site(experiment->site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Experiment not found");
            return;
        }
        int updated = db_->optimize_experiments(utils::get_current_iso8601());
        res.set_content(nlohmann::json({
            {"updated_experiments", updated},
            {"metrics", db_->get_experiment_metrics(experiment->id)}
        }).dump(), "application/json");
    });
}

} // namespace adcontrol
