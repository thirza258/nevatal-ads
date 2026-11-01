#include "handlers.hpp"
#include "utils.hpp"
#include "smtp.hpp"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <set>
#include <map>
#include <ctime>
#include <resolv.h>
#include <arpa/nameser.h>

namespace adcontrol {

namespace {

constexpr int CLICK_TOKEN_TTL_SECONDS = 24 * 60 * 60;
constexpr const char* SDK_VERSION = "1.3.0";
constexpr const char* DEFAULT_AFFILIATE_DISCLOSURE =
    "Affiliate link: we may earn a commission from qualifying purchases.";
constexpr const char* VERIFICATION_RECORD_PREFIX = "_adcontrol-challenge.";
constexpr const char* VERIFICATION_VALUE_PREFIX = "adcontrol-site-verification=";

// URL-safe base64 (no padding) used for click tokens.
const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

std::string base64_url_encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    return out;
}

std::string base64_url_decode(const std::string& in) {
    std::string out;
    std::vector<int> table(256, -1);
    for (int i = 0; i < 64; i++) table[static_cast<unsigned char>(base64_chars[i])] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (table[c] == -1) break;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string env_or(const char* key, const std::string& fallback) {
    const char* value = std::getenv(key);
    return (value && *value) ? std::string(value) : fallback;
}

int env_int_or(const char* key, int fallback, int min_value, int max_value) {
    const char* value = std::getenv(key);
    if (!value || !*value) return fallback;
    try {
        int parsed = std::stoi(value);
        if (parsed < min_value) return min_value;
        if (parsed > max_value) return max_value;
        return parsed;
    } catch (...) {
        return fallback;
    }
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

void json_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(nlohmann::json({{"error", message}}).dump(), "application/json");
}

bool parse_json_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& body,
                     size_t max_bytes = 64 * 1024) {
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

bool read_string(const nlohmann::json& body, const char* key, std::string& out, std::string& err) {
    if (!body.contains(key)) return true;
    const auto& value = body[key];
    if (value.is_null()) {
        out.clear();
        return true;
    }
    if (!value.is_string()) {
        err = std::string(key) + " must be a string";
        return false;
    }
    out = value.get<std::string>();
    return true;
}

bool read_int(const nlohmann::json& body, const char* key, int& out, std::string& err) {
    if (!body.contains(key)) return true;
    const auto& value = body[key];
    if (!value.is_number_integer()) {
        err = std::string(key) + " must be an integer";
        return false;
    }
    out = value.get<int>();
    return true;
}

bool read_string_array(const nlohmann::json& body, const char* key,
                       std::vector<std::string>& out, std::string& err,
                       size_t max_items = 50) {
    if (!body.contains(key)) return true;
    if (!body[key].is_array()) {
        err = std::string(key) + " must be an array of strings";
        return false;
    }
    out.clear();
    for (const auto& item : body[key]) {
        if (!item.is_string()) {
            err = std::string(key) + " must be an array of strings";
            return false;
        }
        std::string value = utils::trim_copy(item.get<std::string>());
        if (value.empty()) continue;
        if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
    }
    if (out.size() > max_items) {
        err = std::string(key) + " has too many entries";
        return false;
    }
    return true;
}

bool read_layout_config(const nlohmann::json& body, std::string& out, std::string& err) {
    if (!body.contains("layout_config")) return true;
    const auto& value = body["layout_config"];
    if (value.is_object()) {
        out = value.dump();
        return true;
    }
    if (value.is_string()) {
        try {
            auto parsed = nlohmann::json::parse(value.get<std::string>());
            if (!parsed.is_object()) {
                err = "layout_config must be a JSON object";
                return false;
            }
            out = parsed.dump();
            return true;
        } catch (...) {
            err = "layout_config must be valid JSON";
            return false;
        }
    }
    err = "layout_config must be a JSON object";
    return false;
}

bool is_valid_source_type(const std::string& value) {
    return value == "house" || value == "direct" ||
           value == "affiliate" || value == "external_network";
}

bool is_valid_verification_status(const std::string& value) {
    return value == "unverified" || value == "verified" || value == "rejected";
}

bool is_valid_monetization_status(const std::string& value) {
    return value == "pending" || value == "enabled" || value == "disabled";
}

bool is_valid_offer_status(const std::string& value) {
    return value == "draft" || value == "active" || value == "paused" ||
           value == "expired" || value == "rejected" || value == "archived";
}

bool is_valid_provider(const std::string& value) {
    return value == "shopee_affiliate" || value == "manual_affiliate";
}

bool is_valid_disclosure_mode(const std::string& value) {
    return value == "required" || value == "inherited" ||
           value == "disabled_only_if_not_applicable";
}

bool is_valid_format(const std::string& format) {
    return format == "responsive_banner" || format == "image" ||
           format == "text_link" || format == "native_card" ||
           format == "product_card";
}

// Affiliate destinations are stored and served byte-for-byte. Validation is
// therefore strict about the exact input: we reject rather than rewrite. HTTPS
// is mandatory, credentials-in-URL and control characters are rejected, and no
// tracking parameters are ever added, removed, or reordered.
bool validate_affiliate_url(const std::string& url, std::string& err) {
    if (url.empty() || url.size() > 2048) {
        err = "affiliate_url is required (max 2048 characters)";
        return false;
    }
    if (url != utils::trim_copy(url)) {
        err = "affiliate_url must not have leading or trailing whitespace";
        return false;
    }
    if (!utils::is_safe_url(url)) {
        err = "affiliate_url must be a well-formed http(s) URL without credentials or control characters";
        return false;
    }
    if (utils::to_lower_copy(url).rfind("https://", 0) != 0) {
        err = "affiliate_url must use https://";
        return false;
    }
    const std::string rest = url.substr(8);
    const size_t host_end = rest.find_first_of("/?#");
    const std::string authority = rest.substr(0, host_end);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        err = "affiliate_url must contain a valid host and no userinfo";
        return false;
    }
    std::string host = authority;
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        const std::string port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.empty() || port.size() > 5 ||
            !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) {
            err = "affiliate_url has an invalid port";
            return false;
        }
    }
    if (!utils::is_valid_domain_entry(host) || host == "localhost" ||
        host.find('.') == std::string::npos) {
        err = "affiliate_url must reference a public hostname";
        return false;
    }
    return true;
}

bool validate_optional_https_url(const std::string& url, const char* label, std::string& err) {
    if (url.empty()) return true;
    if (!utils::is_safe_url(url)) {
        err = std::string(label) + " must be a safe http:// or https:// URL or empty";
        return false;
    }
    return true;
}

// Resolve the TXT record for _adcontrol-challenge.<domain>. Returns true only
// when a TXT record exactly matches the per-site token. No HTTP fetch is made,
// so offer verification cannot be used as an SSRF primitive.
bool dns_txt_has_verification_token(const std::string& domain_entry, const std::string& token) {
    if (token.empty()) return false;
    std::string domain = utils::normalize_host(domain_entry);
    const size_t colon = domain.rfind(':');
    if (colon != std::string::npos) domain = domain.substr(0, colon);
    if (domain.empty() || domain.rfind("*.", 0) == 0 || domain == "*") return false;

    const std::string record = std::string(VERIFICATION_RECORD_PREFIX) + domain;
    unsigned char answer[4096];
    const int length = res_query(record.c_str(), C_IN, T_TXT, answer, sizeof(answer));
    if (length <= 0) return false;

    ns_msg msg;
    if (ns_initparse(answer, length, &msg) < 0) return false;

    const std::string expected = std::string(VERIFICATION_VALUE_PREFIX) + token;
    const int count = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < count; ++i) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) != 0) continue;
        if (ns_rr_type(rr) != ns_t_txt) continue;
        const unsigned char* rdata = ns_rr_rdata(rr);
        const int rdlen = ns_rr_rdlen(rr);
        std::string txt;
        int pos = 0;
        while (pos < rdlen) {
            const int chunk = rdata[pos++];
            if (chunk <= 0 || pos + chunk > rdlen) break;
            txt.append(reinterpret_cast<const char*>(rdata + pos), static_cast<size_t>(chunk));
            pos += chunk;
        }
        if (txt == expected) return true;
    }
    return false;
}

std::string effective_disclosure_text(const Campaign& campaign,
                                      const AffiliateOffer* offer,
                                      const Site& site,
                                      const Placement& placement) {
    if (!campaign.disclosure_override.empty()) return campaign.disclosure_override;
    if (campaign.source_type == "affiliate") {
        if (offer && !offer->disclosure_text.empty()) return offer->disclosure_text;
        if (!site.default_disclosure_text.empty()) return site.default_disclosure_text;
        return DEFAULT_AFFILIATE_DISCLOSURE;
    }
    if (placement.disclosure_mode == "required" && !site.default_disclosure_text.empty()) {
        return site.default_disclosure_text;
    }
    return "";
}

bool is_admin_user(const User& user) {
    return user.role == "admin";
}

bool is_writer_user(const User& user) {
    return user.role == "admin" || user.role == "publisher";
}

bool can_access_site(const User& user, const Site& site) {
    if (is_admin_user(user)) return true;
    return !user.publisher_id.empty() && site.publisher_id == user.publisher_id;
}

bool can_access_publisher(const User& user, const std::string& publisher_id) {
    if (is_admin_user(user)) return true;
    return !user.publisher_id.empty() && user.publisher_id == publisher_id;
}

bool publisher_delivery_enabled(Database& db, const Site& site) {
    if (site.publisher_id.empty()) return true;
    auto publisher = db.get_publisher(site.publisher_id);
    return publisher && publisher->status == "active";
}

bool source_allowed_for_site(const Site& site, const std::string& source_type) {
    return std::find(site.allowed_ad_sources.begin(), site.allowed_ad_sources.end(),
                     source_type) != site.allowed_ad_sources.end();
}

std::string normalized_domain_no_port(const std::string& raw) {
    std::string domain = utils::normalize_host(raw);
    const size_t colon = domain.rfind(':');
    if (colon != std::string::npos) domain = domain.substr(0, colon);
    return domain;
}

// Resolves analytics filters and enforces tenant scope. Administrators may see
// all publishers or filter explicitly; every other role is pinned to its own
// publisher regardless of query parameters.
bool resolve_analytics_scope(Database& db, const User& user, const httplib::Request& req,
                             std::optional<std::string>& publisher_scope,
                             std::optional<std::string>& site_id,
                             std::optional<std::string>& placement_id,
                             std::optional<std::string>& campaign_id,
                             httplib::Response& res) {
    if (req.has_param("site_id")) site_id = req.get_param_value("site_id");
    if (req.has_param("placement_id")) placement_id = req.get_param_value("placement_id");
    if (req.has_param("campaign_id")) campaign_id = req.get_param_value("campaign_id");

    if (is_admin_user(user)) {
        if (req.has_param("publisher_id")) publisher_scope = req.get_param_value("publisher_id");
        return true;
    }
    if (user.publisher_id.empty()) {
        json_error(res, 403, "Forbidden: account is not linked to a publisher");
        return false;
    }
    publisher_scope = user.publisher_id;

    if (site_id.has_value()) {
        auto site = db.get_site(*site_id);
        if (!site || !can_access_site(user, *site)) {
            json_error(res, 404, "Site not found");
            return false;
        }
    }
    if (campaign_id.has_value()) {
        auto campaign = db.get_campaign(*campaign_id);
        auto site = campaign ? db.get_site(campaign->site_id) : std::nullopt;
        if (!campaign || !site || !can_access_site(user, *site)) {
            json_error(res, 404, "Campaign not found");
            return false;
        }
    }
    if (placement_id.has_value()) {
        auto placement = db.get_placement(*placement_id);
        auto site = placement ? db.get_site(placement->site_id) : std::nullopt;
        if (!placement || !site || !can_access_site(user, *site)) {
            json_error(res, 404, "Placement not found");
            return false;
        }
    }
    return true;
}

bool is_valid_fallback(const std::string& value) {
    return value == "hide" || value == "blank" || value == "custom";
}

bool is_valid_site_status(const std::string& value) {
    return value == "active" || value == "disabled";
}

bool is_valid_campaign_status(const std::string& value) {
    return value == "draft" || value == "scheduled" || value == "active" ||
           value == "paused" || value == "expired" || value == "archived";
}

bool is_valid_placement_key(const std::string& key) {
    static const std::regex re(R"(^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$)");
    return std::regex_match(key, re);
}

bool too_long(const std::string& value, size_t max_len) {
    return value.size() > max_len;
}

bool validate_site(Site& site, std::string& err) {
    site.name = utils::trim_copy(site.name);
    if (site.name.empty() || too_long(site.name, 120)) {
        err = "name is required (max 120 characters)";
        return false;
    }

    site.primary_domain = utils::normalize_host(site.primary_domain);
    if (site.primary_domain.empty() || site.primary_domain.rfind("*", 0) == 0 ||
        !utils::is_valid_domain_entry(site.primary_domain)) {
        err = "primary_domain must be a valid hostname, optionally with a port";
        return false;
    }

    std::vector<std::string> normalized;
    for (const auto& raw : site.allowed_domains) {
        std::string domain = utils::normalize_host(raw);
        if (domain.empty()) continue;
        if (!utils::is_valid_domain_entry(domain)) {
            err = "invalid allowed domain: " + domain;
            return false;
        }
        if (std::find(normalized.begin(), normalized.end(), domain) == normalized.end()) {
            normalized.push_back(domain);
        }
    }
    if (normalized.empty()) normalized.push_back(site.primary_domain);
    if (normalized.size() > 50) {
        err = "at most 50 allowed domains are permitted";
        return false;
    }
    site.allowed_domains = normalized;

    if (!is_valid_site_status(site.status)) {
        err = "status must be active or disabled";
        return false;
    }

    site.default_disclosure_text = utils::trim_copy(site.default_disclosure_text);
    if (too_long(site.default_disclosure_text, 500)) {
        err = "default_disclosure_text must be 500 characters or fewer";
        return false;
    }
    if (site.allowed_ad_sources.empty()) {
        err = "allowed_ad_sources must contain at least one source";
        return false;
    }
    for (const auto& source : site.allowed_ad_sources) {
        if (!is_valid_source_type(source) || source == "external_network") {
            err = "allowed_ad_sources may contain house, direct, or affiliate";
            return false;
        }
    }
    if (!is_valid_monetization_status(site.monetization_status)) {
        err = "invalid monetization_status";
        return false;
    }
    if (!is_valid_verification_status(site.verification_status)) {
        err = "invalid verification_status";
        return false;
    }
    return true;
}

bool validate_placement(Placement& placement, std::string& err) {
    placement.placement_key = utils::trim_copy(placement.placement_key);
    placement.name = utils::trim_copy(placement.name);
    if (placement.site_id.empty()) {
        err = "site_id is required";
        return false;
    }
    if (!is_valid_placement_key(placement.placement_key)) {
        err = "placement_key must start with a letter/number and contain only letters, numbers, _ or - (max 64)";
        return false;
    }
    if (placement.name.empty() || too_long(placement.name, 120)) {
        err = "name is required (max 120 characters)";
        return false;
    }
    if (too_long(placement.description, 500)) {
        err = "description must be 500 characters or fewer";
        return false;
    }
    if (!is_valid_format(placement.format)) {
        err = "invalid placement format";
        return false;
    }
    if (!is_valid_fallback(placement.fallback_behavior)) {
        err = "fallback_behavior must be hide, blank, or custom";
        return false;
    }
    if (placement.width < 0 || placement.width > 4096 ||
        placement.height < 0 || placement.height > 4096) {
        err = "width and height must be between 0 and 4096";
        return false;
    }
    if (!is_valid_site_status(placement.status)) {
        err = "status must be active or disabled";
        return false;
    }
    if (placement.allowed_source_types.empty()) {
        err = "allowed_source_types must contain at least one source";
        return false;
    }
    for (const auto& source : placement.allowed_source_types) {
        if (!is_valid_source_type(source) || source == "external_network") {
            err = "allowed_source_types may contain house, direct, or affiliate";
            return false;
        }
    }
    if (placement.max_items < 1 || placement.max_items > 20) {
        err = "max_items must be between 1 and 20";
        return false;
    }
    if (!is_valid_disclosure_mode(placement.disclosure_mode)) {
        err = "disclosure_mode must be required, inherited, or disabled_only_if_not_applicable";
        return false;
    }
    if (too_long(placement.layout_config, 4096)) {
        err = "layout_config must be 4096 characters or fewer";
        return false;
    }
    try {
        auto layout = nlohmann::json::parse(placement.layout_config.empty() ? "{}" : placement.layout_config);
        if (!layout.is_object()) {
            err = "layout_config must be a JSON object";
            return false;
        }
    } catch (...) {
        err = "layout_config must be valid JSON";
        return false;
    }
    return true;
}

void apply_schedule_and_status(Campaign& campaign) {
    const std::string now = utils::get_current_iso8601();
    if (campaign.status == "active" || campaign.status == "scheduled") {
        if (!campaign.ends_at.empty() && campaign.ends_at < now) {
            campaign.status = "expired";
        } else if (!campaign.starts_at.empty() && campaign.starts_at > now) {
            campaign.status = "scheduled";
        } else {
            campaign.status = "active";
        }
    }
}

bool validate_campaign(Campaign& campaign, std::string& err) {
    campaign.name = utils::trim_copy(campaign.name);
    if (campaign.name.empty() || too_long(campaign.name, 120)) {
        err = "name is required (max 120 characters)";
        return false;
    }
    if (campaign.site_id.empty() || campaign.placement_id.empty()) {
        err = "site_id and placement_id are required";
        return false;
    }
    if (!is_valid_source_type(campaign.source_type)) {
        err = "invalid source_type";
        return false;
    }
    if (campaign.source_type == "external_network") {
        err = "external_network delivery is not enabled until a provider is approved";
        return false;
    }
    if (!is_valid_format(campaign.format)) {
        err = "invalid campaign format";
        return false;
    }
    if (campaign.source_type == "affiliate") {
        if (!validate_affiliate_url(campaign.destination_url, err)) {
            return false;
        }
    } else if (!utils::is_safe_url(campaign.destination_url)) {
        err = "destination_url must be a safe http:// or https:// URL";
        return false;
    }
    if (!utils::is_safe_optional_url(campaign.image_url)) {
        err = "image_url must be a safe http:// or https:// URL or empty";
        return false;
    }
    if (too_long(campaign.disclosure_override, 500)) {
        err = "disclosure_override must be 500 characters or fewer";
        return false;
    }
    if (campaign.format == "product_card") {
        if (campaign.title.empty()) {
            err = "product_card campaigns require a title";
            return false;
        }
        if (campaign.image_url.empty()) {
            err = "product_card campaigns require an image_url";
            return false;
        }
    }
    if (!campaign.title.empty() && too_long(campaign.title, 200)) {
        err = "title must be 200 characters or fewer";
        return false;
    }
    if (!campaign.body.empty() && too_long(campaign.body, 1000)) {
        err = "body must be 1000 characters or fewer";
        return false;
    }
    if (!campaign.alt_text.empty() && too_long(campaign.alt_text, 300)) {
        err = "alt_text must be 300 characters or fewer";
        return false;
    }
    if (!campaign.cta_label.empty() && too_long(campaign.cta_label, 60)) {
        err = "cta_label must be 60 characters or fewer";
        return false;
    }
    if (!is_valid_campaign_status(campaign.status)) {
        err = "invalid campaign status";
        return false;
    }
    if (campaign.priority < 0 || campaign.priority > 1000) {
        err = "priority must be between 0 and 1000";
        return false;
    }
    if (campaign.weight < 1 || campaign.weight > 10000) {
        err = "weight must be between 1 and 10000";
        return false;
    }

    std::string normalized_start;
    std::string normalized_end;
    if (!campaign.starts_at.empty()) {
        normalized_start = utils::normalize_iso8601_utc(campaign.starts_at);
        if (normalized_start.empty()) {
            err = "starts_at must be an ISO-8601 date/time";
            return false;
        }
    }
    if (!campaign.ends_at.empty()) {
        normalized_end = utils::normalize_iso8601_utc(campaign.ends_at);
        if (normalized_end.empty()) {
            err = "ends_at must be an ISO-8601 date/time";
            return false;
        }
    }
    if (!normalized_start.empty() && !normalized_end.empty() && normalized_end <= normalized_start) {
        err = "ends_at must be later than starts_at";
        return false;
    }
    campaign.starts_at = normalized_start;
    campaign.ends_at = normalized_end;

    apply_schedule_and_status(campaign);
    return true;
}

bool apply_campaign_source(Campaign& campaign, const Site& site, const Placement& placement,
                           const User& user, const std::optional<AffiliateOffer>& offer,
                           std::string& err) {
    if (!is_valid_source_type(campaign.source_type)) {
        err = "invalid source_type";
        return false;
    }
    if (campaign.source_type == "external_network") {
        err = "external_network delivery is not enabled until a provider is approved";
        return false;
    }
    if (!is_admin_user(user) &&
        campaign.source_type != "house" && campaign.source_type != "affiliate") {
        err = "publishers may only create house or affiliate campaigns";
        return false;
    }
    if (std::find(placement.allowed_source_types.begin(), placement.allowed_source_types.end(),
                  campaign.source_type) == placement.allowed_source_types.end()) {
        err = "this placement does not allow the selected source_type";
        return false;
    }
    if (std::find(site.allowed_ad_sources.begin(), site.allowed_ad_sources.end(),
                  campaign.source_type) == site.allowed_ad_sources.end()) {
        err = "this site does not allow the selected source_type";
        return false;
    }

    if (campaign.source_type == "affiliate") {
        if (!offer) {
            err = "affiliate_offer_id must reference an existing offer for affiliate campaigns";
            return false;
        }
        if (site.publisher_id.empty() || offer->publisher_id != site.publisher_id) {
            err = "affiliate offers can only be used on sites owned by the same publisher";
            return false;
        }
        if (offer->status == "rejected" || offer->status == "archived" ||
            offer->validation_status == "rejected") {
            err = "this affiliate offer is not eligible for placement";
            return false;
        }
        std::string url_err;
        if (!validate_affiliate_url(offer->affiliate_url, url_err)) {
            err = "the affiliate offer has an invalid destination: " + url_err;
            return false;
        }
        campaign.affiliate_offer_id = offer->id;
        // The destination always comes from the stored offer. A client cannot
        // smuggle a different destination into an affiliate campaign.
        campaign.destination_url = offer->affiliate_url;
        if (campaign.format != "product_card") {
            err = "affiliate campaigns must use the product_card format";
            return false;
        }
        if (campaign.title.empty()) campaign.title = offer->product_title;
        if (campaign.body.empty()) campaign.body = offer->product_description;
        if (campaign.image_url.empty()) campaign.image_url = offer->product_image_url;
        if (campaign.alt_text.empty()) campaign.alt_text = offer->product_title;
        if (campaign.cta_label.empty()) campaign.cta_label = "View offer";
    } else {
        campaign.affiliate_offer_id.clear();
    }
    return true;
}

