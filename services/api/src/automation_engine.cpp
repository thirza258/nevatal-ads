#include "automation.hpp"
#include "utils.hpp"
#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string>
#include <vector>

namespace adcontrol {

namespace {

std::string render_template(std::string value, const AutomationWorkflow& workflow,
                            const AutomationRun& run, const nlohmann::json& trigger) {
    auto replace_all = [&](const std::string& needle, const std::string& replacement) {
        size_t pos = 0;
        while ((pos = value.find(needle, pos)) != std::string::npos) {
            value.replace(pos, needle.size(), replacement);
            pos += replacement.size();
        }
    };
    replace_all("{{workflow.name}}", workflow.name);
    replace_all("{{workflow.id}}", workflow.id);
    replace_all("{{run.id}}", run.id);
    replace_all("{{trigger.site_id}}", trigger.value("site_id", workflow.site_id));
    replace_all("{{trigger.campaign_id}}", trigger.value("campaign_id", ""));
    replace_all("{{trigger.metric}}", trigger.value("metric", ""));
    replace_all("{{trigger.actual}}", std::to_string(trigger.value("actual", 0.0)));
    replace_all("{{trigger.threshold}}", std::to_string(trigger.value("threshold", 0.0)));
    return value;
}

bool is_public_ipv4(const sockaddr_in* addr) {
    const uint32_t ip = ntohl(addr->sin_addr.s_addr);
    if ((ip & 0xFF000000U) == 0x00000000U) return false; // 0.0.0.0/8
    if ((ip & 0xFF000000U) == 0x0A000000U) return false; // 10/8
    if ((ip & 0xFF000000U) == 0x7F000000U) return false; // loopback
    if ((ip & 0xFFFF0000U) == 0xA9FE0000U) return false; // link-local
    if ((ip & 0xFFF00000U) == 0xAC100000U) return false; // 172.16/12
    if ((ip & 0xFFFF0000U) == 0xC0A80000U) return false; // 192.168/16
    if ((ip & 0xFFC00000U) == 0x64400000U) return false; // 100.64/10 CGNAT
    if ((ip & 0xFFFE0000U) == 0xC6120000U) return false; // 198.18/15
    if ((ip & 0xFFFFFF00U) == 0xC0000200U) return false; // 192.0.2/24
    if ((ip & 0xFFFFFF00U) == 0xC6336400U) return false; // 198.51.100/24
    if ((ip & 0xFFFFFF00U) == 0xCB007100U) return false; // 203.0.113/24
    if ((ip & 0xF0000000U) == 0xE0000000U) return false; // multicast/reserved
    if (ip == 0xFFFFFFFFU) return false;
    return true;
}

bool is_public_ipv6(const sockaddr_in6* addr) {
    const unsigned char* bytes = addr->sin6_addr.s6_addr;
    if (IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_UNSPECIFIED(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) return false;
    if (IN6_IS_ADDR_SITELOCAL(&addr->sin6_addr)) return false;
    if ((bytes[0] & 0xFE) == 0xFC) return false; // fc00::/7 ULA
    if (bytes[0] == 0xFF) return false;          // multicast
    if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0D && bytes[3] == 0xB8) {
        return false; // documentation
    }
    // IPv4-mapped address (::ffff:a.b.c.d)
    if (IN6_IS_ADDR_V4MAPPED(&addr->sin6_addr)) {
        sockaddr_in mapped{};
        mapped.sin_family = AF_INET;
        std::memcpy(&mapped.sin_addr, bytes + 12, 4);
        return is_public_ipv4(&mapped);
    }
    return true;
}

bool is_public_address(const addrinfo* info) {
    if (!info || !info->ai_addr) return false;
    if (info->ai_family == AF_INET) {
        return is_public_ipv4(reinterpret_cast<const sockaddr_in*>(info->ai_addr));
    }
    if (info->ai_family == AF_INET6) {
        return is_public_ipv6(reinterpret_cast<const sockaddr_in6*>(info->ai_addr));
    }
    return false;
}

struct WebhookTarget {
    std::string host;
    std::string port;
    std::string path;
    std::string ip;
};

bool resolve_webhook_target(const std::string& raw_url, WebhookTarget& target) {
    if (raw_url.size() < 12 || raw_url.size() > 2048) return false;
    if (utils::to_lower_copy(raw_url).rfind("https://", 0) != 0) return false;
    const std::string rest = raw_url.substr(8);
    const size_t path_start = rest.find_first_of("/?#");
    const std::string authority = rest.substr(0, path_start);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    if (authority.find('[') != std::string::npos || authority.find(']') != std::string::npos) {
        return false; // Keep webhook hosts to DNS names, not IPv6 literals.
    }
    std::string host = authority;
    std::string port = "443";
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 ||
            !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) {
            return false;
        }
        int port_number = 0;
        try { port_number = std::stoi(port); } catch (...) { return false; }
        if (port_number < 1 || port_number > 65535) return false;
    }
    host = utils::to_lower_copy(host);
    if (host.empty() || host.size() > 253 || host.find('.') == std::string::npos) return false;
    if (host == "localhost" || host.rfind("localhost.", 0) == 0 ||
        (host.size() > 6 && host.compare(host.size() - 6, 6, ".local") == 0) ||
        (host.size() > 9 && host.compare(host.size() - 9, 9, ".internal") == 0)) {
        return false;
    }
    if (!utils::is_valid_domain_entry(host)) return false;

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &resolved) != 0 || !resolved) {
        return false;
    }
    std::string selected_ip;
    bool any_public = false;
    for (addrinfo* it = resolved; it; it = it->ai_next) {
        if (!is_public_address(it)) {
            any_public = false;
            break;
        }
        any_public = true;
        if (it->ai_family == AF_INET) {
            char buffer[INET_ADDRSTRLEN] = {0};
            const auto* in = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
            if (inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer))) selected_ip = buffer;
        } else if (it->ai_family == AF_INET6 && selected_ip.empty()) {
            char buffer[INET6_ADDRSTRLEN] = {0};
            const auto* in6 = reinterpret_cast<const sockaddr_in6*>(it->ai_addr);
            if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer))) selected_ip = buffer;
        }
    }
    freeaddrinfo(resolved);
    if (!any_public || selected_ip.empty()) return false;

    target.host = host;
    target.port = port;
    target.path = path_start == std::string::npos ? "/" : rest.substr(path_start);
    if (target.path.empty() || target.path[0] == '#') target.path = "/";
    target.ip = selected_ip;
    return true;
}