bool campaign_publish_ready(const Campaign& campaign, const Site& site,
                            const std::optional<AffiliateOffer>& offer, std::string& err) {
    if (site.status != "active" || site.verification_status != "verified" ||
        site.monetization_status != "enabled") {
        err = "site must be active, verified, and enabled for monetization";
        return false;
    }
    if (campaign.source_type == "affiliate") {
        if (!offer) {
            err = "affiliate offer not found";
            return false;
        }
        if (offer->status != "active") {
            err = "affiliate offer must be active before its campaign can be published";
            return false;
        }
        std::string url_err;
        if (!validate_affiliate_url(offer->affiliate_url, url_err)) {
            err = "affiliate offer destination is invalid: " + url_err;
            return false;
        }
    } else if (!utils::is_safe_url(campaign.destination_url)) {
        err = "campaign destination is not a safe URL";
        return false;
    }
    return true;
}

bool is_valid_email(const std::string& value) {
    if (value.empty() || value.size() > 254) return false;
    static const std::regex re(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    return std::regex_match(value, re);
}

bool is_valid_partner_inquiry_kind(const std::string& value) {
    return value == "publisher" || value == "client";
}

bool is_valid_partner_inquiry_status(const std::string& value) {
    return value == "new" || value == "reviewing" || value == "closed";
}

bool is_valid_publisher_status(const std::string& value) {
    return value == "pending" || value == "active" ||
           value == "suspended" || value == "rejected";
}

bool validate_offer(AffiliateOffer& offer, std::string& err) {
    if (!is_valid_provider(offer.provider)) {
        err = "provider must be shopee_affiliate or manual_affiliate";
        return false;
    }
    offer.offer_name = utils::trim_copy(offer.offer_name);
    if (offer.offer_name.empty() || too_long(offer.offer_name, 200)) {
        err = "offer_name is required (max 200 characters)";
        return false;
    }
    offer.product_title = utils::trim_copy(offer.product_title);
    if (offer.product_title.empty() || too_long(offer.product_title, 300)) {
        err = "product_title is required (max 300 characters)";
        return false;
    }
    if (too_long(offer.product_description, 2000)) {
        err = "product_description must be 2000 characters or fewer";
        return false;
    }
    if (too_long(offer.merchant_name, 120)) {
        err = "merchant_name must be 120 characters or fewer";
        return false;
    }
    if (too_long(offer.price_text, 50)) {
        err = "price_text must be 50 characters or fewer";
        return false;
    }
    if (!offer.currency.empty()) {
        if (offer.currency.size() != 3 ||
            !std::all_of(offer.currency.begin(), offer.currency.end(),
                         [](unsigned char c) { return std::isupper(c); })) {
            err = "currency must be a three-letter uppercase code (for example IDR)";
            return false;
        }
    }
    if (!utils::is_safe_optional_url(offer.product_image_url)) {
        err = "product_image_url must be a safe http:// or https:// URL or empty";
        return false;
    }
    if (!validate_affiliate_url(offer.affiliate_url, err)) {
        return false;
    }
    if (!offer.canonical_product_url.empty() &&
        !validate_affiliate_url(offer.canonical_product_url, err)) {
        err = "canonical_product_url: " + err;
        return false;
    }
    if (too_long(offer.disclosure_text, 500)) {
        err = "disclosure_text must be 500 characters or fewer";
        return false;
    }
    if (!is_valid_offer_status(offer.status)) {
        err = "invalid offer status";
        return false;
    }
    if (offer.status == "rejected" || offer.status == "archived" || offer.status == "expired") {
        err = "offers can only be created or edited as draft, active, or paused";
        return false;
    }
    if (!offer.availability_checked_at.empty()) {
        std::string normalized = utils::normalize_iso8601_utc(offer.availability_checked_at);
        if (normalized.empty()) {
            err = "availability_checked_at must be an ISO-8601 date/time";
            return false;
        }
        offer.availability_checked_at = normalized;
    }
    return true;
}

bool read_offer_body(const nlohmann::json& body, AffiliateOffer& offer, std::string& err) {
    return read_string(body, "provider", offer.provider, err) &&
           read_string(body, "offer_name", offer.offer_name, err) &&
           read_string(body, "product_title", offer.product_title, err) &&
           read_string(body, "product_image_url", offer.product_image_url, err) &&
           read_string(body, "product_description", offer.product_description, err) &&
           read_string(body, "merchant_name", offer.merchant_name, err) &&
           read_string(body, "affiliate_url", offer.affiliate_url, err) &&
           read_string(body, "canonical_product_url", offer.canonical_product_url, err) &&
           read_string(body, "price_text", offer.price_text, err) &&
           read_string(body, "currency", offer.currency, err) &&
           read_string(body, "availability_checked_at", offer.availability_checked_at, err) &&
           read_string(body, "disclosure_text", offer.disclosure_text, err) &&
           read_string(body, "status", offer.status, err);
}

bool validate_date_filter(const std::string& value, const std::string& label, httplib::Response& res) {
    if (value.empty()) return true;
    if (value.size() == 10) {
        std::time_t epoch = 0;
        if (utils::parse_iso8601_utc(value + "T00:00:00Z", epoch)) return true;
    } else if (utils::is_valid_iso8601_utc(value)) {
        return true;
    }
    json_error(res, 400, label + " must be YYYY-MM-DD or an ISO-8601 timestamp");
    return false;
}

std::string request_origin(const httplib::Request& req) {
    if (req.has_header("Origin")) {
        std::string origin = req.get_header_value("Origin");
        if (!origin.empty()) return origin;
    }
    if (req.has_header("Referer")) {
        return req.get_header_value("Referer");
    }
    return "";
}

std::string origin_only(const std::string& raw) {
    std::string value = utils::to_lower_copy(utils::trim_copy(raw));
    size_t scheme = value.find("://");
    if (scheme == std::string::npos) return "";
    size_t host_start = scheme + 3;
    size_t host_end = value.find_first_of("/?#", host_start);
    std::string origin = value.substr(0, host_end == std::string::npos ? value.size() : host_end);
    if (origin.size() > 255) origin = origin.substr(0, 255);
    return origin;
}

std::string minimized_referrer(const std::string& raw) {
    std::string value = utils::trim_copy(raw);
    if (value.empty()) return "";
    size_t cut = value.find_first_of("?#");
    if (cut != std::string::npos) value = value.substr(0, cut);
    if (value.size() > 300) value = value.substr(0, 300);
    return value;
}

std::string classify_user_agent(const std::string& ua) {
    std::string lower = utils::to_lower_copy(ua);
    if (lower.find("bot") != std::string::npos ||
        lower.find("crawler") != std::string::npos ||
        lower.find("spider") != std::string::npos ||
        lower.find("slurp") != std::string::npos ||
        lower.find("headless") != std::string::npos) {
        return "bot";
    }
    if (lower.find("mobile") != std::string::npos ||
        lower.find("android") != std::string::npos ||
        lower.find("iphone") != std::string::npos) {
        return "mobile";
    }
    return "desktop";
}

std::string csv_escape(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

void set_no_store(httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
}

void set_delivery_headers(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, If-None-Match");
    res.set_header("Access-Control-Expose-Headers", "ETag");
    res.set_header("Vary", "Origin");
}


std::vector<std::string> effective_domains(const Site& site) {
    if (!site.allowed_domains.empty()) return site.allowed_domains;
    return {site.primary_domain};
}

// ---------------------------------------------------------------------------
// Learning platform helpers
// ---------------------------------------------------------------------------

constexpr size_t LEARNING_MAX_MARKDOWN_BYTES = 120000;

bool is_learning_staff_user(const User& user) {
    return user.role == "instructor" || user.role == "editor" || user.role == "admin";
}

bool is_learning_editor_user(const User& user) {
    return user.role == "editor" || user.role == "admin";
}

bool can_manage_learning_course(const User& user, const LearningCourse& course) {
    if (is_learning_editor_user(user)) return true;
    return user.role == "instructor" && !course.author_id.empty() && course.author_id == user.id;
}

bool is_valid_learning_slug(const std::string& slug) {
    if (slug.size() < 2 || slug.size() > 120) return false;
    static const std::regex re(R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)");
    return std::regex_match(slug, re);
}

bool is_valid_learning_level(const std::string& level) {
    return level == "beginner" || level == "intermediate" || level == "advanced";
}

bool contains_unsafe_learning_markup(const std::string& value) {
    std::string lower = utils::to_lower_copy(value);
    static const std::vector<std::string> blocked = {
        "<script", "</script", "<iframe", "</iframe", "<object", "</object",
        "<embed", "</embed", "<style", "</style", "<link", "<meta",
        "javascript:", "vbscript:", "data:text/html", "data:application/",
        "file://", "onerror=", "onload=", "onclick="
    };
    for (const auto& needle : blocked) {
        if (lower.find(needle) != std::string::npos) return true;
    }
    for (unsigned char c : value) {
        if (c == 0 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) return true;
    }
    return false;
}

bool validate_learning_markdown(const std::string& value, size_t max_len,
                                const std::string& label, bool required,
                                std::string& err) {
    if (value.size() > max_len || value.size() > LEARNING_MAX_MARKDOWN_BYTES) {
        err = label + " is too long (max " + std::to_string(max_len) + " bytes)";
        return false;
    }
    if (required && utils::trim_copy(value).empty()) {
        err = label + " is required";
        return false;
    }
    if (contains_unsafe_learning_markup(value)) {
        err = label + " contains unsupported or unsafe HTML/URL content";
        return false;
    }
    return true;
}

bool validate_learning_plain_text(const std::string& value, size_t min_len, size_t max_len,
                                  const std::string& label, bool required, std::string& err) {
    std::string trimmed = utils::trim_copy(value);
    if (required && trimmed.size() < min_len) {
        err = label + " must be at least " + std::to_string(min_len) + " characters";
        return false;
    }
    if (value.size() > max_len) {
        err = label + " is too long (max " + std::to_string(max_len) + " characters)";
        return false;
    }
    if (contains_unsafe_learning_markup(value)) {
        err = label + " contains unsupported markup";
        return false;
    }
    return true;
}

bool validate_learning_video(const std::string& provider, const std::string& video_id,
                             std::string& err) {
    if (provider.empty() && video_id.empty()) return true;
    if (provider != "youtube" && provider != "vimeo") {
        err = "video_provider must be empty, youtube, or vimeo";
        return false;
    }
    if (video_id.empty()) {
        err = "video_id is required when video_provider is set";
        return false;
    }
    static const std::regex re(R"(^[A-Za-z0-9_-]{6,64}$)");
    if (!std::regex_match(video_id, re)) {
        err = "video_id is invalid";
        return false;
    }
    return true;
}

bool validate_learning_resource_url(const std::string& url, std::string& err) {
    std::string trimmed = utils::trim_copy(url);
    if (trimmed.size() > 2048 || trimmed.rfind("https://", 0) != 0) {
        err = "Resource URL must be an HTTPS URL";
        return false;
    }
    if (!utils::is_safe_url(trimmed)) {
        err = "Resource URL is not a safe HTTPS URL";
        return false;
    }
    return true;
}

bool validate_learning_course_fields(LearningCourse& course, std::string& err) {
    course.slug = utils::to_lower_copy(utils::trim_copy(course.slug));
    course.title = utils::trim_copy(course.title);
    course.summary = utils::trim_copy(course.summary);
    course.level = utils::to_lower_copy(utils::trim_copy(course.level));
    if (!is_valid_learning_slug(course.slug)) {
        err = "slug must be 2-120 lowercase letters, digits, or single hyphens";
        return false;
    }
    if (!validate_learning_plain_text(course.title, 3, 160, "title", true, err)) return false;
    if (!validate_learning_plain_text(course.summary, 10, 500, "summary", true, err)) return false;
    if (!validate_learning_markdown(course.description_md, 60000, "description_md", true, err)) return false;
    if (!is_valid_learning_level(course.level)) {
        err = "level must be beginner, intermediate, or advanced";
        return false;
    }
    if (course.duration_minutes < 1 || course.duration_minutes > 100000) {
        err = "duration_minutes must be between 1 and 100000";
        return false;
    }
    if (course.outcomes.size() > 20) {
        err = "outcomes has too many entries (max 20)";
        return false;
    }
    for (auto& outcome : course.outcomes) {
        outcome = utils::trim_copy(outcome);
        if (outcome.empty() || outcome.size() > 300 ||
            contains_unsafe_learning_markup(outcome)) {
            err = "each outcome must be 1-300 characters of plain text";
            return false;
        }
    }
    if (course.topic_id.empty()) {
        err = "topic_id is required";
        return false;
    }
    return true;
}

bool validate_learning_lesson_fields(const LearningLesson& lesson, std::string& err) {
    if (!is_valid_learning_slug(lesson.slug)) {
        err = "lesson slug must be 2-120 lowercase letters, digits, or single hyphens";
        return false;
    }
    if (!validate_learning_plain_text(lesson.title, 3, 160, "lesson title", true, err)) return false;
    if (!validate_learning_markdown(lesson.content_md, LEARNING_MAX_MARKDOWN_BYTES,
                                    "lesson content_md", false, err)) return false;
    if (lesson.estimated_minutes < 1 || lesson.estimated_minutes > 600) {
        err = "estimated_minutes must be between 1 and 600";
        return false;
    }
    if (lesson.sort_order < 0 || lesson.sort_order > 100000) {
        err = "sort_order must be between 0 and 100000";
        return false;
    }
    if (!validate_learning_video(lesson.video_provider, lesson.video_id, err)) return false;
    return true;
}

bool learning_course_ready_to_publish(Database& db, const std::string& course_id, std::string& err) {
    auto modules = db.list_learning_modules(course_id, false);
    auto lessons = db.list_learning_lessons(course_id, false);
    if (modules.empty()) {
        err = "Add at least one module before publishing";
        return false;
    }
    if (lessons.empty()) {
        err = "Add at least one active lesson before publishing";
        return false;
    }
    for (const auto& module : modules) {
        bool has_lesson = false;
        for (const auto& lesson : lessons) {
            if (lesson.module_id != module.id) continue;
            has_lesson = true;
            if (utils::trim_copy(lesson.content_md).empty()) {
                err = "Every active lesson must have content before publishing";
                return false;
            }
        }
        if (!has_lesson) {
            err = "Every active module must contain at least one active lesson";
            return false;
        }
    }
    return true;
}

nlohmann::json learning_course_public_json(const LearningCourse& course) {
    return {
        {"id", course.id},
        {"slug", course.slug},
        {"topic_id", course.topic_id},
        {"topic_slug", course.topic_slug},
        {"topic_title", course.topic_title},
        {"title", course.title},
        {"summary", course.summary},
        {"description_md", course.description_md},
        {"level", course.level},
        {"duration_minutes", course.duration_minutes},
        {"outcomes", course.outcomes},
        {"version", course.version},
        {"published_at", course.published_at},
        {"last_reviewed_at", course.last_reviewed_at},
        {"next_review_due_at", course.next_review_due_at},
        {"updated_at", course.updated_at}
    };
}

nlohmann::json learning_lesson_visible_json(const LearningLesson& lesson,
                                            bool full_content,
                                            bool completed,
                                            const LessonQuiz* quiz,
                                            bool include_answers) {
    bool visible = full_content || lesson.is_preview;
    nlohmann::json j = {
        {"id", lesson.id},
        {"course_id", lesson.course_id},
        {"module_id", lesson.module_id},
        {"slug", lesson.slug},
        {"title", lesson.title},
        {"estimated_minutes", lesson.estimated_minutes},
        {"sort_order", lesson.sort_order},
        {"is_preview", lesson.is_preview},
        {"is_locked", !visible},
        {"completed", completed},
        {"content_md", visible ? lesson.content_md : ""},
        {"video_provider", visible ? lesson.video_provider : ""},
        {"video_id", visible ? lesson.video_id : ""}
    };
    if (visible && quiz) j["quiz"] = quiz->to_json(include_answers);
    return j;
}

nlohmann::json learning_curriculum_json(Database& db,
                                        const std::string& course_id,
                                        bool full_content,
                                        bool include_quizzes,
                                        bool include_answers,
                                        const std::set<std::string>& completed_ids) {
    auto modules = db.list_learning_modules(course_id, false);
    auto lessons = db.list_learning_lessons(course_id, false);
    nlohmann::json result = nlohmann::json::object();
    nlohmann::json module_list = nlohmann::json::array();
    int total_lessons = 0;
    for (const auto& module : modules) {
        nlohmann::json m = {
            {"id", module.id},
            {"course_id", module.course_id},
            {"title", module.title},
            {"summary", module.summary},
            {"sort_order", module.sort_order}
        };
        nlohmann::json lesson_list = nlohmann::json::array();
        for (const auto& lesson : lessons) {
            if (lesson.module_id != module.id) continue;
            std::optional<LessonQuiz> quiz;
            if (include_quizzes && full_content) {
                quiz = db.get_learning_quiz_for_lesson(lesson.id, include_answers);
            }
            lesson_list.push_back(learning_lesson_visible_json(
                lesson, full_content, completed_ids.count(lesson.id) > 0,
                quiz ? &*quiz : nullptr, include_answers));
            ++total_lessons;
        }
        m["lessons"] = lesson_list;
        module_list.push_back(m);
    }
    result["modules"] = module_list;
    result["module_count"] = modules.size();
    result["lesson_count"] = total_lessons;
    return result;
}

bool learning_course_is_public(Database& db, const LearningCourse& course) {
    if (course.status != "published") return false;
    auto topic = db.get_learning_topic(course.topic_id);
    return topic && topic->status == "published";
}

nlohmann::json learning_authoring_course_json(Database& db, const LearningCourse& course) {
    auto modules = db.list_learning_modules(course.id, true);
    auto lessons = db.list_learning_lessons(course.id, true);
    auto resources = db.list_learning_resources(course.id);
    nlohmann::json module_list = nlohmann::json::array();
    for (const auto& module : modules) {
        nlohmann::json m = module.to_json();
        nlohmann::json lesson_list = nlohmann::json::array();
        for (const auto& lesson : lessons) {
            if (lesson.module_id != module.id) continue;
            nlohmann::json l = lesson.to_json();
            auto quiz = db.get_learning_quiz_for_lesson(lesson.id, true);
            if (quiz) l["quiz"] = quiz->to_json(true);
            else l["quiz"] = nullptr;
            lesson_list.push_back(l);
        }
        m["lessons"] = lesson_list;
        module_list.push_back(m);
    }
    nlohmann::json resource_list = nlohmann::json::array();
    for (const auto& resource : resources) resource_list.push_back(resource.to_json());
    return {
        {"course", course.to_json()},
        {"modules", module_list},
        {"resources", resource_list}
    };
}

std::string learning_public_base_url() {
    std::string base = env_or("PUBLIC_BASE_URL", "https://marketing.nevatal.id");
    base = utils::trim_copy(base);
    while (!base.empty() && base.back() == '/') base.pop_back();
    // Canonical SEO URLs must use HTTPS. An accidentally configured http://
    // origin falls back to the production default instead of emitting
    // inconsistent or downgraded canonicals, sitemap entries, and robots hints.
    const bool https = utils::to_lower_copy(base).rfind("https://", 0) == 0;
    if (!https || !utils::is_safe_url(base)) return "https://marketing.nevatal.id";
    return base;
}

std::string xml_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

nlohmann::json learning_enrollment_summary_json(Database& db, const Enrollment& enrollment) {
    nlohmann::json j = enrollment.to_json();
    auto course = db.get_learning_course(enrollment.course_id);
    if (course) {
        j["course"] = learning_course_public_json(*course);
    } else {
        j["course"] = nullptr;
    }
    return j;
}

} // namespace

ApiServer::ApiServer(std::shared_ptr<Database> db, const std::string& secret_key, bool trust_proxy)
    : db_(std::move(db)), secret_key_(secret_key), trust_proxy_(trust_proxy),
      etag_nonce_(utils::random_hex(8)) {}

std::string ApiServer::client_ip(const httplib::Request& req) const {
    if (trust_proxy_) {
        if (req.has_header("X-Forwarded-For")) {
            std::string forwarded = req.get_header_value("X-Forwarded-For");
            size_t comma = forwarded.find(',');
            std::string first = utils::trim_copy(forwarded.substr(0, comma));
            if (!first.empty() && first.size() <= 64) return first;
        }
        if (req.has_header("X-Real-IP")) {
            std::string real = utils::trim_copy(req.get_header_value("X-Real-IP"));
            if (!real.empty() && real.size() <= 64) return real;
        }
    }
    return req.remote_addr;
}

bool ApiServer::allow_request(const std::string& key, int limit, int window_seconds) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rate_mutex_);

    if (now - last_rate_prune_ > std::chrono::seconds(60)) {
        for (auto it = rate_buckets_.begin(); it != rate_buckets_.end();) {
            if (it->second.empty()) it = rate_buckets_.erase(it);
            else ++it;
        }
        last_rate_prune_ = now;
    }
    // Bound memory even under a distributed key-rotation flood.
    if (rate_buckets_.size() > 100000) rate_buckets_.clear();

    auto& bucket = rate_buckets_[key];
    const auto window = std::chrono::seconds(window_seconds);
    while (!bucket.empty() && now - bucket.front() > window) bucket.pop_front();
    if (static_cast<int>(bucket.size()) >= limit) return false;
    bucket.push_back(now);
    return true;
}

std::string ApiServer::create_click_token(const std::string& campaign_id) {
    nlohmann::json payload = {
        {"c", campaign_id},
        {"t", static_cast<long long>(utils::utc_now_epoch())},
        {"n", utils::random_hex(8)}
    };
    std::string data = payload.dump();
    std::string sig = utils::hmac_sha256_hex(secret_key_, data);
    return base64_url_encode(data) + "." + sig;
}