bool deliver_webhook(const WebhookTarget& target, const std::string& body,
                     const std::map<std::string, std::string>& extra_headers,
                     int& status_code, std::string& response_body) {
    const std::string base = "https://" + target.host +
        (target.port == "443" ? "" : ":" + target.port);
    httplib::Client client(base);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(10, 0);
    client.set_write_timeout(10, 0);
    client.set_follow_location(false);
    client.enable_server_certificate_verification(true);
    client.set_hostname_addr_map({{target.host, target.ip}});
    httplib::Headers headers = {
        {"User-Agent", "AdControl-Automation/1.0"},
        {"Content-Type", "application/json"}
    };
    for (const auto& [key, value] : extra_headers) {
        if (key.empty()) continue;
        headers.emplace(key, value);
    }
    auto result = client.Post(target.path, headers, body, "application/json");
    if (!result) return false;
    status_code = result->status;
    response_body = result->body.substr(0, 2000);
    return result->status >= 200 && result->status < 300;
}

std::string condition_scope(const AutomationWorkflow& workflow, const nlohmann::json& config,
                            const nlohmann::json& trigger, bool want_campaign) {
    if (want_campaign) {
        if (config.contains("campaign_id") && config["campaign_id"].is_string()) {
            return config["campaign_id"].get<std::string>();
        }
        return trigger.value("campaign_id", "");
    }
    if (config.contains("site_id") && config["site_id"].is_string()) {
        return config["site_id"].get<std::string>();
    }
    std::string site = trigger.value("site_id", "");
    return site.empty() ? workflow.site_id : site;
}

bool campaign_in_scope(Database& db, const AutomationWorkflow& workflow,
                       const Campaign& campaign) {
    if (!workflow.site_id.empty() && campaign.site_id != workflow.site_id) return false;
    if (!workflow.publisher_id.empty()) {
        auto site = db.get_site(campaign.site_id);
        if (!site || site->publisher_id != workflow.publisher_id) return false;
    }
    return true;
}

} // namespace

int run_automation_tick(Database& db, int max_runs) {
    const std::string now = utils::get_current_iso8601();
    db.create_due_scheduled_runs(now);
    db.evaluate_performance_triggers(now);

    auto runs = db.list_due_automation_runs(now, max_runs);
    int processed = 0;
    for (const auto& initial_run : runs) {
        AutomationRun run = initial_run;
        auto workflow_opt = db.get_automation_workflow(run.workflow_id);
        if (!workflow_opt) {
            run.status = "failed";
            run.error = "Workflow not found";
            run.completed_at = now;
            db.update_automation_run(run);
            continue;
        }
        if (workflow_opt->status != "active") {
            run.status = "skipped";
            run.error = "Workflow is not active";
            run.completed_at = now;
            db.update_automation_run(run);
            continue;
        }
        const AutomationWorkflow workflow = *workflow_opt;
        nlohmann::json trigger;
        try {
            trigger = nlohmann::json::parse(run.trigger_payload.empty() ? "{}" : run.trigger_payload);
        } catch (...) {
            trigger = nlohmann::json::object();
        }

        if (run.started_at.empty()) run.started_at = now;
        run.status = "running";
        run.error.clear();
        bool stopped_for_delay = false;
        bool failed = false;

        auto log = [&](const std::string& message) {
            if (!run.log.empty()) run.log += "\n";
            run.log += "[" + utils::get_current_iso8601() + "] " + message;
            if (run.log.size() > 16000) run.log = run.log.substr(run.log.size() - 16000);
        };
        if (run.current_step == 0) log("Run started from trigger " + workflow.trigger_type);

        for (size_t index = static_cast<size_t>(std::max(0, run.current_step));
             index < workflow.steps.size(); ++index) {
            const AutomationStep& step = workflow.steps[index];
            nlohmann::json config;
            try {
                config = nlohmann::json::parse(step.config.empty() ? "{}" : step.config);
            } catch (...) {
                config = nlohmann::json::object();
            }

            if (step.type == "condition") {
                std::string campaign_id = condition_scope(workflow, config, trigger, true);
                std::string site_id = condition_scope(workflow, config, trigger, false);
                if (site_id.empty() && campaign_id.empty()) {
                    log("Condition skipped: no site or campaign scope");
                } else {
                    int lookback_hours = 24;
                    try { lookback_hours = std::max(1, std::min(config.value("lookback_hours", 24), 720)); } catch (...) {}
                    auto metrics = db.get_automation_metrics(site_id, campaign_id, lookback_hours);
                    long long impressions = metrics.value("impressions", 0LL);
                    int min_impressions = 0;
                    try { min_impressions = std::max(0, config.value("min_impressions", 0)); } catch (...) {}
                    const std::string metric = config.value("metric", "ctr");
                    const std::string comparator = config.value("comparator", "lt");
                    double threshold = 0.0;
                    try { threshold = config.value("threshold", 0.0); } catch (...) {}
                    double actual = metrics.value("ctr", 0.0);
                    if (metric == "conversion_rate") actual = metrics.value("conversion_rate", 0.0);
                    else if (metric == "impressions") actual = static_cast<double>(impressions);
                    else if (metric == "clicks") actual = static_cast<double>(metrics.value("clicks", 0LL));
                    else if (metric == "conversions") actual = static_cast<double>(metrics.value("conversions", 0LL));
                    else if (metric == "commission_cents") actual = static_cast<double>(metrics.value("commission_cents", 0LL));
                    bool passes = true;
                    if (impressions < min_impressions) passes = false;
                    else if (comparator == "gt") passes = actual > threshold;
                    else if (comparator == "gte") passes = actual >= threshold;
                    else if (comparator == "lte") passes = actual <= threshold;
                    else passes = actual < threshold;
                    log("Condition " + metric + " " + comparator + " " + std::to_string(threshold) +
                        " evaluated " + std::to_string(actual) + (passes ? ": passed" : ": not met"));
                    if (!passes) {
                        run.status = "skipped";
                        run.completed_at = utils::get_current_iso8601();
                        run.current_step = static_cast<int>(index + 1);
                        db.update_automation_run(run);
                        stopped_for_delay = true;
                        break;
                    }
                }
                run.current_step = static_cast<int>(index + 1);
                db.update_automation_run(run);
                continue;
            }

            if (step.type == "delay") {
                int amount = 1;
                try { amount = std::max(1, config.value("amount", 1)); } catch (...) {}
                std::string unit = config.value("unit", "minutes");
                int seconds = unit == "seconds" ? 1 : unit == "hours" ? 3600 : unit == "days" ? 86400 : 60;
                const long long wait_seconds = std::max(1LL, static_cast<long long>(amount) * seconds);
                run.resume_at = utils::iso8601_from_epoch(
                    utils::utc_now_epoch() + static_cast<std::time_t>(wait_seconds));
                run.current_step = static_cast<int>(index + 1);
                run.status = "waiting";
                log("Delayed for " + std::to_string(wait_seconds) + " seconds");
                db.update_automation_run(run);
                stopped_for_delay = true;
                break;
            }

            if (step.type != "action") {
                log("Unknown step type '" + step.type + "' ignored");
                run.current_step = static_cast<int>(index + 1);
                db.update_automation_run(run);
                continue;
            }

            const std::string action = config.value("action", config.value("action_type", ""));
            if (action == "pause_campaign" || action == "activate_campaign") {
                std::string campaign_id = config.value("campaign_id", "");
                if (campaign_id.empty()) campaign_id = trigger.value("campaign_id", "");
                auto campaign = db.get_campaign(campaign_id);
                if (!campaign || !campaign_in_scope(db, workflow, *campaign)) {
                    log("Action failed: campaign not found or outside workflow scope");
                    run.status = "failed";
                    run.error = "Campaign not found or outside workflow scope";
                    run.completed_at = utils::get_current_iso8601();
                    failed = true;
                } else {
                    const bool ok = action == "pause_campaign"
                        ? db.pause_campaign(campaign_id) : db.publish_campaign(campaign_id);
                    log(std::string(action == "pause_campaign" ? "Paused" : "Activated") +
                        " campaign " + campaign->name + (ok ? "" : " (no change)"));
                    if (!ok) {
                        run.status = "failed";
                        run.error = "Campaign state change failed";
                        run.completed_at = utils::get_current_iso8601();
                        failed = true;
                    }
                }
            } else if (action == "send_notification") {
                AppNotification notification;
                notification.publisher_id = workflow.publisher_id;
                notification.kind = config.value("kind", "warning");
                notification.title = render_template(config.value("title", "Automation notification"),
                                                     workflow, run, trigger);
                notification.body = render_template(config.value("body", ""), workflow, run, trigger);
                notification.link = render_template(config.value("link", "#automation"), workflow, run, trigger);
                if (!db.create_notification(notification)) {
                    log("Action failed: notification could not be saved");
                    run.status = "failed";
                    run.error = "Notification creation failed";
                    run.completed_at = utils::get_current_iso8601();
                    failed = true;
                } else {
                    log("Notification sent: " + notification.title);
                }
            } else if (action == "webhook") {
                const std::string url = render_template(config.value("url", ""), workflow, run, trigger);
                nlohmann::json outgoing = {
                    {"workflow", {{"id", workflow.id}, {"name", workflow.name}, {"trigger_type", workflow.trigger_type}}},
                    {"run_id", run.id},
                    {"trigger", trigger},
                    {"timestamp", utils::get_current_iso8601()}
                };
                if (config.contains("payload") && config["payload"].is_object()) {
                    outgoing["data"] = config["payload"];
                }
                WebhookTarget target;
                if (!resolve_webhook_target(url, target)) {
                    log("Action failed: webhook URL must be a public HTTPS endpoint");
                    run.status = "failed";
                    run.error = "Unsafe or invalid webhook URL";
                    run.completed_at = utils::get_current_iso8601();
                    failed = true;
                } else {
                    std::map<std::string, std::string> headers = {
                        {"X-AdControl-Workflow", workflow.id},
                        {"X-AdControl-Run", run.id}
                    };
                    if (config.contains("headers") && config["headers"].is_object()) {
                        for (auto it = config["headers"].begin(); it != config["headers"].end(); ++it) {
                            if (it.value().is_string()) headers[it.key()] = it.value().get<std::string>();
                        }
                    }
                    int status_code = 0;
                    std::string response_body;
                    const bool ok = deliver_webhook(target, outgoing.dump(), headers, status_code, response_body);
                    log("Webhook POST " + target.host + target.path + " returned " +
                        std::to_string(status_code) + (ok ? "" : " (failed)"));
                    if (!ok) {
                        run.status = "failed";
                        run.error = "Webhook delivery failed with status " + std::to_string(status_code);
                        run.completed_at = utils::get_current_iso8601();
                        failed = true;
                    }
                }
            } else {
                log("Unknown action '" + action + "' ignored");
            }

            if (failed) break;
            run.current_step = static_cast<int>(index + 1);
            db.update_automation_run(run);
        }

        if (failed || stopped_for_delay) {
            ++processed;
            continue;
        }
        if (run.status != "skipped") {
            run.status = "completed";
            run.resume_at.clear();
            run.completed_at = utils::get_current_iso8601();
            log("Run completed");
            db.update_automation_run(run);
        }
        ++processed;
    }
    return processed;
}

} // namespace adcontrol