std::optional<std::string> ApiServer::verify_click_token(const std::string& token, bool& expired) {
    expired = false;
    size_t dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) return std::nullopt;

    std::string encoded_data = token.substr(0, dot);
    std::string sig = token.substr(dot + 1);
    std::string data = base64_url_decode(encoded_data);
    std::string expected_sig = utils::hmac_sha256_hex(secret_key_, data);
    if (sig.size() != expected_sig.size() ||
        CRYPTO_memcmp(sig.data(), expected_sig.data(), sig.size()) != 0) {
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(data);
        std::string campaign_id = j.value("c", "");
        long long issued_at = j.value("t", 0LL);
        if (campaign_id.empty() || issued_at <= 0) return std::nullopt;
        long long now = static_cast<long long>(utils::utc_now_epoch());
        if (issued_at > now + 300) return std::nullopt;
        if (now - issued_at > CLICK_TOKEN_TTL_SECONDS) {
            expired = true;
        }
        return campaign_id;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<User> ApiServer::authenticate_any(const httplib::Request& req, httplib::Response& res) {
    std::string token;
    if (req.has_header("Authorization")) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.rfind("Bearer ", 0) == 0) token = auth.substr(7);
    }

    if (token.empty()) {
        set_no_store(res);
        json_error(res, 401, "Unauthorized: missing bearer token");
        return std::nullopt;
    }

    auto user = db_->get_user_by_token(token);
    if (!user) {
        set_no_store(res);
        json_error(res, 401, "Unauthorized: invalid or expired session");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate_any(req, res);
    if (!user) return std::nullopt;
    // Learners and learning staff must never fall through to the inventory,
    // analytics, tenancy, or audit APIs, even though those handlers use this
    // helper for their authorization check.
    if (user->role == "learner" || user->role == "instructor" || user->role == "editor") {
        json_error(res, 403, "Forbidden: this API is for back-office accounts");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate_learning(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate_any(req, res);
    if (!user) return std::nullopt;
    if (user->role != "learner" && user->role != "instructor" &&
        user->role != "editor" && user->role != "admin") {
        json_error(res, 403, "Forbidden: a learning account is required");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate_learning_staff(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate_learning(req, res);
    if (!user) return std::nullopt;
    if (user->role != "instructor" && user->role != "editor" && user->role != "admin") {
        json_error(res, 403, "Forbidden: authoring role required");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate_learning_editor(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate_learning(req, res);
    if (!user) return std::nullopt;
    if (user->role != "editor" && user->role != "admin") {
        json_error(res, 403, "Forbidden: editor or administrator role required");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate_admin(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate(req, res);
    if (!user) return std::nullopt;
    if (user->role != "admin") {
        json_error(res, 403, "Forbidden: administrator role required");
        return std::nullopt;
    }
    return user;
}

std::optional<User> ApiServer::authenticate_writer(const httplib::Request& req, httplib::Response& res) {
    auto user = authenticate(req, res);
    if (!user) return std::nullopt;
    if (!is_writer_user(*user)) {
        json_error(res, 403, "Forbidden: administrator or publisher role required");
        return std::nullopt;
    }
    if (user->role == "publisher" && user->publisher_id.empty()) {
        json_error(res, 403, "Forbidden: publisher account is not linked to a publisher");
        return std::nullopt;
    }
    return user;
}

void ApiServer::register_routes(httplib::Server& svr, const std::string& sdk_path) {
    // Global security headers; these do not replace TLS or proxy-level headers.
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("X-Content-Type-Options", "nosniff");
        // A route may opt into a stricter policy (for example no-referrer on
        // click redirects); do not append a conflicting duplicate header.
        if (res.headers.find("Referrer-Policy") == res.headers.end()) {
            res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
        }
    });

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            res.set_content(nlohmann::json({{"error", "Request failed"}, {"status", res.status}}).dump(),
                            "application/json");
        }
    });

    // CORS preflight for public browser delivery endpoints.
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, If-None-Match");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status = 204;
    });

    // ==========================================
    // Health & readiness
    // ==========================================
    svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(nlohmann::json({
            {"status", "ok"},
            {"service", "adcontrol-api"},
            {"timestamp", utils::get_current_iso8601()}
        }).dump(), "application/json");
    });

    svr.Get("/readyz", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        if (!db_->ping()) {
            json_error(res, 503, "Database is not ready");
            return;
        }
        res.set_content(nlohmann::json({
            {"status", "ready"},
            {"timestamp", utils::get_current_iso8601()}
        }).dump(), "application/json");
    });

    // ==========================================
    // Public delivery
    // ==========================================
    svr.Get(R"(/delivery/v1/sites/([^/]+)/config)", [this](const httplib::Request& req, httplib::Response& res) {
        set_delivery_headers(res);

        const std::string site_key = req.matches[1];
        const std::string ip = client_ip(req);
        if (!allow_request("config:" + ip + ":" + site_key, 240, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }

        auto site_opt = db_->get_site_by_public_key(site_key);
        if (!site_opt || site_opt->status != "active") {
            res.set_header("Cache-Control", "no-store");
            json_error(res, 404, "Site not found or disabled");
            return;
        }
        // Publisher inventory is dark until the domain is verified and the
        // site is approved for monetization. Platform-owned sites are seeded as
        // verified/enabled and keep their Phase 1 behavior.
        if (site_opt->verification_status != "verified" ||
            site_opt->monetization_status != "enabled" ||
            !publisher_delivery_enabled(*db_, *site_opt)) {
            res.set_header("Cache-Control", "no-store");
            json_error(res, 404, "Site is not yet verified for monetization");
            return;
        }
        const auto& site = *site_opt;

        const auto domains = effective_domains(site);
        const std::string origin = request_origin(req);
        if (!origin.empty() && !utils::is_domain_allowed(origin, domains)) {
            res.set_header("Cache-Control", "no-store");
            json_error(res, 403, "Domain origin not authorized for this site key");
            return;
        }

        const std::string now_iso = utils::get_current_iso8601();
        db_->advance_campaign_states(now_iso);
        auto eligible = db_->get_eligible_campaigns_for_delivery(site.id, now_iso);

        struct ClickTarget {
            std::string placement_key;
            std::string campaign_id;
            std::string path_prefix;
        };

        nlohmann::json placements_map = nlohmann::json::object();
        std::vector<ClickTarget> click_targets;
        for (const auto& pair : eligible) {
            const auto& placement = pair.first;
            const auto& campaign = pair.second;
            if (!source_allowed_for_site(site, campaign.source_type)) continue;

            std::optional<AffiliateOffer> offer;
            if (campaign.source_type == "affiliate") {
                if (campaign.affiliate_offer_id.empty()) continue;
                offer = db_->get_affiliate_offer(campaign.affiliate_offer_id);
                if (!offer || offer->status != "active" ||
                    offer->validation_status == "rejected" ||
                    offer->publisher_id != site.publisher_id ||
                    site.publisher_id.empty()) {
                    continue;
                }
                std::string reason;
                if (!validate_affiliate_url(offer->affiliate_url, reason)) continue;
            } else if (!utils::is_safe_url(campaign.destination_url)) {
                continue;
            }

            std::string image_url = utils::is_safe_optional_url(campaign.image_url) ? campaign.image_url : "";
            std::string title = campaign.title;
            std::string body = campaign.body;
            std::string image = image_url;
            std::string alt = campaign.alt_text;
            std::string merchant;
            std::string price_text;
            std::string currency;
            if (offer) {
                if (title.empty()) title = offer->product_title;
                if (body.empty()) body = offer->product_description;
                if (image.empty() && utils::is_safe_optional_url(offer->product_image_url)) {
                    image = offer->product_image_url;
                }
                if (alt.empty()) alt = offer->product_title;
                merchant = offer->merchant_name;
                price_text = offer->price_text;
                currency = offer->currency;
            }

            nlohmann::json item = {
                {"campaign_id", campaign.id},
                {"placement_id", placement.id},
                {"placement_key", placement.placement_key},
                {"format", campaign.format},
                {"placement_format", placement.format},
                {"width", placement.width},
                {"height", placement.height},
                {"fallback_behavior", placement.fallback_behavior},
                {"title", title},
                {"body", body},
                {"image_url", image},
                {"alt_text", alt},
                {"cta_label", campaign.cta_label},
                {"source_type", campaign.source_type},
                {"affiliate", static_cast<bool>(offer)},
                {"disclosure_text", effective_disclosure_text(campaign,
                    offer ? &*offer : nullptr, site, placement)},
                {"merchant_name", merchant},
                {"price_text", price_text},
                {"currency", currency}
            };
            placements_map[placement.placement_key] = item;
            click_targets.push_back({placement.placement_key, campaign.id,
                                     offer ? "/r/" : "/click/"});
        }

        // Active placements without an eligible campaign still need to reach
        // the SDK so it can honor the configured fallback behavior (hide vs.
        // reserve blank space). These entries contain no destination or click
        // URL and cannot be rendered as an ad.
        for (const auto& placement : db_->list_placements(site.id)) {
            if (placement.status != "active" ||
                placements_map.contains(placement.placement_key)) {
                continue;
            }
            placements_map[placement.placement_key] = {
                {"campaign_id", ""},
                {"placement_id", placement.id},
                {"placement_key", placement.placement_key},
                {"format", placement.format},
                {"placement_format", placement.format},
                {"width", placement.width},
                {"height", placement.height},
                {"fallback_behavior", placement.fallback_behavior},
                {"title", ""},
                {"body", ""},
                {"image_url", ""},
                {"alt_text", ""},
                {"cta_label", ""},
                {"source_type", ""},
                {"affiliate", false},
                {"disclosure_text", ""},
                {"merchant_name", ""},
                {"price_text", ""},
                {"currency", ""}
            };
        }

        nlohmann::json config = {
            {"site_key", site.public_site_key},
            {"site_name", site.name},
            {"config_version", site.config_version},
            {"refresh_interval_seconds", 30},
            {"max_stale_seconds", 300},
            {"placements", placements_map}
        };

        // The ETag is derived from the stable configuration plus a rotating
        // issuance window. Signed click tokens stay out of the hash, so a
        // conditional revalidation can correctly return 304; the hourly window
        // still forces a fresh payload (and fresh 24h click tokens) for pages
        // that remain open across token-expiry boundaries. A per-process nonce
        // invalidates conditional caches after a restart, including a restart
        // that rotates SECRET_KEY and therefore invalidates every old token.
        const long long click_issuance_window =
            static_cast<long long>(utils::utc_now_epoch()) / 3600;
        const std::string json_str = config.dump();
        const std::string etag = "W/\"site_" + site.id + "_v" +
            std::to_string(site.config_version) + "_" +
            utils::sha256_hex(json_str + "|" + std::to_string(click_issuance_window)).substr(0, 12) +
            "_n" + etag_nonce_ + "\"";
        res.set_header("ETag", etag);

        if (req.has_header("If-None-Match") && req.get_header_value("If-None-Match") == etag) {
            res.status = 304;
            res.set_header("Cache-Control", "public, max-age=30");
            return;
        }

        for (const auto& target : click_targets) {
            config["placements"][target.placement_key]["click_url"] =
                target.path_prefix + create_click_token(target.campaign_id);
        }

        res.set_header("Cache-Control", "public, max-age=30, stale-while-revalidate=30");
        res.set_content(config.dump(), "application/json");
    });

    svr.Post("/delivery/v1/events", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_header("Vary", "Origin");
        set_no_store(res);

        if (req.body.size() > 8192) {
            json_error(res, 413, "Event payload too large");
            return;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            json_error(res, 400, "Invalid JSON payload");
            return;
        }
        if (!body.is_object()) {
            json_error(res, 400, "JSON body must be an object");
            return;
        }

        std::string site_key;
        std::string event_type;
        std::string event_id;
        std::string placement_key;
        std::string campaign_id;
        std::string page_origin_body;
        std::string referrer_body;
        try {
            site_key = body.value("site_key", "");
            event_type = body.value("event_type", "");
            event_id = body.value("event_id", "");
            placement_key = body.value("placement_key", "");
            campaign_id = body.value("campaign_id", "");
            page_origin_body = body.value("page_origin", "");
            referrer_body = body.value("referrer", "");
        } catch (...) {
            json_error(res, 400, "Invalid field type");
            return;
        }

        // Clicks are recorded exclusively by the signed /click/{token} redirect.
        // Rejecting client-submitted click beacons prevents double counting and
        // removes an easy way to inflate click metrics without a real redirect.
        if (site_key.empty() || event_type != "impression" ||
            placement_key.empty() || campaign_id.empty()) {
            json_error(res, 400,
                       "site_key, impression event_type, placement_key, and campaign_id are required; "
                       "clicks are recorded by the signed redirect");
            return;
        }

        static const std::regex event_id_re(R"(^[A-Za-z0-9._:-]{8,128}$)");
        if (!std::regex_match(event_id, event_id_re)) {
            json_error(res, 400, "event_id is required (8-128 safe characters)");
            return;
        }

        const std::string ip = client_ip(req);
        if (!allow_request("events:" + ip + ":" + site_key, 600, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }

        auto site_opt = db_->get_site_by_public_key(site_key);
        if (!site_opt || site_opt->status != "active" ||
            site_opt->verification_status != "verified" ||
            site_opt->monetization_status != "enabled" ||
            !publisher_delivery_enabled(*db_, *site_opt)) {
            json_error(res, 404, "Active site not found");
            return;
        }

        const auto domains = effective_domains(*site_opt);
        const std::string header_origin = request_origin(req);
        const std::string reported_origin = header_origin.empty() ? page_origin_body : header_origin;
        if (!reported_origin.empty() && !utils::is_domain_allowed(reported_origin, domains)) {
            json_error(res, 403, "Domain origin not authorized for this site key");
            return;
        }

        auto placement_opt = db_->get_placement_by_site_and_key(site_opt->id, placement_key);
        if (!placement_opt) {
            json_error(res, 400, "Unknown placement for this site");
            return;
        }

        auto campaign_opt = db_->get_campaign(campaign_id);
        if (!campaign_opt || campaign_opt->site_id != site_opt->id ||
            campaign_opt->placement_id != placement_opt->id ||
            !source_allowed_for_site(*site_opt, campaign_opt->source_type) ||
            std::find(placement_opt->allowed_source_types.begin(),
                      placement_opt->allowed_source_types.end(),
                      campaign_opt->source_type) == placement_opt->allowed_source_types.end()) {
            json_error(res, 400, "Campaign does not belong to this site and placement");
            return;
        }

        AdEvent ev;
        ev.id = utils::generate_uuid();
        ev.site_id = site_opt->id;
        ev.placement_id = placement_opt->id;
        ev.campaign_id = campaign_opt->id;
        ev.source_type = campaign_opt->source_type;
        ev.affiliate_offer_id = campaign_opt->affiliate_offer_id;
        ev.event_type = event_type;
        ev.event_id = event_id;
        ev.occurred_at = utils::get_current_iso8601();
        ev.page_origin = origin_only(reported_origin);
        ev.referrer = minimized_referrer(referrer_body.empty()
                                             ? (req.has_header("Referer") ? req.get_header_value("Referer") : "")
                                             : referrer_body);
        ev.user_agent_category = classify_user_agent(
            req.has_header("User-Agent") ? req.get_header_value("User-Agent") : "");
        ev.ip_hash = utils::hmac_sha256_hex(secret_key_, ip).substr(0, 16);

        const bool inserted = db_->record_event(ev);
        res.set_content(nlohmann::json({
            {"status", inserted ? "recorded" : "deduplicated"},
            {"event_id", ev.event_id}
        }).dump(), "application/json");
    });

    // Both routes are served by one handler. /click/ is the Phase 1 campaign
    // path; /r/ is the Phase 2 first-party affiliate redirect. The destination
    // is always resolved from server-side campaign data keyed by a signed
    // token, never from a query-string URL.
    auto redirect_handler = [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("X-Robots-Tag", "noindex, nofollow");

        const std::string ip = client_ip(req);
        if (!allow_request("click:" + ip, 240, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }

        const std::string token = req.matches[1];
        bool expired = false;
        auto campaign_id = verify_click_token(token, expired);
        if (!campaign_id) {
            res.status = 400;
            res.set_content("This advertisement link is invalid.", "text/plain; charset=utf-8");
            return;
        }
        if (expired) {
            res.status = 410;
            res.set_content("This advertisement link has expired. Reload the page to continue.", "text/plain; charset=utf-8");
            return;
        }

        const std::string now_iso = utils::get_current_iso8601();
        db_->advance_campaign_states(now_iso);

        auto campaign = db_->get_campaign(*campaign_id);
        if (!campaign) {
            res.status = 410;
            res.set_content("This advertisement is no longer available.", "text/plain; charset=utf-8");
            return;
        }
        auto site = db_->get_site(campaign->site_id);
        auto placement = db_->get_placement(campaign->placement_id);
        const bool site_ready = site && site->status == "active" &&
            site->verification_status == "verified" &&
            site->monetization_status == "enabled" &&
            publisher_delivery_enabled(*db_, *site);
        const bool deliverable = site_ready && placement &&
            placement->status == "active" &&
            std::find(placement->allowed_source_types.begin(),
                      placement->allowed_source_types.end(),
                      campaign->source_type) != placement->allowed_source_types.end() &&
            source_allowed_for_site(*site, campaign->source_type) &&
            (campaign->status == "active" || campaign->status == "scheduled") &&
            utils::is_campaign_active_schedule(campaign->starts_at, campaign->ends_at, now_iso);
        if (!deliverable) {
            res.status = 410;
            res.set_content("This advertisement is no longer available.", "text/plain; charset=utf-8");
            return;
        }

        std::string redirect_to;
        if (campaign->source_type == "affiliate") {
            if (campaign->affiliate_offer_id.empty() || site->publisher_id.empty()) {
                res.status = 410;
                res.set_content("This advertisement is no longer available.", "text/plain; charset=utf-8");
                return;
            }
            auto offer = db_->get_affiliate_offer(campaign->affiliate_offer_id);
            std::string reason;
            if (!offer || offer->status != "active" ||
                offer->validation_status == "rejected" ||
                offer->publisher_id != site->publisher_id ||
                !validate_affiliate_url(offer->affiliate_url, reason)) {
                res.status = 410;
                res.set_content("This advertisement is no longer available.", "text/plain; charset=utf-8");
                return;
            }
            // Exact stored affiliate URL: no tracking parameters are injected,
            // removed, or reordered by AdControl.
            redirect_to = offer->affiliate_url;
        } else {
            if (!utils::is_safe_url(campaign->destination_url)) {
                res.status = 410;
                res.set_content("This advertisement is no longer available.", "text/plain; charset=utf-8");
                return;
            }
            redirect_to = campaign->destination_url;
        }

        const std::string origin = request_origin(req);
        if (!origin.empty() && !utils::is_domain_allowed(origin, effective_domains(*site))) {
            res.status = 403;
            res.set_content("This click origin is not authorized.", "text/plain; charset=utf-8");
            return;
        }

        AdEvent ev;
        ev.id = utils::generate_uuid();
        ev.site_id = site->id;
        ev.placement_id = placement->id;
        ev.campaign_id = campaign->id;
        ev.source_type = campaign->source_type;
        ev.affiliate_offer_id = campaign->affiliate_offer_id;
        ev.event_type = "click";
        ev.event_id = utils::generate_uuid();
        ev.occurred_at = now_iso;
        ev.page_origin = origin_only(origin);
        ev.referrer = minimized_referrer(origin);
        ev.user_agent_category = classify_user_agent(
            req.has_header("User-Agent") ? req.get_header_value("User-Agent") : "");
        ev.ip_hash = utils::hmac_sha256_hex(secret_key_, ip).substr(0, 16);
        db_->record_event(ev);

        if (campaign->source_type == "affiliate") {
            AdEvent outbound = ev;
            outbound.id = utils::generate_uuid();
            outbound.event_type = "outbound_redirect";
            outbound.event_id = utils::generate_uuid();
            db_->record_event(outbound);
        }

        res.set_header("Location", redirect_to);
        res.status = 302;
    };
    svr.Get(R"(/click/([^/]+))", redirect_handler);
    svr.Get(R"(/r/([^/]+))", redirect_handler);

    svr.Get("/sdk/v1/adcontrol.js", [sdk_path](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/javascript; charset=utf-8");
        res.set_header("Cache-Control", "public, max-age=300");

        if (!sdk_path.empty()) {
            std::ifstream file(sdk_path, std::ios::binary);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                res.set_content(ss.str(), "application/javascript; charset=utf-8");
                return;
            }
        }
        res.set_header("Cache-Control", "no-store");
        json_error(res, 404, "SDK asset not found");
    });

    // Stylesheet loaded by the SDK. Serving it as an external asset lets host
    // pages allow the SDK with `style-src https://<adcontrol-host>` instead of
    // enabling inline styles in their CSP.
    std::string sdk_css_path = "adcontrol.css";
    if (!sdk_path.empty()) {
        const size_t slash = sdk_path.find_last_of("/\\");
        if (slash != std::string::npos) {
            sdk_css_path = sdk_path.substr(0, slash + 1) + "adcontrol.css";
        }
    }
    svr.Get("/sdk/v1/adcontrol.css", [sdk_css_path](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "text/css; charset=utf-8");
        res.set_header("Cache-Control", "public, max-age=300");
        res.set_header("X-Content-Type-Options", "nosniff");

        std::ifstream file(sdk_css_path, std::ios::binary);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            res.set_content(ss.str(), "text/css; charset=utf-8");
            return;
        }
        res.set_header("Cache-Control", "no-store");
        json_error(res, 404, "SDK stylesheet not found");
    });

    // ==========================================
    // Authentication
    // ==========================================
    svr.Post("/api/v1/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        const std::string ip = client_ip(req);
        if (!allow_request("login:" + ip, 10, 300)) {
            res.set_header("Retry-After", "300");
            json_error(res, 429, "Too many login attempts. Try again later.");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        std::string email;
        std::string password;
        try {
            email = utils::to_lower_copy(utils::trim_copy(body.value("email", "")));
            password = body.value("password", "");
        } catch (...) {
            json_error(res, 400, "Invalid email or password type");
            return;
        }
        if (email.empty() || password.empty()) {
            json_error(res, 400, "email and password are required");
            return;
        }

        auto user = db_->get_user_by_email(email);
        if (!user) {
            // Constant-ish work for unknown accounts to reduce user enumeration.
            utils::hash_password(password, "00000000000000000000000000000000");
            json_error(res, 401, "Invalid email or password");
            return;
        }
        if (!utils::verify_password(password, user->salt, user->password_hash)) {
            json_error(res, 401, "Invalid email or password");
            return;
        }

        const std::string token = utils::random_hex(32);
        const std::string expires_at = utils::iso8601_from_epoch(utils::utc_now_epoch() + 30 * 86400);
        if (!db_->create_session(token, user->id, expires_at)) {
            json_error(res, 500, "Failed to create session");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "LOGIN";
        al.entity_type = "auth";
        al.entity_id = user->id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.set_content(nlohmann::json({
            {"token", token},
            {"expires_at", expires_at},
            {"user", user->to_json()}
        }).dump(), "application/json");
    });

    svr.Post("/api/v1/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        std::string auth = req.has_header("Authorization") ? req.get_header_value("Authorization") : "";
        if (auth.rfind("Bearer ", 0) == 0) {
            db_->delete_session(auth.substr(7));
        }
        res.set_content(nlohmann::json({{"status", "logged_out"}}).dump(), "application/json");
    });

    svr.Get("/api/v1/auth/me", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_any(req, res);
        if (!user) return;
        res.set_content(nlohmann::json({{"user", user->to_json()}}).dump(), "application/json");
    });

    svr.Post("/api/v1/auth/change-password", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_any(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        std::string current_password;
        std::string new_password;
        try {
            current_password = body.value("current_password", "");
            new_password = body.value("new_password", "");
        } catch (...) {
            json_error(res, 400, "Invalid password field type");
            return;
        }

        if (!utils::verify_password(current_password, user->salt, user->password_hash)) {
            json_error(res, 401, "Current password is incorrect");
            return;
        }
        if (new_password.size() < 12 || new_password.size() > 128) {
            json_error(res, 400, "New password must be between 12 and 128 characters");
            return;
        }
        if (new_password == current_password) {
            json_error(res, 400, "New password must be different from the current password");
            return;
        }

        const std::string salt = utils::random_hex(16);
        const std::string password_hash = utils::hash_password(new_password, salt);
        if (!db_->update_user_password(user->id, password_hash, salt)) {
            json_error(res, 500, "Failed to update password");
            return;
        }
        // Invalidate every session, including the one used for this request.
        db_->delete_user_sessions(user->id);

        AuditLog al;
        al.actor = user->email;
        al.action = "PASSWORD_CHANGED";
        al.entity_type = "auth";
        al.entity_id = user->id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.set_content(nlohmann::json({
            {"status", "password_changed"},
            {"message", "All sessions were invalidated. Sign in again."}
        }).dump(), "application/json");
    });

    // ==========================================
    // Settings (read-only operational configuration)
    // ==========================================
    svr.Get("/api/v1/settings", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        res.set_content(nlohmann::json({
            {"app_name", "AdControl"},
            {"environment", env_or("ADCONTROL_ENV", "development")},
            {"timezone", "UTC"},
            {"sdk_version", SDK_VERSION},
            {"sdk_url", "/sdk/v1/adcontrol.js"},
            {"config_cache_seconds", env_int_or("CONFIG_CACHE_SECONDS", 30, 5, 300)},
            {"max_stale_seconds", env_int_or("MAX_STALE_SECONDS", 300, 30, 3600)},
            {"event_retention_days", env_int_or("EVENT_RETENTION_DAYS", 180, 1, 3650)},
            {"click_token_ttl_seconds", CLICK_TOKEN_TTL_SECONDS},
            {"delivery_rate_limit_per_minute", 240},
            {"event_rate_limit_per_minute", 600}
        }).dump(), "application/json");
    });

    // ==========================================
    // Email marketing / SMTP (administrator only)
    // ==========================================
    svr.Get("/api/v1/email/settings", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;

        EmailSettings settings;
        if (auto stored = db_->get_email_settings()) settings = *stored;
        res.set_content(nlohmann::json({{"settings", settings.to_json()}}).dump(),
                        "application/json");
    });

    svr.Put("/api/v1/email/settings", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        EmailSettings settings;
        if (auto stored = db_->get_email_settings()) settings = *stored;

        std::string error;
        if (body.contains("enabled")) {
            if (!body["enabled"].is_boolean()) {
                json_error(res, 400, "enabled must be a boolean");
                return;
            }
            settings.enabled = body["enabled"].get<bool>();
        }
        if (!read_string(body, "host", settings.host, error) ||
            !read_string(body, "encryption", settings.encryption, error) ||
            !read_string(body, "username", settings.username, error) ||
            !read_string(body, "from_email", settings.from_email, error) ||
            !read_string(body, "from_name", settings.from_name, error) ||
            !read_string(body, "reply_to", settings.reply_to, error) ||
            !read_int(body, "port", settings.port, error)) {
            json_error(res, 400, error);
            return;
        }

        settings.host = utils::trim_copy(settings.host);
        settings.encryption = utils::to_lower_copy(utils::trim_copy(settings.encryption));
        settings.username = utils::trim_copy(settings.username);
        settings.from_email = utils::trim_copy(settings.from_email);
        settings.from_name = utils::trim_copy(settings.from_name);
        settings.reply_to = utils::trim_copy(settings.reply_to);

        bool password_changed = false;
        bool password_cleared = false;
        if (body.contains("password")) {
            const auto& value = body["password"];
            std::string plain;
            if (value.is_null()) {
                plain.clear();
            } else if (value.is_string()) {
                plain = value.get<std::string>();
            } else {
                json_error(res, 400, "password must be a string or null");
                return;
            }
            if (plain.empty()) {
                password_cleared = !settings.password_cipher.empty();
                settings.password_cipher.clear();
            } else {
                if (plain.size() > 512) {
                    json_error(res, 400, "SMTP password is too long");
                    return;
                }
                settings.password_cipher = utils::encrypt_secret(plain, secret_key_);
                if (settings.password_cipher.empty()) {
                    json_error(res, 500, "Could not encrypt the SMTP password");
                    return;
                }
                password_changed = true;
            }
        }

        if (!settings.username.empty() && !settings.password_cipher.empty()) {
            // Keep the "username without password" case allowed (some relays use
            // IP allowlists), but reject a password that can no longer be read.
            if (!utils::decrypt_secret(settings.password_cipher, secret_key_)) {
                json_error(res, 400, "Stored SMTP password could not be decrypted. Re-enter it.");
                return;
            }
        }

        if (settings.enabled || !settings.host.empty() || !settings.from_email.empty()) {
            error = smtp::validate_settings(smtp::Settings{
                settings.enabled, settings.host, settings.port, settings.encryption,
                settings.username, "", settings.from_email, settings.from_name,
                settings.reply_to});
            if (!error.empty()) {
                json_error(res, 400, error);
                return;
            }
            if (settings.enabled && !settings.username.empty() && settings.password_cipher.empty()) {
                json_error(res, 400, "Enter an SMTP password for the configured username");
                return;
            }
            if (settings.enabled && settings.username.empty() && !settings.password_cipher.empty()) {
                json_error(res, 400, "Enter an SMTP username or remove the stored password");
                return;
            }
        }

        settings.updated_by = user->email;
        if (!db_->save_email_settings(settings)) {
            json_error(res, 500, "Failed to save SMTP settings");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_EMAIL_SETTINGS";
        al.entity_type = "email_settings";
        al.entity_id = "default";
        al.details = nlohmann::json({
            {"enabled", settings.enabled},
            {"host", settings.host},
            {"port", settings.port},
            {"encryption", settings.encryption},
            {"password_changed", password_changed},
            {"password_cleared", password_cleared}
        }).dump();
        db_->record_audit_log(al);

        auto saved = db_->get_email_settings();
        res.set_content(nlohmann::json({{"settings", saved ? saved->to_json() : settings.to_json()}}).dump(),
                        "application/json");
    });

    svr.Post("/api/v1/email/test", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;

        if (!allow_request("email-test:" + user->id, 5, 60)) {
            json_error(res, 429, "Too many test emails. Wait a minute and try again.");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        std::string to;
        std::string error;
        if (!read_string(body, "to", to, error) || !smtp::is_valid_email(utils::trim_copy(to))) {
            json_error(res, 400, "A valid recipient email address is required");
            return;
        }
        to = utils::trim_copy(to);

        auto stored = db_->get_email_settings();
        if (!stored || stored->host.empty() || stored->from_email.empty()) {
            json_error(res, 400, "SMTP is not configured yet. Save the server and sender details first.");
            return;
        }

        std::string password;
        if (!stored->password_cipher.empty()) {
            auto decrypted = utils::decrypt_secret(stored->password_cipher, secret_key_);
            if (!decrypted) {
                json_error(res, 400,
                    "The stored SMTP password could not be decrypted. Re-enter it and save again.");
                return;
            }
            password = *decrypted;
        }
        if (!stored->username.empty() && password.empty()) {
            json_error(res, 400, "Enter an SMTP password before sending a test email.");
            return;
        }
        if (stored->username.empty() && !password.empty()) {
            json_error(res, 400, "Enter an SMTP username before sending a test email.");
            return;
        }

        smtp::Settings transport;
        transport.enabled = stored->enabled;
        transport.host = stored->host;
        transport.port = stored->port;
        transport.encryption = stored->encryption;
        transport.username = stored->username;
        transport.password = password;
        transport.from_email = stored->from_email;
        transport.from_name = stored->from_name;
        transport.reply_to = stored->reply_to;

        const std::string html =
            "<p>This is a test message from AdControl.</p>"
            "<p>If you received it, the SMTP configuration is working.</p>";
        auto sent = smtp::send(transport, to, "AdControl SMTP test", html);

        const std::string recipient_domain = to.substr(to.rfind('@') + 1);
        AuditLog al;
        al.actor = user->email;
        al.action = "SEND_TEST_EMAIL";
        al.entity_type = "email_settings";
        al.entity_id = "default";
        al.details = nlohmann::json({{"ok", sent.ok}, {"recipient_domain", recipient_domain}}).dump();
        db_->record_audit_log(al);

        if (!sent.ok) {
            std::cerr << "[SMTP] Test send failed: " << sent.error << std::endl;
            json_error(res, 502, "Test email failed: " + sent.error);
            return;
        }

        res.set_content(nlohmann::json({
            {"status", "sent"},
            {"message", "Test email sent to " + to + "."}
        }).dump(), "application/json");
    });

    // Authenticated preview data for the dashboard sandbox. This deliberately
    // bypasses the Origin allowlist (there is no host page) but still requires an
    // administrator session and only returns currently deliverable creatives.
    svr.Get(R"(/api/v1/sandbox/sites/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        const std::string site_id = req.matches[1];
        auto site = db_->get_site(site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }

        const std::string now_iso = utils::get_current_iso8601();
        db_->advance_campaign_states(now_iso);
        auto eligible = db_->get_eligible_campaigns_for_delivery(site->id, now_iso);
        std::unordered_map<std::string, const Campaign*> by_placement;
        for (const auto& pair : eligible) {
            by_placement[pair.first.id] = &pair.second;
        }

        nlohmann::json placements = nlohmann::json::array();
        for (const auto& placement : db_->list_placements(site->id)) {
            if (placement.status != "active") continue;
            nlohmann::json item = {
                {"placement", placement.to_json()},
                {"campaign", nullptr},
                {"affiliate_offer", nullptr},
                {"disclosure_text", ""},
                {"click_url", nullptr}
            };
            auto found = by_placement.find(placement.id);
            if (found != by_placement.end()) {
                const Campaign& campaign = *found->second;
                std::optional<AffiliateOffer> offer;
                if (campaign.source_type == "affiliate") {
                    offer = db_->get_affiliate_offer(campaign.affiliate_offer_id);
                }
                const bool destination_ok = source_allowed_for_site(*site, campaign.source_type) &&
                    (campaign.source_type == "affiliate"
                         ? (offer && offer->status == "active")
                         : utils::is_safe_url(campaign.destination_url));
                if (destination_ok) {
                    item["campaign"] = campaign.to_json();
                    if (offer) item["affiliate_offer"] = offer->to_json();
                    item["disclosure_text"] = effective_disclosure_text(
                        campaign, offer ? &*offer : nullptr, *site, placement);
                    item["click_url"] = (campaign.source_type == "affiliate" ? "/r/" : "/click/") +
                        create_click_token(campaign.id);
                }
            }
            placements.push_back(item);
        }

        res.set_content(nlohmann::json({
            {"site", site->to_json()},
            {"allowed_domains", effective_domains(*site)},
            {"placements", placements}
        }).dump(), "application/json");
    });

    // ==========================================
    // Website management
    // ==========================================
    svr.Get("/api/v1/sites", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> publisher_filter = std::nullopt;
        if (is_admin_user(*user)) {
            if (req.has_param("publisher_id")) {
                publisher_filter = req.get_param_value("publisher_id");
            }
        } else {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden: account is not linked to a publisher");
                return;
            }
            publisher_filter = user->publisher_id;
        }

        auto sites = db_->list_sites(publisher_filter);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& s : sites) list.push_back(s.to_json());
        res.set_content(nlohmann::json({{"sites", list}}).dump(), "application/json");
    });

    svr.Post("/api/v1/sites", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Site s;
        std::string err;
        if (!read_string(body, "name", s.name, err) ||
            !read_string(body, "primary_domain", s.primary_domain, err) ||
            !read_string(body, "status", s.status, err) ||
            !read_string(body, "default_disclosure_text", s.default_disclosure_text, err) ||
            !read_string_array(body, "allowed_domains", s.allowed_domains, err) ||
            !read_string_array(body, "allowed_ad_sources", s.allowed_ad_sources, err)) {
            json_error(res, 400, err);
            return;
        }
        if (s.status.empty()) s.status = "active";

        if (is_admin_user(*user) && body.contains("publisher_id")) {
            if (!body["publisher_id"].is_string()) {
                json_error(res, 400, "publisher_id must be a string");
                return;
            }
            s.publisher_id = utils::trim_copy(body["publisher_id"].get<std::string>());
            if (!s.publisher_id.empty() && !db_->get_publisher(s.publisher_id)) {
                json_error(res, 400, "publisher_id does not reference an existing publisher");
                return;
            }
        } else {
            // Publisher accounts always create inventory inside their own tenant.
            s.publisher_id = user->publisher_id;
        }

        if (!validate_site(s, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->create_site(s)) {
            json_error(res, 500, "Failed to create site");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_SITE";
        al.entity_type = "site";
        al.entity_id = s.id;
        al.details = s.to_json().dump();
        db_->record_audit_log(al);

        res.status = 201;
        nlohmann::json response = s.to_json();
        if (s.verification_status == "unverified") {
            response["verification_required"] = true;
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Get(R"(/api/v1/sites/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        res.set_content(site->to_json().dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/sites/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site_opt = db_->get_site(id);
        if (!site_opt || !can_access_site(*user, *site_opt)) {
            json_error(res, 404, "Site not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        if (!is_admin_user(*user) &&
            (body.contains("verification_status") || body.contains("monetization_status") ||
             body.contains("verified_at") || body.contains("publisher_id"))) {
            json_error(res, 403, "Forbidden: only administrators can change verification or monetization fields");
            return;
        }

        Site updated = *site_opt;
        const std::string previous_domain = updated.primary_domain;
        std::string err;
        if (!read_string(body, "name", updated.name, err) ||
            !read_string(body, "primary_domain", updated.primary_domain, err) ||
            !read_string(body, "status", updated.status, err) ||
            !read_string(body, "default_disclosure_text", updated.default_disclosure_text, err) ||
            !read_string_array(body, "allowed_domains", updated.allowed_domains, err) ||
            !read_string_array(body, "allowed_ad_sources", updated.allowed_ad_sources, err)) {
            json_error(res, 400, err);
            return;
        }
        if (is_admin_user(*user)) {
            if (!read_string(body, "monetization_status", updated.monetization_status, err)) {
                json_error(res, 400, err);
                return;
            }
        }

        if (!validate_site(updated, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->update_site(updated)) {
            json_error(res, 500, "Failed to update site");
            return;
        }

        // A domain change must never inherit the previous domain's verification.
        if (!site_opt->publisher_id.empty() &&
            utils::normalize_host(previous_domain) != utils::normalize_host(updated.primary_domain)) {
            db_->reset_site_verification(id);
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_SITE";
        al.entity_type = "site";
        al.entity_id = id;
        al.details = body.dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_site(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    // Publisher-readable verification challenge. The token is not part of the
    // normal site representation; it is only returned to the tenant owner or an
    // administrator on this authenticated endpoint.
    svr.Get(R"(/api/v1/sites/([^/]+)/verification)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }

        std::string domain = normalized_domain_no_port(site->primary_domain);
        nlohmann::json out = {
            {"site_id", site->id},
            {"domain", domain},
            {"verification_status", site->verification_status},
            {"verified_at", site->verified_at},
            {"monetization_status", site->monetization_status},
            {"methods", nlohmann::json::array({"dns_txt"})}
        };
        if (site->verification_status != "verified" && !site->verification_token.empty()) {
            out["method"] = "dns_txt";
            out["record_type"] = "TXT";
            out["record_name"] = std::string(VERIFICATION_RECORD_PREFIX) + domain;
            out["record_value"] = std::string(VERIFICATION_VALUE_PREFIX) + site->verification_token;
            out["instructions"] = nlohmann::json::array({
                "Create the TXT record at your DNS provider.",
                "Keep the value exactly as shown; DNS changes can take a few minutes to propagate.",
                "Return here and select Verify with DNS TXT."
            });
        }
        if (is_admin_user(*user)) {
            out["manual_verification_available"] = true;
        }
        res.set_content(out.dump(), "application/json");
    });

    svr.Post(R"(/api/v1/sites/([^/]+)/verify)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        if (site->verification_status == "verified") {
            res.set_content(nlohmann::json({
                {"status", "already_verified"},
                {"site", site->to_json()}
            }).dump(), "application/json");
            return;
        }
        if (site->publisher_id.empty()) {
            json_error(res, 400, "Platform-owned sites are verified administratively");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        std::string method = "dns_txt";
        std::string err;
        if (!read_string(body, "method", method, err)) {
            json_error(res, 400, err);
            return;
        }
        if (method.empty()) method = "dns_txt";

        bool verified = false;
        if (method == "dns_txt") {
            verified = dns_txt_has_verification_token(site->primary_domain, site->verification_token);
            if (!verified) {
                json_error(res, 400,
                    "TXT record not found or does not match. Confirm the record name and value, "
                    "allow for DNS propagation, and try again.");
                return;
            }
        } else if (method == "manual") {
            if (!is_admin_user(*user)) {
                json_error(res, 403, "Forbidden: manual verification requires an administrator");
                return;
            }
            verified = true;
        } else {
            json_error(res, 400, "method must be dns_txt or manual");
            return;
        }

        if (!db_->mark_site_verified(id, method)) {
            json_error(res, 500, "Failed to save verification");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "VERIFY_SITE";
        al.entity_type = "site";
        al.entity_id = id;
        al.details = nlohmann::json({{"method", method},
                                     {"domain", normalized_domain_no_port(site->primary_domain)}}).dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_site(id);
        res.set_content(nlohmann::json({
            {"status", "verified"},
            {"method", method},
            {"site", fresh->to_json()}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v1/sites/([^/]+)/rotate-key)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        std::string new_key;
        if (!db_->rotate_site_key(id, new_key)) {
            json_error(res, 500, "Failed to rotate key");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "ROTATE_KEY";
        al.entity_type = "site";
        al.entity_id = id;
        al.details = nlohmann::json({{"new_key", new_key}}).dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_site(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Post(R"(/api/v1/sites/([^/]+)/pause-all)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        if (!db_->pause_all_campaigns_for_site(id)) {
            json_error(res, 500, "Failed to pause campaigns");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "PAUSE_ALL_CAMPAIGNS";
        al.entity_type = "site";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.set_content(nlohmann::json({{"status", "all_campaigns_paused"}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/sites/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto site = db_->get_site(id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Site not found");
            return;
        }
        if (db_->site_has_events(id)) {
            json_error(res, 409,
                "This site has analytics history and cannot be deleted. Disable it instead to preserve reporting integrity.");
            return;
        }
        if (!db_->delete_site(id)) {
            json_error(res, 500, "Failed to delete site");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_SITE";
        al.entity_type = "site";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // ==========================================
    // Placements
    // ==========================================
    svr.Get("/api/v1/placements", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> site_id = std::nullopt;
        if (req.has_param("site_id")) site_id = req.get_param_value("site_id");

        if (!is_admin_user(*user)) {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden: account is not linked to a publisher");
                return;
            }
            if (site_id.has_value()) {
                auto site = db_->get_site(*site_id);
                if (!site || !can_access_site(*user, *site)) {
                    json_error(res, 404, "Site not found");
                    return;
                }
            }
        }

        std::set<std::string> tenant_sites;
        if (!is_admin_user(*user)) {
            for (const auto& s : db_->list_sites(user->publisher_id)) tenant_sites.insert(s.id);
        }

        auto placements = db_->list_placements(site_id);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& p : placements) {
            if (!is_admin_user(*user) && tenant_sites.find(p.site_id) == tenant_sites.end()) continue;
            list.push_back(p.to_json());
        }
        res.set_content(nlohmann::json({{"placements", list}}).dump(), "application/json");
    });

    svr.Post("/api/v1/placements", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Placement p;
        std::string err;
        if (!read_string(body, "site_id", p.site_id, err) ||
            !read_string(body, "placement_key", p.placement_key, err) ||
            !read_string(body, "name", p.name, err) ||
            !read_string(body, "description", p.description, err) ||
            !read_string(body, "format", p.format, err) ||
            !read_string(body, "fallback_behavior", p.fallback_behavior, err) ||
            !read_string(body, "status", p.status, err) ||
            !read_string(body, "disclosure_mode", p.disclosure_mode, err) ||
            !read_int(body, "width", p.width, err) ||
            !read_int(body, "height", p.height, err) ||
            !read_int(body, "max_items", p.max_items, err) ||
            !read_string_array(body, "allowed_source_types", p.allowed_source_types, err) ||
            !read_layout_config(body, p.layout_config, err)) {
            json_error(res, 400, err);
            return;
        }
        if (p.format.empty()) p.format = "responsive_banner";
        if (p.fallback_behavior.empty()) p.fallback_behavior = "hide";
        if (p.status.empty()) p.status = "active";
        if (p.disclosure_mode.empty()) p.disclosure_mode = "inherited";

        auto site = db_->get_site(p.site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 400, "site_id does not reference an accessible site");
            return;
        }
        if (!validate_placement(p, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->create_placement(p)) {
            json_error(res, 400, "Placement key already exists for this site or failed to save");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_PLACEMENT";
        al.entity_type = "placement";
        al.entity_id = p.id;
        al.details = p.to_json().dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(p.to_json().dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/placements/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto p_opt = db_->get_placement(id);
        auto site = p_opt ? db_->get_site(p_opt->site_id) : std::nullopt;
        if (!p_opt || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Placement not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Placement p = *p_opt;
        std::string err;
        if (!read_string(body, "name", p.name, err) ||
            !read_string(body, "description", p.description, err) ||
            !read_string(body, "format", p.format, err) ||
            !read_string(body, "fallback_behavior", p.fallback_behavior, err) ||
            !read_string(body, "status", p.status, err) ||
            !read_string(body, "disclosure_mode", p.disclosure_mode, err) ||
            !read_int(body, "width", p.width, err) ||
            !read_int(body, "height", p.height, err) ||
            !read_int(body, "max_items", p.max_items, err) ||
            !read_string_array(body, "allowed_source_types", p.allowed_source_types, err) ||
            !read_layout_config(body, p.layout_config, err)) {
            json_error(res, 400, err);
            return;
        }

        if (!validate_placement(p, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->update_placement(p)) {
            json_error(res, 500, "Failed to update placement");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_PLACEMENT";
        al.entity_type = "placement";
        al.entity_id = id;
        al.details = body.dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_placement(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/placements/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto p_opt = db_->get_placement(id);
        auto site = p_opt ? db_->get_site(p_opt->site_id) : std::nullopt;
        if (!p_opt || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Placement not found");
            return;
        }

        if (db_->placement_has_events(id)) {
            if (!db_->archive_placement(id)) {
                json_error(res, 500, "Failed to archive placement");
                return;
            }
            AuditLog al;
            al.actor = user->email;
            al.action = "ARCHIVE_PLACEMENT";
            al.entity_type = "placement";
            al.entity_id = id;
            al.details = nlohmann::json({{"reason", "analytics history preserved"}}).dump();
            db_->record_audit_log(al);
            res.set_content(nlohmann::json({
                {"status", "archived"},
                {"message", "Placement disabled because it has analytics history; event records were preserved."}
            }).dump(), "application/json");
            return;
        }

        if (!db_->delete_placement(id)) {
            json_error(res, 500, "Failed to delete placement");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_PLACEMENT";
        al.entity_type = "placement";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // ==========================================
    // Campaigns
    // ==========================================
    svr.Get("/api/v1/campaigns", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> site_id = req.has_param("site_id")
            ? std::make_optional(req.get_param_value("site_id")) : std::nullopt;
        std::optional<std::string> placement_id = req.has_param("placement_id")
            ? std::make_optional(req.get_param_value("placement_id")) : std::nullopt;
        std::optional<std::string> status = req.has_param("status")
            ? std::make_optional(req.get_param_value("status")) : std::nullopt;

        std::optional<std::string> scope = std::nullopt;
        if (is_admin_user(*user)) {
            if (req.has_param("publisher_id")) {
                scope = req.get_param_value("publisher_id");
            }
        } else {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden: account is not linked to a publisher");
                return;
            }
            scope = user->publisher_id;
        }

        auto campaigns = db_->list_campaigns(site_id, placement_id, status, scope);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& c : campaigns) list.push_back(c.to_json());
        res.set_content(nlohmann::json({{"campaigns", list}}).dump(), "application/json");
    });

    svr.Post("/api/v1/campaigns", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Campaign c;
        std::string err;
        if (!read_string(body, "site_id", c.site_id, err) ||
            !read_string(body, "placement_id", c.placement_id, err) ||
            !read_string(body, "name", c.name, err) ||
            !read_string(body, "destination_url", c.destination_url, err) ||
            !read_string(body, "format", c.format, err) ||
            !read_string(body, "title", c.title, err) ||
            !read_string(body, "body", c.body, err) ||
            !read_string(body, "image_url", c.image_url, err) ||
            !read_string(body, "alt_text", c.alt_text, err) ||
            !read_string(body, "cta_label", c.cta_label, err) ||
            !read_string(body, "status", c.status, err) ||
            !read_string(body, "source_type", c.source_type, err) ||
            !read_string(body, "affiliate_offer_id", c.affiliate_offer_id, err) ||
            !read_string(body, "disclosure_override", c.disclosure_override, err) ||
            !read_string(body, "starts_at", c.starts_at, err) ||
            !read_string(body, "ends_at", c.ends_at, err) ||
            !read_int(body, "priority", c.priority, err) ||
            !read_int(body, "weight", c.weight, err)) {
            json_error(res, 400, err);
            return;
        }
        if (c.status.empty()) c.status = "draft";
        if (c.source_type.empty()) c.source_type = "house";
        if (c.priority == 0) c.priority = 1;
        if (c.weight == 0) c.weight = 100;

        auto site = db_->get_site(c.site_id);
        if (!site || !can_access_site(*user, *site)) {
            json_error(res, 400, "site_id does not reference an accessible site");
            return;
        }
        auto placement = db_->get_placement(c.placement_id);
        if (!placement || placement->site_id != c.site_id) {
            json_error(res, 400, "placement_id must belong to the selected site");
            return;
        }
        if (c.format.empty()) c.format = placement->format;

        std::optional<AffiliateOffer> offer;
        if (c.source_type == "affiliate") {
            if (c.affiliate_offer_id.empty()) {
                json_error(res, 400, "affiliate_offer_id is required for affiliate campaigns");
                return;
            }
            offer = db_->get_affiliate_offer(c.affiliate_offer_id);
            if (!offer) {
                json_error(res, 400, "affiliate_offer_id does not reference an existing offer");
                return;
            }
        }
        if (!apply_campaign_source(c, *site, *placement, *user, offer, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!validate_campaign(c, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->create_campaign(c)) {
            json_error(res, 500, "Failed to create campaign");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_CAMPAIGN";
        al.entity_type = "campaign";
        al.entity_id = c.id;
        al.details = c.to_json().dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(c.to_json().dump(), "application/json");
    });

    svr.Get(R"(/api/v1/campaigns/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        auto c = db_->get_campaign(req.matches[1]);
        auto site = c ? db_->get_site(c->site_id) : std::nullopt;
        if (!c || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Campaign not found");
            return;
        }
        res.set_content(c->to_json().dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/campaigns/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto c_opt = db_->get_campaign(id);
        auto site = c_opt ? db_->get_site(c_opt->site_id) : std::nullopt;
        if (!c_opt || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Campaign not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Campaign c = *c_opt;
        std::string err;
        if (!read_string(body, "placement_id", c.placement_id, err) ||
            !read_string(body, "name", c.name, err) ||
            !read_string(body, "destination_url", c.destination_url, err) ||
            !read_string(body, "format", c.format, err) ||
            !read_string(body, "title", c.title, err) ||
            !read_string(body, "body", c.body, err) ||
            !read_string(body, "image_url", c.image_url, err) ||
            !read_string(body, "alt_text", c.alt_text, err) ||
            !read_string(body, "cta_label", c.cta_label, err) ||
            !read_string(body, "status", c.status, err) ||
            !read_string(body, "source_type", c.source_type, err) ||
            !read_string(body, "affiliate_offer_id", c.affiliate_offer_id, err) ||
            !read_string(body, "disclosure_override", c.disclosure_override, err) ||
            !read_string(body, "starts_at", c.starts_at, err) ||
            !read_string(body, "ends_at", c.ends_at, err) ||
            !read_int(body, "priority", c.priority, err) ||
            !read_int(body, "weight", c.weight, err)) {
            json_error(res, 400, err);
            return;
        }

        auto placement = db_->get_placement(c.placement_id);
        if (!placement || placement->site_id != c.site_id) {
            json_error(res, 400, "placement_id must belong to this campaign's site");
            return;
        }

        std::optional<AffiliateOffer> offer;
        if (c.source_type == "affiliate") {
            if (c.affiliate_offer_id.empty()) {
                json_error(res, 400, "affiliate_offer_id is required for affiliate campaigns");
                return;
            }
            offer = db_->get_affiliate_offer(c.affiliate_offer_id);
            if (!offer) {
                json_error(res, 400, "affiliate_offer_id does not reference an existing offer");
                return;
            }
        }
        if (!apply_campaign_source(c, *site, *placement, *user, offer, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!validate_campaign(c, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->update_campaign(c)) {
            json_error(res, 500, "Failed to update campaign");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_CAMPAIGN";
        al.entity_type = "campaign";
        al.entity_id = id;
        al.details = body.dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_campaign(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Post(R"(/api/v1/campaigns/([^/]+)/publish)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto campaign = db_->get_campaign(id);
        auto site = campaign ? db_->get_site(campaign->site_id) : std::nullopt;
        if (!campaign || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Campaign not found");
            return;
        }
        auto placement = db_->get_placement(campaign->placement_id);
        if (!placement || placement->site_id != campaign->site_id) {
            json_error(res, 400, "Campaign placement is no longer valid");
            return;
        }
        std::optional<AffiliateOffer> offer;
        if (campaign->source_type == "affiliate") {
            offer = db_->get_affiliate_offer(campaign->affiliate_offer_id);
        }
        std::string publish_err;
        if (!campaign_publish_ready(*campaign, *site, offer, publish_err)) {
            json_error(res, 400, publish_err);
            return;
        }
        if (!db_->publish_campaign(id)) {
            json_error(res, 500, "Failed to publish campaign");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "PUBLISH_CAMPAIGN";
        al.entity_type = "campaign";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);

        auto fresh = db_->get_campaign(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Post(R"(/api/v1/campaigns/([^/]+)/pause)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto campaign = db_->get_campaign(id);
        auto site = campaign ? db_->get_site(campaign->site_id) : std::nullopt;
        if (!campaign || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Campaign not found");
            return;
        }
        if (!db_->pause_campaign(id)) {
            json_error(res, 404, "Campaign not found");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "PAUSE_CAMPAIGN";
        al.entity_type = "campaign";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);

        auto fresh = db_->get_campaign(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/campaigns/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        auto campaign = db_->get_campaign(id);
        auto site = campaign ? db_->get_site(campaign->site_id) : std::nullopt;
        if (!campaign || !site || !can_access_site(*user, *site)) {
            json_error(res, 404, "Campaign not found");
            return;
        }

        if (db_->campaign_has_events(id)) {
            if (!db_->archive_campaign(id)) {
                json_error(res, 500, "Failed to archive campaign");
                return;
            }
            AuditLog al;
            al.actor = user->email;
            al.action = "ARCHIVE_CAMPAIGN";
            al.entity_type = "campaign";
            al.entity_id = id;
            al.details = nlohmann::json({{"reason", "analytics history preserved"}}).dump();
            db_->record_audit_log(al);
            res.set_content(nlohmann::json({
                {"status", "archived"},
                {"message", "Campaign archived because it has analytics history; event records were preserved."}
            }).dump(), "application/json");
            return;
        }

        if (!db_->delete_campaign(id)) {
            json_error(res, 500, "Failed to delete campaign");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_CAMPAIGN";
        al.entity_type = "campaign";
        al.entity_id = id;
        al.details = "{}";
        db_->record_audit_log(al);
        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // ==========================================
    // Publishers (invite-only / administrator provisioned)
    // ==========================================
    svr.Get("/api/v1/publishers", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_admin(req, res);
        if (!user) return;

        auto publishers = db_->list_publishers();
        nlohmann::json list = nlohmann::json::array();
        for (const auto& p : publishers) list.push_back(p.to_json());
        res.set_content(nlohmann::json({{"publishers", list}}).dump(), "application/json");
    });

    svr.Post("/api/v1/publishers", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_admin(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        Publisher publisher;
        std::string owner_email;
        std::string password;
        std::string err;
        if (!read_string(body, "name", publisher.name, err) ||
            !read_string(body, "owner_email", owner_email, err) ||
            !read_string(body, "password", password, err) ||
            !read_string(body, "status", publisher.status, err) ||
            !read_string(body, "contact_email", publisher.contact_email, err) ||
            !read_string(body, "timezone", publisher.timezone, err)) {
            json_error(res, 400, err);
            return;
        }
        publisher.name = utils::trim_copy(publisher.name);
        owner_email = utils::to_lower_copy(utils::trim_copy(owner_email));
        publisher.contact_email = utils::to_lower_copy(utils::trim_copy(publisher.contact_email));
        publisher.timezone = utils::trim_copy(publisher.timezone);

        if (publisher.name.empty() || too_long(publisher.name, 120)) {
            json_error(res, 400, "name is required (max 120 characters)");
            return;
        }
        if (!is_valid_email(owner_email)) {
            json_error(res, 400, "owner_email must be a valid email address");
            return;
        }
        if (password.size() < 12 || password.size() > 128) {
            json_error(res, 400, "password must be between 12 and 128 characters");
            return;
        }
        if (publisher.status.empty()) publisher.status = "pending";
        if (!is_valid_publisher_status(publisher.status)) {
            json_error(res, 400, "invalid publisher status");
            return;
        }
        if (publisher.contact_email.empty()) publisher.contact_email = owner_email;
        if (!is_valid_email(publisher.contact_email)) {
            json_error(res, 400, "contact_email must be a valid email address");
            return;
        }
        if (publisher.timezone.empty()) publisher.timezone = "UTC";
        static const std::regex timezone_re(R"(^[A-Za-z0-9_+\-/]{1,64}$)");
        if (!std::regex_match(publisher.timezone, timezone_re)) {
            json_error(res, 400, "timezone must be an IANA timezone name");
            return;
        }
        if (db_->get_user_by_email(owner_email)) {
            json_error(res, 409, "An account with this email already exists");
            return;
        }

        User owner;
        owner.email = owner_email;
        owner.salt = utils::random_hex(16);
        owner.password_hash = utils::hash_password(password, owner.salt);

        if (!db_->create_publisher_with_owner(publisher, owner)) {
            json_error(res, 500, "Failed to create publisher");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_PUBLISHER";
        al.entity_type = "publisher";
        al.entity_id = publisher.id;
        al.details = nlohmann::json({
            {"name", publisher.name},
            {"owner_email", owner_email},
            {"status", publisher.status}
        }).dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(publisher.to_json().dump(), "application/json");
    });

    svr.Get("/api/v1/publishers/profile", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;
        if (user->publisher_id.empty()) {
            json_error(res, 404, "This account is not linked to a publisher");
            return;
        }
        auto publisher = db_->get_publisher(user->publisher_id);
        if (!publisher) {
            json_error(res, 404, "Publisher not found");
            return;
        }
        auto sites = db_->list_sites(user->publisher_id);
        res.set_content(nlohmann::json({
            {"publisher", publisher->to_json()},
            {"sites_count", sites.size()}
        }).dump(), "application/json");
    });

    svr.Get(R"(/api/v1/publishers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;
        const std::string id = req.matches[1];
        if (!can_access_publisher(*user, id)) {
            json_error(res, 404, "Publisher not found");
            return;
        }
        auto publisher = db_->get_publisher(id);
        if (!publisher) {
            json_error(res, 404, "Publisher not found");
            return;
        }
        auto sites = db_->list_sites(id);
        nlohmann::json site_list = nlohmann::json::array();
        for (const auto& s : sites) site_list.push_back(s.to_json());
        res.set_content(nlohmann::json({
            {"publisher", publisher->to_json()},
            {"sites", site_list}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/publishers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        const std::string id = req.matches[1];
        if (!can_access_publisher(*user, id)) {
            json_error(res, 404, "Publisher not found");
            return;
        }
        auto p_opt = db_->get_publisher(id);
        if (!p_opt) {
            json_error(res, 404, "Publisher not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        if (!is_admin_user(*user) &&
            (body.contains("name") || body.contains("status") || body.contains("owner_user_id"))) {
            json_error(res, 403, "Forbidden: publishers may only update contact details");
            return;
        }

        Publisher p = *p_opt;
        std::string err;
        if (!read_string(body, "name", p.name, err) ||
            !read_string(body, "status", p.status, err) ||
            !read_string(body, "contact_email", p.contact_email, err) ||
            !read_string(body, "timezone", p.timezone, err)) {
            json_error(res, 400, err);
            return;
        }
        p.name = utils::trim_copy(p.name);
        p.contact_email = utils::to_lower_copy(utils::trim_copy(p.contact_email));
        p.timezone = utils::trim_copy(p.timezone);
        if (p.name.empty() || too_long(p.name, 120)) {
            json_error(res, 400, "name is required (max 120 characters)");
            return;
        }
        if (!is_valid_publisher_status(p.status)) {
            json_error(res, 400, "invalid publisher status");
            return;
        }
        if (!p.contact_email.empty() && !is_valid_email(p.contact_email)) {
            json_error(res, 400, "contact_email must be a valid email address");
            return;
        }
        static const std::regex timezone_re(R"(^[A-Za-z0-9_+\-/]{1,64}$)");
        if (!p.timezone.empty() && !std::regex_match(p.timezone, timezone_re)) {
            json_error(res, 400, "timezone must be an IANA timezone name");
            return;
        }

        if (!db_->update_publisher(p)) {
            json_error(res, 500, "Failed to update publisher");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_PUBLISHER";
        al.entity_type = "publisher";
        al.entity_id = id;
        al.details = body.dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_publisher(id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    // ==========================================
    // Affiliate offers (manually entered, provider adapter only)
    // ==========================================
    svr.Get("/api/v1/affiliate-offers", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> scope;
        if (is_admin_user(*user)) {
            if (req.has_param("publisher_id")) scope = req.get_param_value("publisher_id");
        } else {
            if (user->publisher_id.empty()) {
                json_error(res, 403, "Forbidden: account is not linked to a publisher");
                return;
            }
            scope = user->publisher_id;
        }
        std::optional<std::string> status = std::nullopt;
        if (req.has_param("status")) {
            const std::string requested = req.get_param_value("status");
            if (!is_valid_offer_status(requested)) {
                json_error(res, 400, "invalid status filter");
                return;
            }
            status = requested;
        }

        auto offers = db_->list_affiliate_offers(scope, status);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& offer : offers) list.push_back(offer.to_json());
        res.set_content(nlohmann::json({{"affiliate_offers", list}}).dump(), "application/json");
    });

    svr.Post("/api/v1/affiliate-offers", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        AffiliateOffer offer;
        std::string err;
        if (!read_offer_body(body, offer, err)) {
            json_error(res, 400, err);
            return;
        }
        if (offer.provider.empty()) offer.provider = "shopee_affiliate";
        if (offer.status.empty()) offer.status = "draft";

        if (is_admin_user(*user)) {
            std::string publisher_id;
            if (!read_string(body, "publisher_id", publisher_id, err)) {
                json_error(res, 400, err);
                return;
            }
            offer.publisher_id = utils::trim_copy(publisher_id);
            if (offer.publisher_id.empty()) {
                json_error(res, 400, "publisher_id is required for administrator-created offers");
                return;
            }
        } else {
            offer.publisher_id = user->publisher_id;
        }
        if (!db_->get_publisher(offer.publisher_id)) {
            json_error(res, 400, "publisher_id does not reference an existing publisher");
            return;
        }

        if (!validate_offer(offer, err)) {
            json_error(res, 400, err);
            return;
        }
        offer.validation_status = "unverified";
        offer.validation_reason = "Structural validation only; the destination was not fetched.";
        offer.last_link_validation_at.clear();

        if (!db_->create_affiliate_offer(offer)) {
            json_error(res, 500, "Failed to create affiliate offer");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_AFFILIATE_OFFER";
        al.entity_type = "affiliate_offer";
        al.entity_id = offer.id;
        al.details = nlohmann::json({
            {"publisher_id", offer.publisher_id},
            {"provider", offer.provider},
            {"offer_name", offer.offer_name},
            {"status", offer.status}
        }).dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(offer.to_json().dump(), "application/json");
    });

    svr.Get(R"(/api/v1/affiliate-offers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate(req, res);
        if (!user) return;
        auto offer = db_->get_affiliate_offer(req.matches[1]);
        if (!offer || !can_access_publisher(*user, offer->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }
        nlohmann::json campaigns = nlohmann::json::array();
        for (const auto& c : db_->list_campaigns_by_offer(offer->id)) campaigns.push_back(c.to_json());
        res.set_content(nlohmann::json({
            {"affiliate_offer", offer->to_json()},
            {"campaigns", campaigns}
        }).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/affiliate-offers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto o_opt = db_->get_affiliate_offer(req.matches[1]);
        if (!o_opt || !can_access_publisher(*user, o_opt->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        AffiliateOffer offer = *o_opt;
        std::string err;
        if (!read_offer_body(body, offer, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!validate_offer(offer, err)) {
            json_error(res, 400, err);
            return;
        }
        if (offer.affiliate_url != o_opt->affiliate_url) {
            offer.validation_status = "unverified";
            offer.validation_reason = "Destination changed; run structural validation again.";
            offer.last_link_validation_at.clear();
        }
        if (!db_->update_affiliate_offer(offer)) {
            json_error(res, 500, "Failed to update affiliate offer");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_AFFILIATE_OFFER";
        al.entity_type = "affiliate_offer";
        al.entity_id = offer.id;
        al.details = body.dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_affiliate_offer(offer.id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    svr.Post(R"(/api/v1/affiliate-offers/([^/]+)/validate)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto o_opt = db_->get_affiliate_offer(req.matches[1]);
        if (!o_opt || !can_access_publisher(*user, o_opt->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }

        AffiliateOffer offer = *o_opt;
        std::string err;
        if (!validate_offer(offer, err)) {
            offer.validation_status = "rejected";
            offer.validation_reason = err;
            offer.last_link_validation_at = utils::get_current_iso8601();
            db_->update_affiliate_offer(offer);
            json_error(res, 400, err);
            return;
        }

        // Structural validation only. AdControl does not fetch the destination;
        // doing so would require a separately reviewed SSRF-safe link checker.
        offer.validation_status = "valid";
        offer.validation_reason = "Structurally valid HTTPS URL; destination was not fetched.";
        offer.last_link_validation_at = utils::get_current_iso8601();
        if (!db_->update_affiliate_offer(offer)) {
            json_error(res, 500, "Failed to save validation result");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "VALIDATE_AFFILIATE_OFFER";
        al.entity_type = "affiliate_offer";
        al.entity_id = offer.id;
        al.details = nlohmann::json({{"validation_status", offer.validation_status}}).dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_affiliate_offer(offer.id);
        res.set_content(fresh->to_json().dump(), "application/json");
    });

    auto set_offer_status = [this](const httplib::Request& req, httplib::Response& res,
                                   const std::string& status, const std::string& action) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto offer = db_->get_affiliate_offer(req.matches[1]);
        if (!offer || !can_access_publisher(*user, offer->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }
        if (status == "active") {
            std::string err;
            AffiliateOffer candidate = *offer;
            candidate.status = "active";
            if (!validate_offer(candidate, err)) {
                json_error(res, 400, err);
                return;
            }
            if (candidate.validation_status == "rejected") {
                json_error(res, 400, "A rejected affiliate offer cannot be activated");
                return;
            }
        }
        if (!db_->set_affiliate_offer_status(offer->id, status)) {
            json_error(res, 500, "Failed to update affiliate offer status");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = action;
        al.entity_type = "affiliate_offer";
        al.entity_id = offer->id;
        al.details = nlohmann::json({{"status", status}}).dump();
        db_->record_audit_log(al);

        auto fresh = db_->get_affiliate_offer(offer->id);
        res.set_content(fresh->to_json().dump(), "application/json");
    };
    svr.Post(R"(/api/v1/affiliate-offers/([^/]+)/publish)", [set_offer_status](const httplib::Request& req, httplib::Response& res) {
        set_offer_status(req, res, "active", "PUBLISH_AFFILIATE_OFFER");
    });
    svr.Post(R"(/api/v1/affiliate-offers/([^/]+)/pause)", [set_offer_status](const httplib::Request& req, httplib::Response& res) {
        set_offer_status(req, res, "paused", "PAUSE_AFFILIATE_OFFER");
    });

    // Creates a product-card campaign for an offer in one step. The offer's
    // destination is copied server-side; request bodies cannot set it.
    svr.Post(R"(/api/v1/affiliate-offers/([^/]+)/placements)", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto offer_opt = db_->get_affiliate_offer(req.matches[1]);
        if (!offer_opt || !can_access_publisher(*user, offer_opt->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        std::string placement_id;
        std::string name;
        std::string status;
        std::string starts_at;
        std::string ends_at;
        std::string err;
        int priority = 1;
        int weight = 100;
        if (!read_string(body, "placement_id", placement_id, err) ||
            !read_string(body, "name", name, err) ||
            !read_string(body, "status", status, err) ||
            !read_string(body, "starts_at", starts_at, err) ||
            !read_string(body, "ends_at", ends_at, err) ||
            !read_int(body, "priority", priority, err) ||
            !read_int(body, "weight", weight, err)) {
            json_error(res, 400, err);
            return;
        }
        if (placement_id.empty()) {
            json_error(res, 400, "placement_id is required");
            return;
        }
        auto placement = db_->get_placement(placement_id);
        auto site = placement ? db_->get_site(placement->site_id) : std::nullopt;
        if (!placement || !site || !can_access_site(*user, *site)) {
            json_error(res, 400, "placement_id does not reference an accessible placement");
            return;
        }
        if (offer_opt->publisher_id != site->publisher_id) {
            json_error(res, 400, "placement and affiliate offer belong to different publishers");
            return;
        }

        Campaign c;
        c.site_id = site->id;
        c.placement_id = placement->id;
        c.name = utils::trim_copy(name);
        if (c.name.empty()) c.name = offer_opt->offer_name;
        if (too_long(c.name, 120)) {
            json_error(res, 400, "name must be 120 characters or fewer");
            return;
        }
        c.format = "product_card";
        c.source_type = "affiliate";
        c.affiliate_offer_id = offer_opt->id;
        c.status = status.empty() ? "draft" : status;
        c.priority = priority;
        c.weight = weight;
        c.starts_at = starts_at;
        c.ends_at = ends_at;

        if (!apply_campaign_source(c, *site, *placement, *user, offer_opt, err) ||
            !validate_campaign(c, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->create_campaign(c)) {
            json_error(res, 500, "Failed to create campaign");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "PLACE_AFFILIATE_OFFER";
        al.entity_type = "campaign";
        al.entity_id = c.id;
        al.details = nlohmann::json({
            {"affiliate_offer_id", offer_opt->id},
            {"site_id", site->id},
            {"placement_id", placement->id}
        }).dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(c.to_json().dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/affiliate-offers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto user = authenticate_writer(req, res);
        if (!user) return;
        auto offer = db_->get_affiliate_offer(req.matches[1]);
        if (!offer || !can_access_publisher(*user, offer->publisher_id)) {
            json_error(res, 404, "Affiliate offer not found");
            return;
        }
        if (db_->affiliate_offer_has_campaigns(offer->id)) {
            json_error(res, 409,
                "This offer is used by one or more campaigns. Pause or delete those campaigns first.");
            return;
        }
        if (!db_->delete_affiliate_offer(offer->id)) {
            json_error(res, 500, "Failed to delete affiliate offer");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_AFFILIATE_OFFER";
        al.entity_type = "affiliate_offer";
        al.entity_id = offer->id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.set_content(nlohmann::json({{"status", "deleted"}}).dump(), "application/json");
    });

    // ==========================================
    // Analytics
    // ==========================================
    svr.Get("/api/v1/analytics/summary", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> scope;
        std::optional<std::string> site_id;
        std::optional<std::string> placement_id;
        std::optional<std::string> campaign_id;
        if (!resolve_analytics_scope(*db_, *user, req, scope, site_id, placement_id, campaign_id, res)) {
            return;
        }
        const std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        const std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        if (!validate_date_filter(start_date, "start_date", res) ||
            !validate_date_filter(end_date, "end_date", res)) {
            return;
        }

        auto summary = db_->get_analytics_summary(site_id, placement_id, campaign_id,
                                                  start_date, end_date, scope);
        res.set_content(summary.dump(), "application/json");
    });

    svr.Get("/api/v1/analytics/timeseries", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> scope;
        std::optional<std::string> site_id;
        std::optional<std::string> placement_id;
        std::optional<std::string> campaign_id;
        if (!resolve_analytics_scope(*db_, *user, req, scope, site_id, placement_id, campaign_id, res)) {
            return;
        }
        int days = 7;
        if (req.has_param("days") && !parse_int(req.get_param_value("days"), days)) {
            json_error(res, 400, "days must be an integer");
            return;
        }
        if (days < 1) days = 1;
        if (days > 365) days = 365;
        const std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        const std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        if (!validate_date_filter(start_date, "start_date", res) ||
            !validate_date_filter(end_date, "end_date", res)) {
            return;
        }

        auto ts = db_->get_analytics_timeseries(site_id, placement_id, campaign_id, days,
                                               start_date, end_date, scope);
        res.set_content(ts.dump(), "application/json");
    });

    svr.Get("/api/v1/analytics/breakdown", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        const std::string dimension = req.has_param("dimension") ? req.get_param_value("dimension") : "campaign";
        if (dimension != "site" && dimension != "placement" &&
            dimension != "campaign" && dimension != "source") {
            json_error(res, 400, "dimension must be site, placement, campaign, or source");
            return;
        }
        std::optional<std::string> scope;
        std::optional<std::string> site_id;
        std::optional<std::string> placement_id;
        std::optional<std::string> campaign_id;
        if (!resolve_analytics_scope(*db_, *user, req, scope, site_id, placement_id, campaign_id, res)) {
            return;
        }
        const std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        const std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        if (!validate_date_filter(start_date, "start_date", res) ||
            !validate_date_filter(end_date, "end_date", res)) {
            return;
        }

        auto rows = db_->get_analytics_breakdown(dimension, site_id, placement_id, campaign_id,
                                                 start_date, end_date, scope);
        res.set_content(nlohmann::json({{"rows", rows}}).dump(), "application/json");
    });

    svr.Get("/api/v1/analytics/export.csv", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate(req, res);
        if (!user) return;

        std::optional<std::string> scope;
        std::optional<std::string> site_id;
        std::optional<std::string> placement_id;
        std::optional<std::string> campaign_id;
        if (!resolve_analytics_scope(*db_, *user, req, scope, site_id, placement_id, campaign_id, res)) {
            return;
        }
        const std::string start_date = req.has_param("start_date") ? req.get_param_value("start_date") : "";
        const std::string end_date = req.has_param("end_date") ? req.get_param_value("end_date") : "";
        if (!validate_date_filter(start_date, "start_date", res) ||
            !validate_date_filter(end_date, "end_date", res)) {
            return;
        }

        auto events = db_->get_events_export(site_id, placement_id, campaign_id,
                                             start_date, end_date, scope);
        std::ostringstream csv;
        csv << "occurred_at,event_type,event_id,site_id,placement_id,campaign_id,source_type,affiliate_offer_id,page_origin,referrer,user_agent_category\n";
        for (const auto& ev : events) {
            csv << csv_escape(ev.occurred_at) << ","
                << csv_escape(ev.event_type) << ","
                << csv_escape(ev.event_id) << ","
                << csv_escape(ev.site_id) << ","
                << csv_escape(ev.placement_id) << ","
                << csv_escape(ev.campaign_id) << ","
                << csv_escape(ev.source_type) << ","
                << csv_escape(ev.affiliate_offer_id) << ","
                << csv_escape(ev.page_origin) << ","
                << csv_escape(ev.referrer) << ","
                << csv_escape(ev.user_agent_category) << "\n";
        }

        res.set_header("Content-Disposition", "attachment; filename=\"adcontrol_events.csv\"");
        res.set_content(csv.str(), "text/csv; charset=utf-8");
    });

    // ==========================================
    // Audit log
    // ==========================================
    svr.Get("/api/v1/audit-log", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        // Audit records have no tenant column and can reference any entity;
        // they remain platform-administrator-only.
        auto user = authenticate_admin(req, res);
        if (!user) return;

        int limit = 50;
        int offset = 0;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        if (req.has_param("offset") && !parse_int(req.get_param_value("offset"), offset)) {
            json_error(res, 400, "offset must be an integer");
            return;
        }
        if (limit < 1) limit = 1;
        if (limit > 200) limit = 200;
        if (offset < 0) offset = 0;

        auto logs = db_->list_audit_logs(limit, offset);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& log : logs) list.push_back(log.to_json());
        res.set_content(nlohmann::json({{"audit_logs", list}}).dump(), "application/json");
    });

    // ==========================================
    // Partner applications (public publisher/client inquiries)
    // ==========================================
    svr.Get("/api/v1/partner-inquiries", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;

        std::optional<std::string> kind;
        std::optional<std::string> status;
        if (req.has_param("kind")) {
            kind = utils::to_lower_copy(utils::trim_copy(req.get_param_value("kind")));
            if (!is_valid_partner_inquiry_kind(*kind)) {
                json_error(res, 400, "kind must be publisher or client");
                return;
            }
        }
        if (req.has_param("status")) {
            status = utils::to_lower_copy(utils::trim_copy(req.get_param_value("status")));
            if (!is_valid_partner_inquiry_status(*status)) {
                json_error(res, 400, "status must be new, reviewing, or closed");
                return;
            }
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
        if (limit < 1) limit = 1;
        if (limit > 200) limit = 200;
        if (offset < 0) offset = 0;

        auto inquiries = db_->list_partner_inquiries(kind, status, limit, offset);
        nlohmann::json list = nlohmann::json::array();
        for (const auto& inquiry : inquiries) list.push_back(inquiry.to_json());
        res.set_content(nlohmann::json({{"inquiries", list}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/partner-inquiries/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;

        const std::string id = req.matches[1];
        if (id.empty() || id.size() > 80) {
            json_error(res, 400, "Invalid inquiry id");
            return;
        }
        auto existing = db_->get_partner_inquiry(id);
        if (!existing) {
            json_error(res, 404, "Inquiry not found");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body, 4 * 1024)) return;
        std::string status;
        try {
            if (!body.contains("status") || !body["status"].is_string()) {
                json_error(res, 400, "status is required");
                return;
            }
            status = utils::to_lower_copy(utils::trim_copy(body["status"].get<std::string>()));
        } catch (...) {
            json_error(res, 400, "Invalid status");
            return;
        }
        if (!is_valid_partner_inquiry_status(status)) {
            json_error(res, 400, "status must be new, reviewing, or closed");
            return;
        }
        if (!db_->update_partner_inquiry_status(id, status)) {
            json_error(res, 404, "Inquiry not found");
            return;
        }

        AuditLog al;
        al.actor = user->email;
        al.action = "PARTNER_INQUIRY_STATUS_UPDATED";
        al.entity_type = "partner_inquiry";
        al.entity_id = id;
        al.details = nlohmann::json({{"from", existing->status}, {"to", status}}).dump();
        db_->record_audit_log(al);

        auto updated = db_->get_partner_inquiry(id);
        res.set_content(nlohmann::json({{"inquiry", updated ? updated->to_json() : existing->to_json()}}).dump(),
                        "application/json");
    });

    // ==========================================
    // Phase 3: public learning catalog and SEO
    // ==========================================
    svr.Get("/sitemap.xml", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string base = learning_public_base_url();
        auto topics = db_->list_learning_topics(true);
        auto courses = db_->list_learning_courses(std::nullopt, std::nullopt, std::nullopt,
                                                  "", 200, 0, true);
        auto paths = db_->list_learning_paths(true);

        std::set<std::string> public_topic_ids;
        for (const auto& topic : topics) public_topic_ids.insert(topic.id);

        std::ostringstream xml;
        xml << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
        xml << R"(<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">)" << "\n";
        auto add_url = [&](const std::string& path, const std::string& lastmod,
                           const std::string& priority) {
            xml << "  <url><loc>" << xml_escape(base + path) << "</loc>";
            if (!lastmod.empty()) xml << "<lastmod>" << xml_escape(lastmod) << "</lastmod>";
            xml << "<priority>" << priority << "</priority></url>\n";
        };
        add_url("/", "", "1.0");
        add_url("/courses", "", "0.8");
        add_url("/topics", "", "0.7");
        add_url("/learning-paths", "", "0.7");
        add_url("/become-a-publisher", "", "0.6");
        add_url("/become-a-client", "", "0.6");
        add_url("/privacy", "", "0.5");
        add_url("/terms", "", "0.5");
        for (const auto& topic : topics) {
            add_url("/topics/" + topic.slug, topic.updated_at, "0.6");
        }
        for (const auto& course : courses) {
            if (public_topic_ids.count(course.topic_id) == 0) continue;
            add_url("/courses/" + course.slug, course.updated_at, "0.8");
        }
        for (const auto& path : paths) {
            add_url("/learning-paths/" + path.slug, path.updated_at, "0.7");
        }
        xml << "</urlset>\n";

        res.set_header("Cache-Control", "public, max-age=1800");
        res.set_content(xml.str(), "application/xml; charset=utf-8");
    });

    svr.Get("/robots.txt", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string base = learning_public_base_url();
        std::ostringstream body;
        body << "User-agent: *\n"
             << "Content-Signal: ai-train=yes, search=yes, ai-input=yes\n"
             << "Allow: /\n"
             << "Allow: /courses\n"
             << "Allow: /topics\n"
             << "Allow: /learning-paths\n"
             << "Allow: /become-a-publisher\n"
             << "Allow: /become-a-client\n"
             // Private app shells are not content, but keeping them out of
             // search results avoids indexing sign-in and authoring screens.
             // Access control is still enforced server-side.
             << "Disallow: /api/\n"
             << "Disallow: /author\n"
             << "Disallow: /my-learning\n"
             << "Disallow: /dashboard\n"
             << "\n"
             << "Sitemap: " << base << "/sitemap.xml\n"
             << "Agentmap: " << base << "/.well-known/ai-catalog.json\n";
        res.set_header("Cache-Control", "no-cache, must-revalidate");
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });

    svr.Get("/api/v3/catalog/courses", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string ip = client_ip(req);
        if (!allow_request("learn-catalog:" + ip, 300, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }

        std::optional<std::string> topic_id;
        if (req.has_param("topic")) {
            const std::string topic_slug = utils::to_lower_copy(
                utils::trim_copy(req.get_param_value("topic")));
            if (!is_valid_learning_slug(topic_slug)) {
                json_error(res, 400, "topic must be a valid topic slug");
                return;
            }
            auto topic = db_->get_learning_topic_by_slug(topic_slug);
            if (!topic || topic->status != "published") {
                json_error(res, 404, "Topic not found");
                return;
            }
            topic_id = topic->id;
        }
        std::string query = req.has_param("q")
            ? utils::trim_copy(req.get_param_value("q")) : "";
        if (query.size() > 100) {
            json_error(res, 400, "q is too long");
            return;
        }
        int limit = 50;
        int offset = 0;
        if (req.has_param("limit") && !parse_int(req.get_param_value("limit"), limit)) {
            json_error(res, 400, "limit must be an integer");
            return;
        }
        if (req.has_param("offset") && !parse_int(req.get_param_value("offset"), offset)) {
            json_error(res, 400, "offset must be an integer");
            return;
        }
        limit = std::clamp(limit, 1, 100);
        offset = std::max(0, offset);

        auto topics = db_->list_learning_topics(true);
        std::set<std::string> public_topic_ids;
        for (const auto& topic : topics) public_topic_ids.insert(topic.id);

        auto courses = db_->list_learning_courses(std::nullopt, topic_id, std::nullopt,
                                                  query, limit + 100, offset, true);
        nlohmann::json rows = nlohmann::json::array();
        // A course in a draft topic is not publicly discoverable, even if the
        // course row itself was published before the topic was hidden.
        for (const auto& course : courses) {
            if (public_topic_ids.count(course.topic_id) == 0) continue;
            if (rows.size() >= static_cast<size_t>(limit)) break;
            rows.push_back(learning_course_public_json(course));
        }
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({{"courses", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/catalog/courses/([a-z0-9-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string slug = req.matches[1];
        if (!is_valid_learning_slug(slug)) {
            json_error(res, 404, "Course not found");
            return;
        }
        auto course = db_->get_learning_course_by_slug(slug);
        if (!course || course->status != "published") {
            json_error(res, 404, "Course not found");
            return;
        }
        auto topic = db_->get_learning_topic(course->topic_id);
        if (!topic || topic->status != "published") {
            json_error(res, 404, "Course not found");
            return;
        }
        auto curriculum = learning_curriculum_json(*db_, course->id, false, false, false, {});
        bool has_preview = false;
        for (const auto& module : curriculum["modules"]) {
            for (const auto& lesson : module["lessons"]) {
                if (lesson.value("is_preview", false)) has_preview = true;
            }
        }
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({
            {"course", learning_course_public_json(*course)},
            {"curriculum", curriculum},
            {"has_preview", has_preview},
            {"enrollment_required", true}
        }).dump(), "application/json");
    });

    svr.Get("/api/v3/catalog/topics", [this](const httplib::Request& req, httplib::Response& res) {
        auto topics = db_->list_learning_topics(true);
        auto courses = db_->list_learning_courses(std::nullopt, std::nullopt, std::nullopt,
                                                  "", 200, 0, true);
        std::map<std::string, int> counts;
        std::set<std::string> public_topic_ids;
        for (const auto& topic : topics) public_topic_ids.insert(topic.id);
        for (const auto& course : courses) {
            if (public_topic_ids.count(course.topic_id) == 0) continue;
            counts[course.topic_id]++;
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& topic : topics) {
            nlohmann::json j = topic.to_json();
            j["course_count"] = counts[topic.id];
            rows.push_back(j);
        }
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({{"topics", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/catalog/topics/([a-z0-9-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto topic = db_->get_learning_topic_by_slug(req.matches[1]);
        if (!topic || topic->status != "published") {
            json_error(res, 404, "Topic not found");
            return;
        }
        auto courses = db_->list_learning_courses(std::nullopt, topic->id, std::nullopt,
                                                  "", 200, 0, true);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& course : courses) rows.push_back(learning_course_public_json(course));
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({
            {"topic", topic->to_json()},
            {"courses", rows}
        }).dump(), "application/json");
    });

    svr.Get("/api/v3/catalog/learning-paths", [this](const httplib::Request& req, httplib::Response& res) {
        auto paths = db_->list_learning_paths(true);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& path : paths) {
            nlohmann::json j = path.to_json();
            nlohmann::json courses = nlohmann::json::array();
            for (const auto& course : db_->list_learning_path_courses(path.id, true)) {
                if (!learning_course_is_public(*db_, course)) continue;
                courses.push_back(learning_course_public_json(course));
            }
            j["courses"] = courses;
            rows.push_back(j);
        }
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({{"learning_paths", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/catalog/learning-paths/([a-z0-9-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto path = db_->get_learning_path_by_slug(req.matches[1]);
        if (!path || path->status != "published") {
            json_error(res, 404, "Learning path not found");
            return;
        }
        nlohmann::json courses = nlohmann::json::array();
        for (const auto& course : db_->list_learning_path_courses(path->id, true)) {
            if (!learning_course_is_public(*db_, course)) continue;
            courses.push_back(learning_course_public_json(course));
        }
        res.set_header("Cache-Control", "public, max-age=120");
        res.set_content(nlohmann::json({
            {"learning_path", path->to_json()},
            {"courses", courses}
        }).dump(), "application/json");
    });

    // ==========================================
    // Phase 3: learner registration and account privacy
    // ==========================================
    svr.Post("/api/v3/auth/register", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        const std::string ip = client_ip(req);
        if (!allow_request("learn-register:" + ip, 8, 300)) {
            res.set_header("Retry-After", "300");
            json_error(res, 429, "Too many registration attempts. Try again later.");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;

        std::string email;
        std::string password;
        try {
            email = utils::to_lower_copy(utils::trim_copy(body.value("email", "")));
            password = body.value("password", "");
        } catch (...) {
            json_error(res, 400, "Invalid email or password type");
            return;
        }
        if (!is_valid_email(email)) {
            json_error(res, 400, "Enter a valid email address");
            return;
        }
        if (password.size() < 12 || password.size() > 128) {
            json_error(res, 400, "Password must be between 12 and 128 characters");
            return;
        }

        User user;
        user.id = utils::generate_uuid();
        user.email = email;
        user.salt = utils::random_hex(16);
        user.password_hash = utils::hash_password(password, user.salt);
        user.role = "learner";
        user.created_at = utils::get_current_iso8601();
        user.updated_at = user.created_at;
        std::string error;
        if (!db_->create_user(user, error)) {
            if (error.find("already exists") != std::string::npos) {
                json_error(res, 409, error);
            } else {
                json_error(res, 500, error);
            }
            return;
        }

        const std::string token = utils::random_hex(32);
        const std::string expires_at = utils::iso8601_from_epoch(utils::utc_now_epoch() + 30 * 86400);
        if (!db_->create_session(token, user.id, expires_at)) {
            db_->delete_learning_user(user.id);
            json_error(res, 500, "Failed to create session");
            return;
        }

        AuditLog al;
        al.actor = user.email;
        al.action = "LEARNER_REGISTERED";
        al.entity_type = "auth";
        al.entity_id = user.id;
        al.details = "{}";
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(nlohmann::json({
            {"token", token},
            {"expires_at", expires_at},
            {"user", user.to_json()}
        }).dump(), "application/json");
    });

    // Public publisher/client application form. This is intentionally
    // unauthenticated and only stores the submission for administrator review;
    // it never creates an account, publisher, site, or campaign.
    svr.Post("/api/v3/partners/inquiries", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        const std::string ip = client_ip(req);
        // The origin bucket is shared by all visitors behind Cloudflare Tunnel,
        // so keep this high enough for a launch burst; per-client limits belong
        // in a Cloudflare WAF rule for this path.
        if (!allow_request("partner-inquiry:" + ip, 20, 600)) {
            res.set_header("Retry-After", "600");
            json_error(res, 429, "Too many applications. Try again later.");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;

        auto read_field = [&](const char* key, std::string& out, bool lower = false) -> bool {
            out.clear();
            if (!body.contains(key) || body[key].is_null()) return true;
            if (!body[key].is_string()) return false;
            out = utils::trim_copy(body[key].get<std::string>());
            if (lower) out = utils::to_lower_copy(out);
            return true;
        };

        std::string kind;
        std::string name;
        std::string email;
        std::string company;
        std::string website;
        std::string message;
        std::string honeypot;
        try {
            if (!read_field("kind", kind, true) ||
                !read_field("name", name) ||
                !read_field("email", email, true) ||
                !read_field("company", company) ||
                !read_field("website", website) ||
                !read_field("message", message) ||
                !read_field("fax", honeypot)) {
                json_error(res, 400, "Invalid field type");
                return;
            }
        } catch (...) {
            json_error(res, 400, "Invalid fields");
            return;
        }

        // A hidden "fax" field is never shown to a person. Bots that fill every
        // input get a normal-looking success response without a stored record.
        if (!honeypot.empty()) {
            res.status = 201;
            res.set_content(nlohmann::json({{"status", "received"}}).dump(), "application/json");
            return;
        }

        if (!is_valid_partner_inquiry_kind(kind)) {
            json_error(res, 400, "kind must be publisher or client");
            return;
        }
        if (name.size() < 2 || too_long(name, 120)) {
            json_error(res, 400, "name must be between 2 and 120 characters");
            return;
        }
        if (!is_valid_email(email)) {
            json_error(res, 400, "Enter a valid email address");
            return;
        }
        if (too_long(company, 160)) {
            json_error(res, 400, "company must be 160 characters or fewer");
            return;
        }
        if (too_long(website, 2048)) {
            json_error(res, 400, "website must be 2048 characters or fewer");
            return;
        }
        if (!website.empty() && !utils::is_safe_url(website)) {
            json_error(res, 400, "website must be a valid http or https URL");
            return;
        }
        if (kind == "publisher" && website.empty()) {
            json_error(res, 400, "website is required for publisher applications");
            return;
        }
        if (message.size() < 20 || too_long(message, 4000)) {
            json_error(res, 400, "message must be between 20 and 4000 characters");
            return;
        }

        PartnerInquiry inquiry;
        inquiry.kind = kind;
        inquiry.name = name;
        inquiry.email = email;
        inquiry.company = company;
        inquiry.website = website;
        inquiry.message = message;
        inquiry.status = "new";
        if (!db_->create_partner_inquiry(inquiry)) {
            json_error(res, 500, "Could not save the application");
            return;
        }

        AuditLog al;
        al.actor = email;
        al.action = "PARTNER_INQUIRY_RECEIVED";
        al.entity_type = "partner_inquiry";
        al.entity_id = inquiry.id;
        al.details = nlohmann::json({{"kind", kind}}).dump();
        db_->record_audit_log(al);

        res.status = 201;
        res.set_content(nlohmann::json({{"status", "received"}}).dump(), "application/json");
    });

    svr.Get("/api/v3/learning/me/export", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (!allow_request("learn-export:" + user->id, 5, 300)) {
            res.set_header("Retry-After", "300");
            json_error(res, 429, "Too many export requests. Try again later.");
            return;
        }

        nlohmann::json enrollments = nlohmann::json::array();
        for (const auto& enrollment : db_->list_learning_enrollments(user->id)) {
            nlohmann::json j = learning_enrollment_summary_json(*db_, enrollment);
            nlohmann::json completed = nlohmann::json::array();
            for (const auto& lesson_id : db_->list_completed_lesson_ids(enrollment.id)) {
                completed.push_back(lesson_id);
            }
            j["completed_lesson_ids"] = completed;
            enrollments.push_back(j);
        }
        nlohmann::json attempts = nlohmann::json::array();
        for (const auto& attempt : db_->list_quiz_attempts(user->id)) {
            attempts.push_back(attempt.to_json());
        }

        res.set_header("Content-Disposition", "attachment; filename=\"adcontrol-learning-export.json\"");
        res.set_content(nlohmann::json({
            {"exported_at", utils::get_current_iso8601()},
            {"user", user->to_json()},
            {"enrollments", enrollments},
            {"quiz_attempts", attempts},
            {"note", "This export contains your learning account data. It does not include other learners' data."}
        }).dump(2), "application/json");
    });

    svr.Delete("/api/v3/learning/me", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (user->role != "learner") {
            json_error(res, 403, "Only learner accounts can self-delete; ask an administrator to remove staff accounts");
            return;
        }
        db_->delete_user_sessions(user->id);
        if (!db_->delete_learning_user(user->id)) {
            json_error(res, 404, "Account not found");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "LEARNER_ACCOUNT_DELETED";
        al.entity_type = "auth";
        al.entity_id = user->id;
        al.details = "{}";
        db_->record_audit_log(al);
        res.status = 204;
    });

    // ==========================================
    // Phase 3: enrollment, My Learning, progress and quizzes
    // ==========================================
    svr.Post(R"(/api/v3/learning/courses/([a-z0-9-]+)/enroll)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (!allow_request("learn-enroll:" + user->id, 30, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto course = db_->get_learning_course_by_slug(req.matches[1]);
        if (!course || !learning_course_is_public(*db_, *course)) {
            json_error(res, 404, "Course not found");
            return;
        }
        Enrollment enrollment;
        bool created = false;
        std::string error;
        if (!db_->enroll_in_learning_course(user->id, course->id, enrollment, created, error)) {
            json_error(res, 500, error);
            return;
        }
        if (created) {
            AuditLog al;
            al.actor = user->email;
            al.action = "ENROLL_LEARNING_COURSE";
            al.entity_type = "learning_course";
            al.entity_id = course->id;
            al.details = nlohmann::json({{"course_slug", course->slug}}).dump();
            db_->record_audit_log(al);
        }
        res.status = created ? 201 : 200;
        res.set_content(nlohmann::json({
            {"created", created},
            {"enrollment", enrollment.to_json()},
            {"course", learning_course_public_json(*course)}
        }).dump(), "application/json");
    });

    svr.Get("/api/v3/learning/my-learning", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (!allow_request("learn-my:" + user->id, 240, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& enrollment : db_->list_learning_enrollments(user->id)) {
            nlohmann::json j = enrollment.to_json();
            auto course = db_->get_learning_course(enrollment.course_id);
            if (course) {
                j["course"] = learning_course_public_json(*course);
                j["available"] = course->status == "published";
                j["last_reviewed_at"] = course->last_reviewed_at;
            } else {
                j["course"] = nullptr;
                j["available"] = false;
            }
            rows.push_back(j);
        }
        res.set_content(nlohmann::json({{"enrollments", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/learning/courses/([a-z0-9-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        auto course = db_->get_learning_course_by_slug(req.matches[1]);
        if (!course || course->status != "published") {
            json_error(res, 404, "Course not found");
            return;
        }
        auto enrollment = db_->get_learning_enrollment(user->id, course->id);
        if (!enrollment) {
            json_error(res, 403, "Enroll in this course to access its lessons");
            return;
        }
        auto completed_ids = db_->list_completed_lesson_ids(enrollment->id);
        std::set<std::string> completed_set(completed_ids.begin(), completed_ids.end());
        auto curriculum = learning_curriculum_json(*db_, course->id, true, true, false, completed_set);
        nlohmann::json resources = nlohmann::json::array();
        for (const auto& resource : db_->list_learning_resources(course->id)) {
            resources.push_back(resource.to_json());
        }
        nlohmann::json attempts = nlohmann::json::array();
        for (const auto& attempt : db_->list_quiz_attempts(user->id, course->id)) {
            attempts.push_back(attempt.to_json());
        }
        res.set_content(nlohmann::json({
            {"course", learning_course_public_json(*course)},
            {"curriculum", curriculum},
            {"resources", resources},
            {"enrollment", enrollment->to_json()},
            {"completed_lesson_ids", completed_ids},
            {"quiz_attempts", attempts}
        }).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/learning/lessons/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || course->status != "published") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto enrollment = db_->get_learning_enrollment(user->id, course->id);
        if (!enrollment) {
            json_error(res, 403, "Enroll in this course to access its lessons");
            return;
        }
        auto completed_ids = db_->list_completed_lesson_ids(enrollment->id);
        std::set<std::string> completed_set(completed_ids.begin(), completed_ids.end());
        auto quiz = db_->get_learning_quiz_for_lesson(lesson->id, false);
        nlohmann::json lesson_json = learning_lesson_visible_json(
            *lesson, true, completed_set.count(lesson->id) > 0, nullptr, false);
        nlohmann::json quiz_json = quiz ? quiz->to_json(false) : nlohmann::json(nullptr);
        nlohmann::json resources = nlohmann::json::array();
        for (const auto& resource : db_->list_learning_resources(course->id)) {
            if (resource.lesson_id == lesson->id) resources.push_back(resource.to_json());
        }
        nlohmann::json attempts = nlohmann::json::array();
        for (const auto& attempt : db_->list_quiz_attempts(user->id, course->id, lesson->id)) {
            attempts.push_back(attempt.to_json());
        }
        auto module = db_->get_learning_module(lesson->module_id);
        res.set_content(nlohmann::json({
            {"lesson", lesson_json},
            {"quiz", quiz_json},
            {"resources", resources},
            {"module", module ? module->to_json() : nlohmann::json(nullptr)},
            {"course", learning_course_public_json(*course)},
            {"enrollment", enrollment->to_json()},
            {"quiz_attempts", attempts},
            {"completed", completed_set.count(lesson->id) > 0}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v3/learning/lessons/([^/]+)/complete)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (!allow_request("learn-complete:" + user->id, 120, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || course->status != "published") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto enrollment = db_->get_learning_enrollment(user->id, course->id);
        if (!enrollment) {
            json_error(res, 403, "Enroll in this course before recording progress");
            return;
        }
        Enrollment updated;
        std::string error;
        if (!db_->complete_learning_lesson(enrollment->id, course->id, lesson->id, updated, error)) {
            json_error(res, 400, error);
            return;
        }
        res.set_content(nlohmann::json({
            {"completed", true},
            {"enrollment", updated.to_json()}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v3/learning/lessons/([^/]+)/quiz/attempt)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning(req, res);
        if (!user) return;
        if (!allow_request("learn-quiz:" + user->id, 30, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || course->status != "published") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto enrollment = db_->get_learning_enrollment(user->id, course->id);
        if (!enrollment) {
            json_error(res, 403, "Enroll in this course before taking its quiz");
            return;
        }
        auto quiz = db_->get_learning_quiz_for_lesson(lesson->id, true);
        if (!quiz || quiz->questions.empty()) {
            json_error(res, 404, "This lesson has no quiz");
            return;
        }

        nlohmann::json body;
        if (!parse_json_body(req, res, body, 128 * 1024)) return;
        if (!body.contains("answers") || !body["answers"].is_array()) {
            json_error(res, 400, "answers must be an array");
            return;
        }
        if (body["answers"].size() > 100) {
            json_error(res, 400, "Too many answers");
            return;
        }
        std::map<std::string, std::string> selected;
        for (const auto& answer : body["answers"]) {
            if (!answer.is_object() || !answer.contains("question_id") ||
                !answer.contains("option_id") || !answer["question_id"].is_string() ||
                !answer["option_id"].is_string()) {
                json_error(res, 400, "Each answer needs question_id and option_id strings");
                return;
            }
            const std::string question_id = answer["question_id"].get<std::string>();
            const std::string option_id = answer["option_id"].get<std::string>();
            if (selected.count(question_id)) {
                json_error(res, 400, "Duplicate question_id in answers");
                return;
            }
            bool valid = false;
            for (const auto& question : quiz->questions) {
                if (question.id != question_id) continue;
                for (const auto& option : question.options) {
                    if (option.id == option_id) {
                        valid = true;
                        break;
                    }
                }
                break;
            }
            if (!valid) {
                json_error(res, 400, "An answer references an unknown question or option");
                return;
            }
            selected[question_id] = option_id;
        }

        int correct_count = 0;
        nlohmann::json results = nlohmann::json::array();
        for (const auto& question : quiz->questions) {
            std::string correct_option_id;
            for (const auto& option : question.options) {
                if (option.is_correct) {
                    correct_option_id = option.id;
                    break;
                }
            }
            std::string chosen;
            auto it = selected.find(question.id);
            if (it != selected.end()) chosen = it->second;
            const bool is_correct = !correct_option_id.empty() && chosen == correct_option_id;
            if (is_correct) ++correct_count;
            results.push_back({
                {"question_id", question.id},
                {"selected_option_id", chosen},
                {"correct_option_id", correct_option_id},
                {"is_correct", is_correct},
                {"explanation", question.explanation}
            });
        }
        const int total = static_cast<int>(quiz->questions.size());
        const int score = (correct_count * 100) / total;
        const bool passed = score >= quiz->passing_score;

        QuizAttempt attempt;
        attempt.id = utils::generate_uuid();
        attempt.enrollment_id = enrollment->id;
        attempt.user_id = user->id;
        attempt.course_id = course->id;
        attempt.lesson_id = lesson->id;
        attempt.quiz_id = quiz->id;
        attempt.score = score;
        attempt.passed = passed;
        attempt.correct_count = correct_count;
        attempt.total_questions = total;
        attempt.attempted_at = utils::get_current_iso8601();
        if (!db_->record_quiz_attempt(attempt)) {
            json_error(res, 500, "Could not record quiz attempt");
            return;
        }
        res.set_content(nlohmann::json({
            {"attempt", attempt.to_json()},
            {"score", score},
            {"passed", passed},
            {"passing_score", quiz->passing_score},
            {"correct_count", correct_count},
            {"total_questions", total},
            {"results", results},
            {"note", "Explanations and the answer key are returned only after a graded attempt."}
        }).dump(), "application/json");
    });

    // ==========================================
    // Phase 3: authoring, review, publication and revision history
    // ==========================================
    svr.Get("/api/v3/author/topics", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& topic : db_->list_learning_topics(false)) rows.push_back(topic.to_json());
        res.set_content(nlohmann::json({{"topics", rows}}).dump(), "application/json");
    });

    svr.Get("/api/v3/author/courses", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        std::optional<std::string> status;
        if (req.has_param("status") && !req.get_param_value("status").empty()) {
            std::string value = req.get_param_value("status");
            if (value != "draft" && value != "in_review" && value != "approved" &&
                value != "published" && value != "archived") {
                json_error(res, 400, "Invalid status filter");
                return;
            }
            status = value;
        }
        std::string query = req.has_param("q")
            ? utils::trim_copy(req.get_param_value("q")) : "";
        if (query.size() > 100) {
            json_error(res, 400, "q is too long");
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
        limit = std::clamp(limit, 1, 200);
        offset = std::max(0, offset);
        std::optional<std::string> author_id;
        if (user->role == "instructor") author_id = user->id;

        auto courses = db_->list_learning_courses(status, std::nullopt, author_id,
                                                  query, limit, offset, false);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& course : courses) rows.push_back(course.to_json());
        res.set_content(nlohmann::json({{"courses", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v3/author/courses", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        if (!allow_request("learn-author:" + user->id, 120, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 256 * 1024)) return;

        LearningCourse course;
        try {
            course.slug = body.value("slug", "");
            course.title = body.value("title", "");
            course.summary = body.value("summary", "");
            course.description_md = body.value("description_md", "");
            course.level = body.value("level", "beginner");
            course.duration_minutes = body.value("duration_minutes", 60);
            course.topic_id = body.value("topic_id", "");
            std::string topic_slug = body.value("topic_slug", "");
            if (course.topic_id.empty() && !topic_slug.empty()) {
                auto topic = db_->get_learning_topic_by_slug(
                    utils::to_lower_copy(utils::trim_copy(topic_slug)));
                if (topic) course.topic_id = topic->id;
            }
        } catch (...) {
            json_error(res, 400, "Invalid course field type");
            return;
        }
        std::string err;
        if (body.contains("outcomes") &&
            !read_string_array(body, "outcomes", course.outcomes, err, 20)) {
            json_error(res, 400, err);
            return;
        }
        if (!validate_learning_course_fields(course, err)) {
            json_error(res, 400, err);
            return;
        }
        auto topic = db_->get_learning_topic(course.topic_id);
        if (!topic) {
            json_error(res, 400, "topic_id does not reference an existing topic");
            return;
        }
        course.id = utils::generate_uuid();
        course.author_id = user->id;
        course.status = "draft";
        course.version = 0;
        course.created_at = utils::get_current_iso8601();
        course.updated_at = course.created_at;
        if (!db_->create_learning_course(course, err)) {
            json_error(res, err.find("already exists") != std::string::npos ? 409 : 500, err);
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_LEARNING_COURSE";
        al.entity_type = "learning_course";
        al.entity_id = course.id;
        al.details = nlohmann::json({{"slug", course.slug}, {"title", course.title}}).dump();
        db_->record_audit_log(al);
        res.status = 201;
        res.set_content(learning_authoring_course_json(*db_, course).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/author/courses/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        auto payload = learning_authoring_course_json(*db_, *course);
        nlohmann::json revisions = nlohmann::json::array();
        for (const auto& revision : db_->list_course_revisions(course->id)) {
            revisions.push_back({
                {"id", revision.id},
                {"version", revision.version},
                {"created_by", revision.created_by},
                {"created_by_email", revision.created_by_email},
                {"note", revision.note},
                {"created_at", revision.created_at}
            });
        }
        payload["revisions"] = revisions;
        res.set_content(payload.dump(), "application/json");
    });

    svr.Patch(R"(/api/v3/author/courses/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        if (!allow_request("learn-author:" + user->id, 120, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Only draft courses can be edited. Move the course to draft before editing published content.");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 256 * 1024)) return;

        LearningCourse updated = *course;
        try {
            if (body.contains("slug")) updated.slug = body.value("slug", "");
            if (body.contains("title")) updated.title = body.value("title", "");
            if (body.contains("summary")) updated.summary = body.value("summary", "");
            if (body.contains("description_md")) updated.description_md = body.value("description_md", "");
            if (body.contains("level")) updated.level = body.value("level", "");
            if (body.contains("duration_minutes")) updated.duration_minutes = body.value("duration_minutes", 0);
            if (body.contains("topic_id")) updated.topic_id = body.value("topic_id", "");
            if (body.contains("topic_slug")) {
                auto topic = db_->get_learning_topic_by_slug(
                    utils::to_lower_copy(utils::trim_copy(body.value("topic_slug", ""))));
                if (topic) updated.topic_id = topic->id;
                else if (!body.contains("topic_id")) {
                    json_error(res, 400, "topic_slug does not reference an existing topic");
                    return;
                }
            }
        } catch (...) {
            json_error(res, 400, "Invalid course field type");
            return;
        }
        std::string err;
        if (body.contains("outcomes") &&
            !read_string_array(body, "outcomes", updated.outcomes, err, 20)) {
            json_error(res, 400, err);
            return;
        }
        if (!validate_learning_course_fields(updated, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!db_->get_learning_topic(updated.topic_id)) {
            json_error(res, 400, "topic_id does not reference an existing topic");
            return;
        }
        updated.updated_at = utils::get_current_iso8601();
        if (!db_->update_learning_course(updated)) {
            json_error(res, 500, "Could not update course");
            return;
        }
        auto reloaded = db_->get_learning_course(updated.id);
        AuditLog al;
        al.actor = user->email;
        al.action = "UPDATE_LEARNING_COURSE";
        al.entity_type = "learning_course";
        al.entity_id = updated.id;
        al.details = nlohmann::json({{"slug", updated.slug}}).dump();
        db_->record_audit_log(al);
        res.set_content(learning_authoring_course_json(*db_, reloaded ? *reloaded : updated).dump(),
                        "application/json");
    });

    svr.Post(R"(/api/v3/author/courses/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        if (!allow_request("learn-author:" + user->id, 120, 60)) {
            res.set_header("Retry-After", "60");
            json_error(res, 429, "Rate limit exceeded");
            return;
        }
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 16 * 1024)) return;
        std::string target;
        std::string note;
        try {
            target = body.value("status", "");
            note = utils::trim_copy(body.value("note", ""));
        } catch (...) {
            json_error(res, 400, "Invalid status or note type");
            return;
        }
        if (note.size() > 500 || contains_unsafe_learning_markup(note)) {
            json_error(res, 400, "note must be plain text of at most 500 characters");
            return;
        }
        if (target != "draft" && target != "in_review" && target != "approved" &&
            target != "published" && target != "archived") {
            json_error(res, 400, "status must be draft, in_review, approved, published, or archived");
            return;
        }
        const std::string current = course->status;
        if (target == current) {
            json_error(res, 409, "Course is already in that status");
            return;
        }
        const bool editor = is_learning_editor_user(*user);
        bool allowed = false;
        if (target == "in_review") {
            allowed = current == "draft";
        } else if (target == "approved") {
            allowed = editor && current == "in_review";
        } else if (target == "published") {
            allowed = editor && current == "approved";
        } else if (target == "draft") {
            allowed = current == "in_review" ||
                      (editor && (current == "approved" || current == "published" ||
                                  current == "archived"));
        } else if (target == "archived") {
            allowed = editor ? current != "archived" : current == "draft";
        }
        if (!allowed) {
            json_error(res, 403, "Forbidden: that status transition is not allowed for your role");
            return;
        }

        std::string err;
        if (target == "approved" || target == "published") {
            if (!learning_course_ready_to_publish(*db_, course->id, err)) {
                json_error(res, 400, err);
                return;
            }
        }

        const std::string now = utils::get_current_iso8601();
        if (target == "published") {
            const int next_version = course->version + 1;
            LearningCourse versioned = *course;
            versioned.version = next_version;
            versioned.updated_at = now;
            if (!db_->update_learning_course(versioned)) {
                json_error(res, 500, "Could not version course");
                return;
            }
            CourseRevision revision;
            std::string revision_note = note.empty()
                ? ("Published version " + std::to_string(next_version)) : note;
            if (!db_->create_course_revision(course->id, user->id, revision_note, revision, err)) {
                // Roll the version back so the authoring UI does not show a
                // version that was never published.
                LearningCourse rollback = *course;
                rollback.updated_at = now;
                db_->update_learning_course(rollback);
                json_error(res, 500, err);
                return;
            }
        }

        if (!db_->set_learning_course_status(course->id, target, user->id, now)) {
            json_error(res, 500, "Could not change course status");
            return;
        }
        auto reloaded = db_->get_learning_course(course->id);
        AuditLog al;
        al.actor = user->email;
        al.action = "LEARNING_COURSE_STATUS";
        al.entity_type = "learning_course";
        al.entity_id = course->id;
        al.details = nlohmann::json({{"from", current}, {"to", target},
                                     {"note", note}}).dump();
        db_->record_audit_log(al);
        res.set_content(nlohmann::json({
            {"course", reloaded ? reloaded->to_json() : course->to_json()},
            {"status", target}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/v3/author/courses/([^/]+)/modules)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        LearningModule module;
        std::string err;
        try {
            module.title = body.value("title", "");
            module.summary = body.value("summary", "");
            module.sort_order = body.value("sort_order", 0);
        } catch (...) {
            json_error(res, 400, "Invalid module field type");
            return;
        }
        module.title = utils::trim_copy(module.title);
        module.summary = utils::trim_copy(module.summary);
        if (!validate_learning_plain_text(module.title, 3, 160, "module title", true, err) ||
            !validate_learning_plain_text(module.summary, 0, 500, "module summary", false, err) ||
            module.sort_order < 0 || module.sort_order > 100000) {
            if (err.empty()) err = "sort_order must be between 0 and 100000";
            json_error(res, 400, err);
            return;
        }
        module.id = utils::generate_uuid();
        module.course_id = course->id;
        module.status = "active";
        module.created_at = utils::get_current_iso8601();
        module.updated_at = module.created_at;
        if (!db_->create_learning_module(module, err)) {
            json_error(res, 500, err);
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_LEARNING_MODULE";
        al.entity_type = "learning_module";
        al.entity_id = module.id;
        al.details = nlohmann::json({{"course_id", course->id}, {"title", module.title}}).dump();
        db_->record_audit_log(al);
        res.status = 201;
        res.set_content(nlohmann::json({{"module", module.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v3/author/modules/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto module = db_->get_learning_module(req.matches[1]);
        if (!module || module->status != "active") {
            json_error(res, 404, "Module not found");
            return;
        }
        auto course = db_->get_learning_course(module->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        LearningModule updated = *module;
        std::string err;
        try {
            if (body.contains("title")) updated.title = body.value("title", "");
            if (body.contains("summary")) updated.summary = body.value("summary", "");
            if (body.contains("sort_order")) updated.sort_order = body.value("sort_order", 0);
        } catch (...) {
            json_error(res, 400, "Invalid module field type");
            return;
        }
        updated.title = utils::trim_copy(updated.title);
        updated.summary = utils::trim_copy(updated.summary);
        if (!validate_learning_plain_text(updated.title, 3, 160, "module title", true, err) ||
            !validate_learning_plain_text(updated.summary, 0, 500, "module summary", false, err) ||
            updated.sort_order < 0 || updated.sort_order > 100000) {
            if (err.empty()) err = "sort_order must be between 0 and 100000";
            json_error(res, 400, err);
            return;
        }
        updated.updated_at = utils::get_current_iso8601();
        if (!db_->update_learning_module(updated)) {
            json_error(res, 500, "Could not update module");
            return;
        }
        res.set_content(nlohmann::json({{"module", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v3/author/modules/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto module = db_->get_learning_module(req.matches[1]);
        if (!module || module->status != "active") {
            json_error(res, 404, "Module not found");
            return;
        }
        auto course = db_->get_learning_course(module->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        if (!db_->archive_learning_module(module->id)) {
            json_error(res, 404, "Module not found");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "ARCHIVE_LEARNING_MODULE";
        al.entity_type = "learning_module";
        al.entity_id = module->id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        res.status = 204;
    });

    svr.Post(R"(/api/v3/author/courses/([^/]+)/lessons)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 256 * 1024)) return;
        LearningLesson lesson;
        std::string err;
        try {
            lesson.module_id = body.value("module_id", "");
            lesson.slug = body.value("slug", "");
            lesson.title = body.value("title", "");
            lesson.content_md = body.value("content_md", "");
            lesson.estimated_minutes = body.value("estimated_minutes", 10);
            lesson.sort_order = body.value("sort_order", 0);
            lesson.is_preview = body.value("is_preview", false);
            lesson.video_provider = utils::to_lower_copy(body.value("video_provider", ""));
            lesson.video_id = body.value("video_id", "");
        } catch (...) {
            json_error(res, 400, "Invalid lesson field type");
            return;
        }
        auto module = db_->get_learning_module(lesson.module_id);
        if (!module || module->course_id != course->id || module->status != "active") {
            json_error(res, 400, "module_id must reference an active module in this course");
            return;
        }
        lesson.slug = utils::to_lower_copy(utils::trim_copy(lesson.slug));
        lesson.title = utils::trim_copy(lesson.title);
        lesson.video_provider = utils::to_lower_copy(utils::trim_copy(lesson.video_provider));
        lesson.video_id = utils::trim_copy(lesson.video_id);
        if (!validate_learning_lesson_fields(lesson, err)) {
            json_error(res, 400, err);
            return;
        }
        lesson.id = utils::generate_uuid();
        lesson.course_id = course->id;
        lesson.status = "active";
        lesson.created_at = utils::get_current_iso8601();
        lesson.updated_at = lesson.created_at;
        if (!db_->create_learning_lesson(lesson, err)) {
            json_error(res, err.find("already exists") != std::string::npos ? 409 : 500, err);
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_LEARNING_LESSON";
        al.entity_type = "learning_lesson";
        al.entity_id = lesson.id;
        al.details = nlohmann::json({{"course_id", course->id}, {"slug", lesson.slug}}).dump();
        db_->record_audit_log(al);
        res.status = 201;
        res.set_content(nlohmann::json({{"lesson", lesson.to_json()}}).dump(), "application/json");
    });

    svr.Patch(R"(/api/v3/author/lessons/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 256 * 1024)) return;
        LearningLesson updated = *lesson;
        std::string err;
        try {
            if (body.contains("slug")) updated.slug = body.value("slug", "");
            if (body.contains("title")) updated.title = body.value("title", "");
            if (body.contains("content_md")) updated.content_md = body.value("content_md", "");
            if (body.contains("estimated_minutes")) updated.estimated_minutes = body.value("estimated_minutes", 0);
            if (body.contains("sort_order")) updated.sort_order = body.value("sort_order", 0);
            if (body.contains("is_preview")) {
                if (!body["is_preview"].is_boolean()) {
                    json_error(res, 400, "is_preview must be a boolean");
                    return;
                }
                updated.is_preview = body["is_preview"].get<bool>();
            }
            if (body.contains("video_provider")) updated.video_provider = utils::to_lower_copy(body.value("video_provider", ""));
            if (body.contains("video_id")) updated.video_id = body.value("video_id", "");
        } catch (...) {
            json_error(res, 400, "Invalid lesson field type");
            return;
        }
        updated.slug = utils::to_lower_copy(utils::trim_copy(updated.slug));
        updated.title = utils::trim_copy(updated.title);
        updated.video_provider = utils::to_lower_copy(utils::trim_copy(updated.video_provider));
        updated.video_id = utils::trim_copy(updated.video_id);
        if (!validate_learning_lesson_fields(updated, err)) {
            json_error(res, 400, err);
            return;
        }
        updated.updated_at = utils::get_current_iso8601();
        if (!db_->update_learning_lesson(updated)) {
            json_error(res, 500, "Could not update lesson");
            return;
        }
        res.set_content(nlohmann::json({{"lesson", updated.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v3/author/lessons/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its curriculum");
            return;
        }
        if (!db_->archive_learning_lesson(lesson->id)) {
            json_error(res, 404, "Lesson not found");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "ARCHIVE_LEARNING_LESSON";
        al.entity_type = "learning_lesson";
        al.entity_id = lesson->id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        res.status = 204;
    });

    svr.Post(R"(/api/v3/author/courses/([^/]+)/resources)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its resources");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        LearningResource resource;
        std::string err;
        try {
            resource.course_id = course->id;
            resource.lesson_id = body.value("lesson_id", "");
            resource.title = utils::trim_copy(body.value("title", ""));
            resource.description = utils::trim_copy(body.value("description", ""));
            resource.url = utils::trim_copy(body.value("url", ""));
            resource.kind = utils::to_lower_copy(body.value("kind", "link"));
            resource.sort_order = body.value("sort_order", 0);
        } catch (...) {
            json_error(res, 400, "Invalid resource field type");
            return;
        }
        if (!validate_learning_plain_text(resource.title, 3, 200, "resource title", true, err) ||
            !validate_learning_plain_text(resource.description, 0, 1000, "resource description", false, err)) {
            json_error(res, 400, err);
            return;
        }
        if (resource.kind != "link" && resource.kind != "download" && resource.kind != "tool") {
            json_error(res, 400, "kind must be link, download, or tool");
            return;
        }
        if (resource.sort_order < 0 || resource.sort_order > 100000) {
            json_error(res, 400, "sort_order must be between 0 and 100000");
            return;
        }
        if (!validate_learning_resource_url(resource.url, err)) {
            json_error(res, 400, err);
            return;
        }
        if (!resource.lesson_id.empty()) {
            auto lesson = db_->get_learning_lesson(resource.lesson_id);
            if (!lesson || lesson->course_id != course->id) {
                json_error(res, 400, "lesson_id must reference a lesson in this course");
                return;
            }
        }
        resource.id = utils::generate_uuid();
        resource.created_at = utils::get_current_iso8601();
        if (!db_->create_learning_resource(resource, err)) {
            json_error(res, 500, err);
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "CREATE_LEARNING_RESOURCE";
        al.entity_type = "learning_resource";
        al.entity_id = resource.id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        res.status = 201;
        res.set_content(nlohmann::json({{"resource", resource.to_json()}}).dump(), "application/json");
    });

    svr.Delete(R"(/api/v3/author/resources/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto resource = db_->get_learning_resource(req.matches[1]);
        if (!resource) {
            json_error(res, 404, "Resource not found");
            return;
        }
        auto course = db_->get_learning_course(resource->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its resources");
            return;
        }
        if (!db_->delete_learning_resource(resource->id)) {
            json_error(res, 404, "Resource not found");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_LEARNING_RESOURCE";
        al.entity_type = "learning_resource";
        al.entity_id = resource->id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        res.status = 204;
    });

    svr.Get(R"(/api/v3/author/lessons/([^/]+)/quiz)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        auto quiz = db_->get_learning_quiz_for_lesson(lesson->id, true);
        res.set_content(nlohmann::json({
            {"quiz", quiz ? quiz->to_json(true) : nlohmann::json(nullptr)}
        }).dump(), "application/json");
    });

    svr.Put(R"(/api/v3/author/lessons/([^/]+)/quiz)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its quiz");
            return;
        }
        nlohmann::json body;
        if (!parse_json_body(req, res, body, 256 * 1024)) return;
        LessonQuiz quiz;
        std::string err;
        try {
            quiz.title = utils::trim_copy(body.value("title", "Knowledge check"));
            quiz.passing_score = body.value("passing_score", 70);
        } catch (...) {
            json_error(res, 400, "Invalid quiz field type");
            return;
        }
        if (!validate_learning_plain_text(quiz.title, 3, 160, "quiz title", true, err) ||
            quiz.passing_score < 0 || quiz.passing_score > 100) {
            if (err.empty()) err = "passing_score must be between 0 and 100";
            json_error(res, 400, err);
            return;
        }
        if (!body.contains("questions") || !body["questions"].is_array() ||
            body["questions"].empty() || body["questions"].size() > 50) {
            json_error(res, 400, "questions must be an array of 1-50 questions");
            return;
        }
        int q_index = 0;
        for (const auto& raw_question : body["questions"]) {
            if (!raw_question.is_object()) {
                json_error(res, 400, "Each question must be an object");
                return;
            }
            QuizQuestion question;
            try {
                question.prompt = utils::trim_copy(raw_question.value("prompt", ""));
                question.explanation = utils::trim_copy(raw_question.value("explanation", ""));
            } catch (...) {
                json_error(res, 400, "Invalid question field type");
                return;
            }
            if (!validate_learning_plain_text(question.prompt, 3, 1000,
                                              "question prompt", true, err) ||
                !validate_learning_plain_text(question.explanation, 0, 2000,
                                              "question explanation", false, err)) {
                json_error(res, 400, err);
                return;
            }
            if (!raw_question.contains("options") || !raw_question["options"].is_array() ||
                raw_question["options"].size() < 2 || raw_question["options"].size() > 10) {
                json_error(res, 400, "Each question needs 2-10 options");
                return;
            }
            int o_index = 0;
            int correct_count = 0;
            for (const auto& raw_option : raw_question["options"]) {
                if (!raw_option.is_object()) {
                    json_error(res, 400, "Each option must be an object");
                    return;
                }
                QuizOption option;
                try {
                    option.text = utils::trim_copy(raw_option.value("text", ""));
                    option.is_correct = raw_option.value("is_correct", false);
                } catch (...) {
                    json_error(res, 400, "Invalid option field type");
                    return;
                }
                if (!validate_learning_plain_text(option.text, 1, 500,
                                                  "option text", true, err)) {
                    json_error(res, 400, err);
                    return;
                }
                if (option.is_correct) ++correct_count;
                option.id = utils::generate_uuid();
                option.question_id = ""; // assigned by the DB helper
                option.sort_order = ++o_index;
                question.options.push_back(option);
            }
            if (correct_count != 1) {
                json_error(res, 400, "Each question must have exactly one correct option");
                return;
            }
            question.id = utils::generate_uuid();
            question.quiz_id = "";
            question.sort_order = ++q_index;
            quiz.questions.push_back(question);
        }
        quiz.lesson_id = lesson->id;
        quiz.course_id = course->id;
        quiz.id = "";
        if (!db_->replace_learning_quiz(lesson->id, course->id, quiz, err)) {
            json_error(res, 500, err);
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "REPLACE_LEARNING_QUIZ";
        al.entity_type = "learning_lesson";
        al.entity_id = lesson->id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        auto saved = db_->get_learning_quiz_for_lesson(lesson->id, true);
        res.set_content(nlohmann::json({
            {"quiz", saved ? saved->to_json(true) : nlohmann::json(nullptr)}
        }).dump(), "application/json");
    });

    svr.Delete(R"(/api/v3/author/lessons/([^/]+)/quiz)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto lesson = db_->get_learning_lesson(req.matches[1]);
        if (!lesson || lesson->status != "active") {
            json_error(res, 404, "Lesson not found");
            return;
        }
        auto course = db_->get_learning_course(lesson->course_id);
        if (!course || !can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        if (course->status != "draft") {
            json_error(res, 409, "Move the course to draft before editing its quiz");
            return;
        }
        if (!db_->delete_learning_quiz(lesson->id)) {
            json_error(res, 404, "Quiz not found");
            return;
        }
        AuditLog al;
        al.actor = user->email;
        al.action = "DELETE_LEARNING_QUIZ";
        al.entity_type = "learning_lesson";
        al.entity_id = lesson->id;
        al.details = nlohmann::json({{"course_id", course->id}}).dump();
        db_->record_audit_log(al);
        res.status = 204;
    });

    svr.Get(R"(/api/v3/author/courses/([^/]+)/revisions)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& revision : db_->list_course_revisions(course->id)) {
            rows.push_back({
                {"id", revision.id},
                {"version", revision.version},
                {"created_by", revision.created_by},
                {"created_by_email", revision.created_by_email},
                {"note", revision.note},
                {"created_at", revision.created_at}
            });
        }
        res.set_content(nlohmann::json({{"revisions", rows}}).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/author/courses/([^/]+)/revisions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only manage your own courses");
            return;
        }
        auto revision = db_->get_course_revision(req.matches[2]);
        if (!revision || revision->course_id != course->id) {
            json_error(res, 404, "Revision not found");
            return;
        }
        nlohmann::json snapshot = nlohmann::json::object();
        try {
            snapshot = nlohmann::json::parse(revision->snapshot);
        } catch (...) {
            json_error(res, 500, "Revision snapshot is unavailable");
            return;
        }
        res.set_content(nlohmann::json({
            {"id", revision->id},
            {"course_id", revision->course_id},
            {"version", revision->version},
            {"created_by", revision->created_by},
            {"created_by_email", revision->created_by_email},
            {"note", revision->note},
            {"created_at", revision->created_at},
            {"snapshot", snapshot}
        }).dump(), "application/json");
    });

    svr.Get(R"(/api/v3/author/courses/([^/]+)/analytics)", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_learning_staff(req, res);
        if (!user) return;
        auto course = db_->get_learning_course(req.matches[1]);
        if (!course) {
            json_error(res, 404, "Course not found");
            return;
        }
        if (!can_manage_learning_course(*user, *course)) {
            json_error(res, 403, "Forbidden: you can only view analytics for your own courses");
            return;
        }
        res.set_content(db_->get_course_learning_analytics(course->id).dump(), "application/json");
    });

    // ==========================================
    // Phase 3: administrator learning operations
    // ==========================================
    svr.Get("/api/v3/admin/learning/users", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto user = authenticate_admin(req, res);
        if (!user) return;
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& learning_user : db_->list_learning_users()) {
            rows.push_back(learning_user.to_json());
        }
        res.set_content(nlohmann::json({{"users", rows}}).dump(), "application/json");
    });

    svr.Post("/api/v3/admin/learning/users", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        nlohmann::json body;
        if (!parse_json_body(req, res, body)) return;
        std::string email;
        std::string password;
        std::string role;
        try {
            email = utils::to_lower_copy(utils::trim_copy(body.value("email", "")));
            password = body.value("password", "");
            role = utils::to_lower_copy(utils::trim_copy(body.value("role", "")));
        } catch (...) {
            json_error(res, 400, "Invalid staff field type");
            return;
        }
        if (!is_valid_email(email)) {
            json_error(res, 400, "Enter a valid email address");
            return;
        }
        if (password.size() < 12 || password.size() > 128) {
            json_error(res, 400, "Password must be between 12 and 128 characters");
            return;
        }
        if (role != "instructor" && role != "editor") {
            json_error(res, 400, "role must be instructor or editor");
            return;
        }
        User staff;
        staff.id = utils::generate_uuid();
        staff.email = email;
        staff.salt = utils::random_hex(16);
        staff.password_hash = utils::hash_password(password, staff.salt);
        staff.role = role;
        staff.created_at = utils::get_current_iso8601();
        staff.updated_at = staff.created_at;
        std::string err;
        if (!db_->create_user(staff, err)) {
            json_error(res, err.find("already exists") != std::string::npos ? 409 : 500, err);
            return;
        }
        AuditLog al;
        al.actor = admin->email;
        al.action = "CREATE_LEARNING_STAFF";
        al.entity_type = "auth";
        al.entity_id = staff.id;
        al.details = nlohmann::json({{"role", staff.role}}).dump();
        db_->record_audit_log(al);
        res.status = 201;
        res.set_content(nlohmann::json({{"user", staff.to_json()}}).dump(), "application/json");
    });

    svr.Get("/api/v3/admin/learning/analytics", [this](const httplib::Request& req, httplib::Response& res) {
        set_no_store(res);
        auto admin = authenticate_admin(req, res);
        if (!admin) return;
        auto users = db_->list_learning_users();
        int learners = 0;
        int instructors = 0;
        int editors = 0;
        for (const auto& user : users) {
            if (user.role == "learner") ++learners;
            else if (user.role == "instructor") ++instructors;
            else if (user.role == "editor") ++editors;
        }
        auto courses = db_->list_learning_courses(std::nullopt, std::nullopt, std::nullopt,
                                                  "", 200, 0, false);
        nlohmann::json rows = nlohmann::json::array();
        int total_enrollments = 0;
        int total_completions = 0;
        int total_quiz_attempts = 0;
        for (const auto& course : courses) {
            auto analytics = db_->get_course_learning_analytics(course.id);
            total_enrollments += analytics.value("enrollments", 0);
            total_completions += analytics.value("completions", 0);
            total_quiz_attempts += analytics.value("quiz_attempts", 0);
            rows.push_back({
                {"course_id", course.id},
                {"slug", course.slug},
                {"title", course.title},
                {"status", course.status},
                {"author_email", course.author_email},
                {"analytics", analytics}
            });
        }
        res.set_content(nlohmann::json({
            {"users_by_role", {{"learners", learners}, {"instructors", instructors}, {"editors", editors}}},
            {"totals", {{"enrollments", total_enrollments},
                        {"completions", total_completions},
                        {"quiz_attempts", total_quiz_attempts}}},
            {"courses", rows},
            {"note", "Aggregate AdControl learning metrics only; no named learner activity is returned."}
        }).dump(), "application/json");
    });
}

} // namespace adcontrol
