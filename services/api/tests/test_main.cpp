#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sqlite3.h>

#ifdef NDEBUG
#error "AdControl tests must be built with assertions enabled (-UNDEBUG)."
#endif

#include "utils.hpp"
#include "db.hpp"
#include "automation.hpp"
#include "smtp.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

void test_url_safety() {
    using namespace adcontrol::utils;

    assert(is_safe_url("https://example.com/ad"));
    assert(is_safe_url("http://example.com/promo?id=123&utm=abc"));
    assert(is_safe_url("https://sub.domain.co.uk/path/to/page#section"));

    // Unsafe schemes must be rejected
    assert(!is_safe_url("javascript:alert(document.cookie)"));
    assert(!is_safe_url("JAVASCRIPT:alert(1)"));
    assert(!is_safe_url("   javascript:void(0)"));
    assert(!is_safe_url("data:text/html,<script>alert(1)</script>"));
    assert(!is_safe_url("vbscript:MsgBox(\"evil\")"));
    assert(!is_safe_url("file:///etc/passwd"));
    assert(!is_safe_url(""));
    assert(!is_safe_url("ftp://example.com"));

    std::cout << "[PASS] URL safety validation tests passed." << std::endl;
}

void test_domain_matching() {
    using namespace adcontrol::utils;

    std::vector<std::string> allowed = {"example.com", "*.myshop.com", "localhost:3000"};

    // Exact match
    assert(is_domain_allowed("https://example.com", allowed));
    assert(is_domain_allowed("http://example.com/path", allowed));
    assert(is_domain_allowed("http://localhost:3000", allowed));

    // Subdomain matching
    assert(is_domain_allowed("https://deals.myshop.com", allowed));
    assert(is_domain_allowed("https://myshop.com", allowed));

    // Disallowed domains
    assert(!is_domain_allowed("https://evil.com", allowed));
    assert(!is_domain_allowed("https://fakemyshop.com", allowed));
    assert(!is_domain_allowed("https://othermyshop.com.evil.com", allowed));

    // Wildcard
    std::vector<std::string> wildcard = {"*"};
    assert(is_domain_allowed("https://anywhere.io", wildcard));

    // An empty allowlist denies every non-empty origin (server-side validation
    // should never rely on a site key alone).
    assert(!is_domain_allowed("https://example.com", {}));

    // Privacy tools may strip Origin/Referer. Such requests are accepted only
    // after a rate-limited, signed-token path; they are not origin-authorized.
    assert(is_domain_allowed("", allowed));

    // Scheme, path, and casing are normalized for matching.
    assert(is_domain_allowed("HTTPS://EXAMPLE.COM/some/path?q=1", allowed));
    assert(!is_domain_allowed("http://localhost:3001", allowed));

    std::cout << "[PASS] Domain matching tests passed." << std::endl;
}

void test_schedule_check() {
    using namespace adcontrol::utils;

    std::string now = "2026-10-26T12:00:00Z";

    // Active
    assert(is_campaign_active_schedule("2026-10-26T00:00:00Z", "2026-10-26T23:59:59Z", now));
    // Open-ended
    assert(is_campaign_active_schedule("", "", now));
    assert(is_campaign_active_schedule("2026-10-01T00:00:00Z", "", now));
    assert(is_campaign_active_schedule("", "2026-10-30T00:00:00Z", now));

    // Expired
    assert(!is_campaign_active_schedule("2026-10-01T00:00:00Z", "2026-10-25T23:59:59Z", now));

    // Future scheduled
    assert(!is_campaign_active_schedule("2026-10-27T00:00:00Z", "2026-10-30T23:59:59Z", now));

    std::cout << "[PASS] Schedule eligibility tests passed." << std::endl;
}

void test_password_and_crypto() {
    using namespace adcontrol::utils;

    std::string salt = random_hex(16);
    std::string password = "SecretPassword123!";
    std::string hash = hash_password(password, salt);

    assert(verify_password(password, salt, hash));
    assert(!verify_password("WrongPassword", salt, hash));

    std::cout << "[PASS] PBKDF2 Password hashing tests passed." << std::endl;
}

void test_database_and_deduplication() {
    adcontrol::Database db;
    // Use an isolated temporary database
    std::string test_db = "test_adcontrol_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    // 1. Check default admin was seeded
    auto admin = db.get_user_by_email("admin@adcontrol.local");
    assert(admin.has_value());
    assert(admin->role == "admin");

    // 2. Create Site
    adcontrol::Site site;
    site.name = "Tech Blog";
    site.primary_domain = "blog.example.com";
    site.allowed_domains = {"blog.example.com", "localhost:5173"};
    site.status = "active";
    assert(db.create_site(site));
    assert(!site.id.empty());
    assert(!site.public_site_key.empty());

    // 3. Create Placement
    adcontrol::Placement p;
    p.site_id = site.id;
    p.placement_key = "header-banner";
    p.name = "Header Banner";
    p.format = "responsive_banner";
    p.fallback_behavior = "hide";
    p.status = "active";
    assert(db.create_placement(p));

    // Unique placement_key per site: duplicate key should fail
    adcontrol::Placement p_dup = p;
    p_dup.id = "";
    assert(!db.create_placement(p_dup));

    // 4. Create Campaign
    adcontrol::Campaign camp;
    camp.site_id = site.id;
    camp.placement_id = p.id;
    camp.name = "Autumn Sale";
    camp.destination_url = "https://example.com/autumn-sale";
    camp.format = "responsive_banner";
    camp.title = "Autumn Special 50% Off";
    camp.body = "Get top quality deals on all items today!";
    camp.image_url = "https://images.unsplash.com/photo-1557804506-669a67965ba0";
    camp.cta_label = "Claim Deal";
    camp.status = "active";
    assert(db.create_campaign(camp));

    // 5. Test delivery eligibility
    std::string now = adcontrol::utils::get_current_iso8601();
    auto eligible = db.get_eligible_campaigns_for_delivery(site.id, now);
    assert(eligible.size() == 1);
    assert(eligible[0].second.destination_url == "https://example.com/autumn-sale");

    // Pause campaign centrally -> check it is no longer delivered!
    assert(db.pause_campaign(camp.id));
    eligible = db.get_eligible_campaigns_for_delivery(site.id, now);
    assert(eligible.empty()); // Central pause works immediately!

    // Re-publish campaign
    assert(db.publish_campaign(camp.id));
    eligible = db.get_eligible_campaigns_for_delivery(site.id, now);
    assert(eligible.size() == 1);

    // The site-level kill switch must pause scheduled campaigns as well, not
    // only active ones. Otherwise a future campaign would activate itself.
    adcontrol::Campaign future;
    future.site_id = site.id;
    future.placement_id = p.id;
    future.name = "Future Sale";
    future.destination_url = "https://example.com/future-sale";
    future.format = "responsive_banner";
    future.status = "scheduled";
    future.starts_at = "2099-01-01T00:00:00Z";
    assert(db.create_campaign(future));
    assert(db.pause_all_campaigns_for_site(site.id));
    auto paused_future = db.get_campaign(future.id);
    assert(paused_future.has_value());
    assert(paused_future->status == "paused");
    auto paused_active = db.get_campaign(camp.id);
    assert(paused_active.has_value());
    assert(paused_active->status == "paused");
    // Restore the active campaign for the remaining assertions.
    assert(db.publish_campaign(camp.id));

    // 6. Test Event Deduplication (P0 requirement)
    adcontrol::AdEvent ev1;
    ev1.site_id = site.id;
    ev1.placement_id = p.id;
    ev1.campaign_id = camp.id;
    ev1.event_type = "impression";
    ev1.event_id = "dedup_event_unique_001";
    ev1.occurred_at = now;

    // First insertion should succeed
    bool first_insert = db.record_event(ev1);
    assert(first_insert == true);

    // Second insertion with SAME event_id must be deduplicated
    bool second_insert = db.record_event(ev1);
    assert(second_insert == false);

    // Record a click event
    adcontrol::AdEvent ev_click;
    ev_click.site_id = site.id;
    ev_click.placement_id = p.id;
    ev_click.campaign_id = camp.id;
    ev_click.event_type = "click";
    ev_click.event_id = "dedup_event_click_001";
    ev_click.occurred_at = now;
    assert(db.record_event(ev_click) == true);

    // 7. Verify Analytics Summary
    auto summary = db.get_analytics_summary(site.id, std::nullopt, std::nullopt);
    assert(summary["impressions"] == 1);
    assert(summary["clicks"] == 1);
    assert(summary["ctr"] == 100.0);

    // 8. Key rotation
    std::string old_key = site.public_site_key;
    std::string new_key;
    assert(db.rotate_site_key(site.id, new_key));
    assert(old_key != new_key);

    // Cleanup test db
    db.close();
    std::remove(test_db.c_str());

    std::cout << "[PASS] Database, eligibility, central pause, and event deduplication tests passed." << std::endl;
}

void test_phase2_tenancy_and_affiliate_metrics() {
    adcontrol::Database db;
    const std::string test_db = "test_phase2_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    // A publisher account owns exactly one tenant.
    adcontrol::Publisher publisher;
    publisher.name = "Integration Publisher";
    publisher.status = "active";
    publisher.contact_email = "owner@publisher.test";
    publisher.timezone = "UTC";

    adcontrol::User owner;
    owner.email = "owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));
    assert(!publisher.id.empty());

    auto stored_owner = db.get_user_by_email("owner@publisher.test");
    assert(stored_owner.has_value());
    assert(stored_owner->role == "publisher");
    assert(stored_owner->publisher_id == publisher.id);
    // Password material is never serialized in API representations.
    auto owner_json = stored_owner->to_json();
    assert(!owner_json.contains("password_hash"));
    assert(!owner_json.contains("salt"));

    // Publisher-owned sites start unverified and non-monetized, with a private
    // verification challenge that is not part of the public site JSON.
    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "Publisher Site";
    site.primary_domain = "publisher.test";
    site.allowed_domains = {"publisher.test"};
    site.status = "active";
    assert(db.create_site(site));
    assert(site.verification_status == "unverified");
    assert(site.monetization_status == "pending");
    assert(!site.verification_token.empty());
    auto site_json = site.to_json();
    assert(!site_json.contains("verification_token"));

    assert(db.mark_site_verified(site.id, "dns_txt"));
    auto verified = db.get_site(site.id);
    assert(verified.has_value());
    assert(verified->verification_status == "verified");
    assert(verified->monetization_status == "enabled");
    assert(verified->verification_token.empty());

    // Changing the domain must reset verification rather than inheriting it.
    adcontrol::Site changed = *verified;
    changed.primary_domain = "new.publisher.test";
    changed.allowed_domains = {"new.publisher.test"};
    assert(db.update_site(changed));
    assert(db.reset_site_verification(changed.id));
    auto reset = db.get_site(changed.id);
    assert(reset.has_value());
    assert(reset->verification_status == "unverified");
    assert(reset->monetization_status == "pending");
    assert(db.mark_site_verified(changed.id, "manual"));

    // Affiliate offers are scoped by publisher and preserve the exact URL.
    adcontrol::AffiliateOffer offer;
    offer.publisher_id = publisher.id;
    offer.provider = "shopee_affiliate";
    offer.offer_name = "Exact URL offer";
    offer.product_title = "Product A";
    offer.product_image_url = "https://cdn.example.com/product-a.jpg";
    offer.affiliate_url =
        "https://shopee.example.com/product/123?smtt=0.1&utm_source=Manual&aff_id=ABC";
    offer.canonical_product_url = "https://shopee.example.com/product/123";
    offer.disclosure_text = "Affiliate disclosure: we may earn a commission.";
    offer.status = "active";
    offer.validation_status = "valid";
    assert(db.create_affiliate_offer(offer));
    auto stored_offer = db.get_affiliate_offer(offer.id);
    assert(stored_offer.has_value());
    assert(stored_offer->affiliate_url == offer.affiliate_url);
    assert(db.list_affiliate_offers(publisher.id).size() == 1);
    assert(db.list_affiliate_offers("another-publisher").empty());

    // Analytics separate AdControl-observed redirects from confirmed external
    // conversions, and tenant filtering cannot leak another publisher's events.
    adcontrol::Placement placement;
    placement.site_id = changed.id;
    placement.placement_key = "product-grid";
    placement.name = "Product Grid";
    placement.format = "product_card";
    placement.fallback_behavior = "hide";
    placement.status = "active";
    assert(db.create_placement(placement));

    adcontrol::Campaign campaign;
    campaign.site_id = changed.id;
    campaign.placement_id = placement.id;
    campaign.name = "Affiliate Product Card";
    campaign.destination_url = offer.affiliate_url;
    campaign.format = "product_card";
    campaign.source_type = "affiliate";
    campaign.affiliate_offer_id = offer.id;
    campaign.title = offer.product_title;
    campaign.image_url = offer.product_image_url;
    campaign.status = "active";
    assert(db.create_campaign(campaign));

    const std::string now = adcontrol::utils::get_current_iso8601();
    adcontrol::AdEvent impression;
    impression.site_id = changed.id;
    impression.placement_id = campaign.placement_id;
    impression.campaign_id = campaign.id;
    impression.source_type = "affiliate";
    impression.affiliate_offer_id = offer.id;
    impression.event_type = "impression";
    impression.event_id = "phase2-impression-1";
    impression.occurred_at = now;
    assert(db.record_event(impression));

    adcontrol::AdEvent click = impression;
    click.event_type = "click";
    click.event_id = "phase2-click-1";
    assert(db.record_event(click));

    adcontrol::AdEvent outbound = impression;
    outbound.event_type = "outbound_redirect";
    outbound.event_id = "phase2-outbound-1";
    assert(db.record_event(outbound));

    auto summary = db.get_analytics_summary(changed.id, std::nullopt, std::nullopt);
    assert(summary["impressions"] == 1);
    assert(summary["clicks"] == 1);
    assert(summary["outbound_redirects"] == 1);
    assert(summary["affiliate_clicks"] == 1);
    assert(summary["confirmed_conversions"] == 0);
    assert(summary["confirmed_commission"] == 0);
    assert(summary["provenance"]["conversions"] == "conversion_source_reported");

    // A second tenant's events must never appear in the first tenant's summary.
    adcontrol::Publisher other;
    other.name = "Other Publisher";
    other.status = "active";
    adcontrol::User other_owner;
    other_owner.email = "owner@other.test";
    other_owner.salt = adcontrol::utils::random_hex(16);
    other_owner.password_hash = adcontrol::utils::hash_password("another-strong-password", other_owner.salt);
    assert(db.create_publisher_with_owner(other, other_owner));

    adcontrol::Site other_site;
    other_site.publisher_id = other.id;
    other_site.name = "Other Site";
    other_site.primary_domain = "other.test";
    other_site.allowed_domains = {"other.test"};
    other_site.status = "active";
    assert(db.create_site(other_site));

    adcontrol::AdEvent other_event;
    other_event.site_id = other_site.id;
    other_event.event_type = "impression";
    other_event.event_id = "phase2-other-impression-1";
    other_event.occurred_at = now;
    assert(db.record_event(other_event));

    auto scoped = db.get_analytics_summary(std::nullopt, std::nullopt, std::nullopt, "", "",
                                           publisher.id);
    assert(scoped["impressions"] == 1);
    assert(scoped["outbound_redirects"] == 1);
    auto other_scoped = db.get_analytics_summary(std::nullopt, std::nullopt, std::nullopt, "", "",
                                                other.id);
    assert(other_scoped["impressions"] == 1);

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Phase 2 tenancy, verification, affiliate URL, and provenance tests passed."
              << std::endl;
}

void test_online_backup() {
    adcontrol::Database db;
    const std::string test_db = "test_backup_" + adcontrol::utils::random_hex(6) + ".db";
    const std::string backup_db = "test_backup_" + adcontrol::utils::random_hex(6) + ".db";
    const std::string wal_suffix = "-wal";
    const std::string shm_suffix = "-shm";

    assert(db.init(test_db));

#ifndef _WIN32
    struct stat db_stat{};
    assert(stat(test_db.c_str(), &db_stat) == 0);
    assert((db_stat.st_mode & 0777) == 0600);
#endif

    adcontrol::Site site;
    site.name = "Backup Test Site";
    site.primary_domain = "backup.example.com";
    site.allowed_domains = {"backup.example.com"};
    site.status = "active";
    assert(db.create_site(site));

    assert(db.backup_to(backup_db));
    assert(std::filesystem::exists(backup_db));
    assert(std::filesystem::file_size(backup_db) > 0);

#ifndef _WIN32
    struct stat backup_stat{};
    assert(stat(backup_db.c_str(), &backup_stat) == 0);
    assert((backup_stat.st_mode & 0777) == 0600);
#endif

    // A backup is a complete, already-checkpointed SQLite database. Open it
    // immutable so the read-only verification does not create WAL side files.
    sqlite3* check = nullptr;
    const std::string uri = "file:" + backup_db + "?immutable=1";
    assert(sqlite3_open_v2(uri.c_str(), &check,
                           SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(check, "PRAGMA integrity_check;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const char* integrity = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    assert(integrity && std::string(integrity) == "ok");
    sqlite3_finalize(stmt);

    stmt = nullptr;
    assert(sqlite3_prepare_v2(check, "SELECT COUNT(*) FROM sites;", -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);
    assert(sqlite3_close(check) == SQLITE_OK);

    db.close();
    std::filesystem::remove(test_db);
    std::filesystem::remove(test_db + wal_suffix);
    std::filesystem::remove(test_db + shm_suffix);
    std::filesystem::remove(backup_db);

    std::cout << "[PASS] Online backup integrity, content, and permissions tests passed." << std::endl;
}

void test_partner_inquiries() {
    adcontrol::Database db;
    const std::string test_db = "test_inquiries_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::PartnerInquiry publisher;
    publisher.kind = "publisher";
    publisher.name = "Ada Publisher";
    publisher.email = "ada@example.com";
    publisher.company = "Ada's Blog";
    publisher.website = "https://ada.example.com";
    publisher.message = "I would like to place one article unit on my site.";
    assert(db.create_partner_inquiry(publisher));
    assert(!publisher.id.empty());
    assert(publisher.status == "new");
    assert(!publisher.created_at.empty());
    assert(!publisher.updated_at.empty());

    adcontrol::PartnerInquiry client;
    client.kind = "client";
    client.name = "Casey Client";
    client.email = "casey@example.com";
    client.company = "Casey Coffee";
    client.message = "We would like to promote a coffee subscription for six weeks.";
    assert(db.create_partner_inquiry(client));
    assert(!client.id.empty());
    assert(client.status == "new");

    auto all = db.list_partner_inquiries();
    assert(all.size() == 2);
    auto publishers = db.list_partner_inquiries("publisher", std::nullopt);
    assert(publishers.size() == 1);
    assert(publishers[0].id == publisher.id);
    auto clients = db.list_partner_inquiries("client", std::nullopt);
    assert(clients.size() == 1);
    assert(clients[0].id == client.id);
    assert(db.list_partner_inquiries(std::nullopt, "closed").empty());

    auto fetched = db.get_partner_inquiry(publisher.id);
    assert(fetched.has_value());
    assert(fetched->email == "ada@example.com");
    assert(fetched->website == "https://ada.example.com");
    assert(!db.get_partner_inquiry("missing").has_value());

    assert(db.update_partner_inquiry_status(publisher.id, "reviewing"));
    auto updated = db.get_partner_inquiry(publisher.id);
    assert(updated.has_value());
    assert(updated->status == "reviewing");
    assert(updated->updated_at >= publisher.updated_at);

    auto reviewing = db.list_partner_inquiries(std::nullopt, "reviewing");
    assert(reviewing.size() == 1);
    assert(reviewing[0].id == publisher.id);

    assert(!db.update_partner_inquiry_status("missing", "closed"));

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Partner inquiry persistence and status tests passed." << std::endl;
}

void test_secret_encryption() {
    using namespace adcontrol::utils;

    const std::string key = "unit-test-secret-key-material";
    const std::string cipher = encrypt_secret("p@ss w0rd!", key);
    assert(!cipher.empty());
    assert(cipher.rfind("v1:", 0) == 0);
    assert(cipher.find("p@ss") == std::string::npos);

    auto plain = decrypt_secret(cipher, key);
    assert(plain.has_value());
    assert(*plain == "p@ss w0rd!");

    // A different SECRET_KEY (or a tampered payload) must not decrypt.
    assert(!decrypt_secret(cipher, "another-key").has_value());
    std::string tampered = cipher;
    tampered.back() = tampered.back() == '0' ? '1' : '0';
    assert(!decrypt_secret(tampered, key).has_value());

    assert(encrypt_secret("", key).empty());
    auto empty = decrypt_secret("", key);
    assert(empty.has_value() && empty->empty());
    assert(!decrypt_secret("not-a-secret", key).has_value());

    std::cout << "[PASS] SMTP secret encryption tests passed." << std::endl;
}

void test_smtp_validation_and_message() {
    using namespace adcontrol::smtp;

    assert(is_valid_host("smtp.example.com"));
    assert(is_valid_host("localhost"));
    assert(!is_valid_host(""));
    assert(!is_valid_host("http://smtp.example.com"));
    assert(!is_valid_host("-bad.example.com"));
    assert(!is_valid_host("bad..example.com"));
    assert(!is_valid_host("smtp example.com"));

    assert(is_valid_email("user@example.com"));
    assert(is_valid_email("first.last+tag@sub.example.co.uk"));
    assert(!is_valid_email("user@"));
    assert(!is_valid_email("user example.com"));
    assert(!is_valid_email("user@example.com\r\nBcc: evil@example.com"));

    Settings settings;
    settings.host = "smtp.example.com";
    settings.port = 587;
    settings.encryption = "starttls";
    settings.username = "user";
    settings.password = "secret";
    settings.from_email = "newsletter@example.com";
    settings.from_name = "AdControl Marketing";
    settings.reply_to = "replies@example.com";
    assert(validate_settings(settings).empty());

    Settings invalid = settings;
    invalid.from_email = "not-an-email";
    assert(!validate_settings(invalid).empty());
    invalid = settings;
    invalid.port = 70000;
    assert(!validate_settings(invalid).empty());
    invalid = settings;
    invalid.encryption = "plaintext";
    assert(!validate_settings(invalid).empty());
    invalid = settings;
    invalid.from_name = "Evil\r\nBcc: other@example.com";
    assert(!validate_settings(invalid).empty());
    invalid = settings;
    invalid.reply_to = "bad";
    assert(!validate_settings(invalid).empty());

    const std::string message =
        build_message(settings, "learner@example.com", "Your weekly picks", "<p>Hello</p>");
    assert(message.find("From: AdControl Marketing <newsletter@example.com>\r\n") != std::string::npos);
    assert(message.find("To: learner@example.com\r\n") != std::string::npos);
    assert(message.find("Reply-To: replies@example.com\r\n") != std::string::npos);
    assert(message.find("Subject: Your weekly picks\r\n") != std::string::npos);
    assert(message.find("Content-Type: text/html; charset=UTF-8\r\n") != std::string::npos);
    // The HTML body is base64 encoded, never injected as raw markup.
    assert(message.find("<p>Hello</p>") == std::string::npos);

    Settings encoded = settings;
    encoded.from_name = "Név";
    const std::string encoded_message =
        build_message(encoded, "learner@example.com", "Héllo", "<p>x</p>");
    assert(encoded_message.find("=?UTF-8?B?") != std::string::npos);
    assert(encoded_message.find("\r\nBcc:") == std::string::npos);

    std::cout << "[PASS] SMTP validation and message building tests passed." << std::endl;
}

void test_email_settings_persistence() {
    adcontrol::Database db;
    const std::string test_db = "test_email_settings_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    // No row is seeded; the API returns defaults until an administrator saves.
    assert(!db.get_email_settings().has_value());

    adcontrol::EmailSettings settings;
    settings.enabled = true;
    settings.host = "smtp.example.com";
    settings.port = 465;
    settings.encryption = "ssl";
    settings.username = "newsletter";
    settings.password_cipher = adcontrol::utils::encrypt_secret("smtp-password", "test-key");
    settings.from_email = "newsletter@example.com";
    settings.from_name = "Example News";
    settings.reply_to = "replies@example.com";
    settings.updated_by = "admin@example.com";
    assert(!settings.password_cipher.empty());
    assert(db.save_email_settings(settings));

    auto loaded = db.get_email_settings();
    assert(loaded.has_value());
    assert(loaded->enabled);
    assert(loaded->host == "smtp.example.com");
    assert(loaded->port == 465);
    assert(loaded->encryption == "ssl");
    assert(loaded->username == "newsletter");
    assert(loaded->password_cipher == settings.password_cipher);
    assert(loaded->from_email == "newsletter@example.com");
    assert(loaded->from_name == "Example News");
    assert(loaded->reply_to == "replies@example.com");
    assert(loaded->updated_by == "admin@example.com");
    assert(!loaded->updated_at.empty());

    // The dashboard representation reports only that a password exists.
    auto json = loaded->to_json();
    assert(json["password_set"] == true);
    assert(json.find("password_cipher") == json.end());
    assert(json.find("password") == json.end());
    assert(json.dump().find("smtp-password") == std::string::npos);

    // A second save updates the single row instead of inserting another.
    loaded->enabled = false;
    loaded->port = 587;
    loaded->encryption = "starttls";
    loaded->password_cipher.clear();
    assert(db.save_email_settings(*loaded));

    auto updated = db.get_email_settings();
    assert(updated.has_value());
    assert(!updated->enabled);
    assert(updated->port == 587);
    assert(updated->encryption == "starttls");
    assert(updated->password_cipher.empty());
    assert(updated->to_json()["password_set"] == false);

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Email settings persistence tests passed." << std::endl;
}

void test_monetization_billing_and_paid_learning() {
    adcontrol::Database db;
    const std::string test_db = "test_billing_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    // ------------------------------------------------------------------
    // Seeded plans
    // ------------------------------------------------------------------
    auto plans = db.list_billing_plans(true);
    assert(plans.size() == 4);
    assert(plans[0].id == "free");
    assert(plans[1].id == "pro");
    assert(plans[2].id == "business");
    assert(plans[3].id == "enterprise");

    auto free_plan = db.get_billing_plan("free");
    assert(free_plan.has_value());
    assert(free_plan->name == "Free");
    assert(free_plan->price_cents == 0);
    assert(free_plan->included_campaigns == 3);
    assert(free_plan->included_impressions == 50000);
    assert(free_plan->overage_per_1000_impressions_cents == 0);
    assert(free_plan->revenue_share_percent == 10);
    assert(free_plan->features.size() == 4);

    auto pro_plan = db.get_billing_plan("pro");
    assert(pro_plan.has_value());
    assert(pro_plan->price_cents == 2900);
    assert(pro_plan->included_campaigns == 25);
    assert(pro_plan->included_impressions == 500000);
    assert(pro_plan->overage_per_1000_impressions_cents == 500);
    assert(pro_plan->revenue_share_percent == 15);

    auto business_plan = db.get_billing_plan("business");
    assert(business_plan.has_value());
    assert(business_plan->price_cents == 9900);
    assert(business_plan->overage_per_1000_impressions_cents == 300);

    auto enterprise_plan = db.get_billing_plan("enterprise");
    assert(enterprise_plan.has_value());
    assert(enterprise_plan->price_cents == 49900);
    assert(enterprise_plan->included_campaigns == -1);
    assert(enterprise_plan->included_impressions == -1);
    assert(enterprise_plan->revenue_share_percent == 25);

    // Plan edits are persisted, including unlimited (-1) limits.
    adcontrol::BillingPlan edited_free = *free_plan;
    edited_free.description = "Tiny test allowance";
    edited_free.included_impressions = 2;
    edited_free.features = {"Test allowance"};
    assert(db.update_billing_plan(edited_free));
    auto stored_free = db.get_billing_plan("free");
    assert(stored_free.has_value());
    assert(stored_free->included_impressions == 2);
    assert(stored_free->description == "Tiny test allowance");
    assert(stored_free->features.size() == 1);

    // Keep the Pro allowance small enough to exercise overage billing without
    // inserting hundreds of thousands of rows.
    adcontrol::BillingPlan edited_pro = *pro_plan;
    edited_pro.included_impressions = 2;
    assert(db.update_billing_plan(edited_pro));

    // ------------------------------------------------------------------
    // Publisher subscription lifecycle
    // ------------------------------------------------------------------
    adcontrol::Publisher publisher;
    publisher.name = "Billing Publisher";
    publisher.status = "active";
    publisher.contact_email = "billing@publisher.test";
    publisher.timezone = "UTC";

    adcontrol::User owner;
    owner.email = "billing-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));
    assert(!publisher.id.empty());

    std::string error;
    auto subscription = db.ensure_publisher_subscription(publisher.id, error);
    assert(subscription.has_value());
    assert(subscription->publisher_id == publisher.id);
    assert(subscription->plan_id == "free");
    assert(subscription->status == "active");
    assert(!subscription->current_period_start.empty());
    assert(!subscription->current_period_end.empty());
    assert(subscription->current_period_start < subscription->current_period_end);

    // The subscription is lazily created once and returned on every call.
    auto repeated = db.ensure_publisher_subscription(publisher.id, error);
    assert(repeated.has_value());
    assert(repeated->id == subscription->id);

    // Unknown publishers and plans are rejected.
    assert(!db.ensure_publisher_subscription("missing-publisher", error));
    assert(error == "Publisher not found");
    assert(!db.update_publisher_subscription_plan(publisher.id, "missing-plan", error));
    assert(error == "Plan not found or inactive");

    // ------------------------------------------------------------------
    // Site, placement, campaigns, and usage
    // ------------------------------------------------------------------
    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "Billing Site";
    site.primary_domain = "billing.test";
    site.allowed_domains = {"billing.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::Placement placement;
    placement.site_id = site.id;
    placement.placement_key = "billing-slot";
    placement.name = "Billing Slot";
    placement.format = "responsive_banner";
    placement.fallback_behavior = "hide";
    placement.status = "active";
    assert(db.create_placement(placement));

    auto make_campaign = [&](const std::string& name, const std::string& source_type,
                             int cpm_cents, const std::string& status) {
        adcontrol::Campaign campaign;
        campaign.site_id = site.id;
        campaign.placement_id = placement.id;
        campaign.name = name;
        campaign.destination_url = "https://advertiser.example/" + name;
        campaign.format = "responsive_banner";
        campaign.source_type = source_type;
        campaign.status = status;
        campaign.cpm_cents = cpm_cents;
        assert(db.create_campaign(campaign));
        return campaign;
    };

    auto direct = make_campaign("direct-campaign", "direct", 5000, "active");
    auto house = make_campaign("house-campaign", "house", 0, "active");
    auto archived = make_campaign("archived-campaign", "direct", 9000, "archived");

    // Archived campaigns do not consume the plan's campaign allowance.
    assert(db.count_publisher_campaigns(publisher.id) == 2);

    auto usage = db.get_publisher_usage(publisher.id, subscription->current_period_start,
                                        subscription->current_period_end);
    assert(usage.campaigns == 2);
    assert(usage.included_campaigns == 3);
    assert(!usage.campaign_quota_exceeded);
    assert(usage.impressions == 0);
    assert(usage.hard_cap);
    assert(usage.included_impressions == 2);
    assert(!usage.impression_quota_exceeded);

    // A third active campaign reaches the Free plan's campaign limit.
    auto extra = make_campaign("extra-campaign", "house", 0, "active");
    assert(db.count_publisher_campaigns(publisher.id) == 3);
    usage = db.get_publisher_usage(publisher.id, subscription->current_period_start,
                                   subscription->current_period_end);
    assert(usage.campaigns == 3);
    assert(usage.campaign_quota_exceeded);

    const std::string now = adcontrol::utils::get_current_iso8601();
    auto record_impression = [&](const adcontrol::Campaign& campaign,
                                 const std::string& event_id) {
        adcontrol::AdEvent event;
        event.site_id = site.id;
        event.placement_id = placement.id;
        event.campaign_id = campaign.id;
        event.source_type = campaign.source_type;
        event.event_type = "impression";
        event.event_id = event_id;
        event.occurred_at = now;
        assert(db.record_event(event));
    };

    for (int i = 0; i < 5; ++i) {
        record_impression(direct, "billing-direct-impression-" + std::to_string(i));
    }
    record_impression(house, "billing-house-impression-0");
    record_impression(house, "billing-house-impression-1");

    assert(db.count_publisher_impressions(publisher.id, subscription->current_period_start,
                                          subscription->current_period_end) == 7);

    // The Free plan's zero overage rate makes the allowance a hard cap.
    usage = db.get_publisher_usage(publisher.id, subscription->current_period_start,
                                   subscription->current_period_end);
    assert(usage.impressions == 7);
    assert(usage.overage_impressions == 5);
    assert(usage.overage_cents == 0);
    assert(usage.hard_cap);
    assert(usage.impression_quota_exceeded);
    assert(usage.campaign_quota_exceeded);
    assert(usage.revenue_share_percent == 10);

    auto revenue = db.get_publisher_revenue(publisher.id, subscription->current_period_start,
                                            subscription->current_period_end);
    assert(revenue.direct_impressions == 5);
    assert(revenue.gross_cents == 25); // 5 impressions x $50 CPM
    assert(revenue.share_percent == 10);
    assert(revenue.publisher_cents == 2);

    // Upgrading to Pro enables overage billing and a larger revenue share.
    assert(db.update_publisher_subscription_plan(publisher.id, "pro", error));
    auto pro_subscription = db.get_publisher_subscription(publisher.id);
    assert(pro_subscription.has_value());
    assert(pro_subscription->plan_id == "pro");

    usage = db.get_publisher_usage(publisher.id, subscription->current_period_start,
                                   subscription->current_period_end);
    assert(!usage.hard_cap);
    assert(usage.included_impressions == 2);
    assert(usage.overage_impressions == 5);
    assert(usage.overage_cents == 500); // one 1,000-impression block at $5
    assert(usage.revenue_share_percent == 15);

    revenue = db.get_publisher_revenue(publisher.id, subscription->current_period_start,
                                       subscription->current_period_end);
    assert(revenue.share_percent == 15);
    assert(revenue.publisher_cents == 3);

    // Enterprise is unlimited and never overage-billed.
    assert(db.update_publisher_subscription_plan(publisher.id, "enterprise", error));
    usage = db.get_publisher_usage(publisher.id, subscription->current_period_start,
                                   subscription->current_period_end);
    assert(usage.included_impressions == -1);
    assert(usage.included_campaigns == -1);
    assert(usage.overage_impressions == 0);
    assert(usage.overage_cents == 0);
    assert(!usage.hard_cap);
    assert(!usage.impression_quota_exceeded);
    assert(!usage.campaign_quota_exceeded);
    revenue = db.get_publisher_revenue(publisher.id, subscription->current_period_start,
                                       subscription->current_period_end);
    assert(revenue.share_percent == 25);
    assert(revenue.publisher_cents == 6);

    // ------------------------------------------------------------------
    // Invoices and manual settlement
    // ------------------------------------------------------------------
    adcontrol::BillingInvoice invoice;
    invoice.publisher_id = publisher.id;
    invoice.context = "subscription";
    invoice.period_start = subscription->current_period_start;
    invoice.period_end = subscription->current_period_end;
    invoice.currency = "USD";
    invoice.notes = "Pro plan and overage";

    adcontrol::BillingInvoiceItem subscription_item;
    subscription_item.kind = "subscription";
    subscription_item.description = "Pro plan";
    subscription_item.quantity = 1;
    subscription_item.unit_amount_cents = 2900;
    invoice.items.push_back(subscription_item);

    adcontrol::BillingInvoiceItem overage_item;
    overage_item.kind = "overage";
    overage_item.description = "Impression overage";
    overage_item.quantity = 1;
    overage_item.unit_amount_cents = 500;
    invoice.items.push_back(overage_item);

    assert(db.create_billing_invoice(invoice, error));
    assert(!invoice.id.empty());
    assert(invoice.subtotal_cents == 3400);
    assert(invoice.total_cents == 3400);
    assert(invoice.status == "open");
    assert(invoice.items.size() == 2);

    auto stored_invoice = db.get_billing_invoice(invoice.id);
    assert(stored_invoice.has_value());
    assert(stored_invoice->amount_paid_cents == 0);
    assert(stored_invoice->items.size() == 2);
    assert(stored_invoice->items[0].invoice_id == invoice.id);

    auto open_invoice = db.find_billing_invoice(publisher.id, "", "subscription",
                                                subscription->current_period_start,
                                                subscription->current_period_end, true);
    assert(open_invoice.has_value());
    assert(open_invoice->id == invoice.id);

    auto open_list = db.list_billing_invoices(publisher.id, std::nullopt, "subscription",
                                              "open", 100, 0);
    assert(open_list.size() == 1);
    assert(open_list[0].id == invoice.id);

    // An invoice must have a payer and at least one line item.
    adcontrol::BillingInvoice invalid_invoice;
    assert(!db.create_billing_invoice(invalid_invoice, error));
    invalid_invoice.publisher_id = publisher.id;
    assert(!db.create_billing_invoice(invalid_invoice, error));

    // Partial settlement.
    adcontrol::BillingPayment payment;
    payment.invoice_id = invoice.id;
    payment.amount_cents = 1000;
    payment.method = "bank_transfer";
    payment.reference = "BANK-001";
    payment.recorded_by = "admin@example.test";
    assert(db.record_billing_payment(payment, error));
    assert(payment.publisher_id == publisher.id);
    assert(!payment.id.empty());

    stored_invoice = db.get_billing_invoice(invoice.id);
    assert(stored_invoice->status == "partially_paid");
    assert(stored_invoice->amount_paid_cents == 1000);
    assert(stored_invoice->paid_at.empty());

    // Overpayment and currency mismatches are rejected without changing state.
    adcontrol::BillingPayment overpayment;
    overpayment.invoice_id = invoice.id;
    overpayment.amount_cents = 3000;
    assert(!db.record_billing_payment(overpayment, error));
    assert(error == "Payment exceeds the outstanding balance");

    adcontrol::BillingPayment wrong_currency;
    wrong_currency.invoice_id = invoice.id;
    wrong_currency.amount_cents = 100;
    wrong_currency.currency = "EUR";
    assert(!db.record_billing_payment(wrong_currency, error));
    assert(error == "Payment currency must match the invoice currency");

    adcontrol::BillingPayment zero_payment;
    zero_payment.invoice_id = invoice.id;
    assert(!db.record_billing_payment(zero_payment, error));

    // Final settlement marks the invoice paid and timestamps it.
    adcontrol::BillingPayment final_payment;
    final_payment.invoice_id = invoice.id;
    final_payment.amount_cents = 2400;
    final_payment.method = "cash";
    assert(db.record_billing_payment(final_payment, error));

    stored_invoice = db.get_billing_invoice(invoice.id);
    assert(stored_invoice->status == "paid");
    assert(stored_invoice->amount_paid_cents == 3400);
    assert(!stored_invoice->paid_at.empty());

    auto payments = db.list_billing_payments(publisher.id, std::nullopt, invoice.id, 100, 0);
    assert(payments.size() == 2);
    assert(payments[0].amount_cents + payments[1].amount_cents == 3400);

    // Paid invoices cannot receive more payments or be voided.
    assert(!db.record_billing_payment(final_payment, error));
    assert(error == "This invoice is already paid");
    assert(!db.void_billing_invoice(invoice.id));
    assert(!db.find_billing_invoice(publisher.id, "", "subscription",
                                    subscription->current_period_start,
                                    subscription->current_period_end, true).has_value());

    auto paid_list = db.list_billing_invoices(publisher.id, std::nullopt, std::nullopt,
                                              "paid", 100, 0);
    assert(paid_list.size() == 1);

    // Open manual invoices can be voided; void invoices reject payments.
    adcontrol::BillingInvoice manual;
    manual.publisher_id = publisher.id;
    manual.context = "manual";
    adcontrol::BillingInvoiceItem adjustment;
    adjustment.kind = "adjustment";
    adjustment.description = "Manual adjustment";
    adjustment.quantity = 1;
    adjustment.unit_amount_cents = 1000;
    manual.items.push_back(adjustment);
    assert(db.create_billing_invoice(manual, error));
    assert(db.void_billing_invoice(manual.id));
    auto voided = db.get_billing_invoice(manual.id);
    assert(voided.has_value());
    assert(voided->status == "void");
    assert(!db.void_billing_invoice(manual.id));

    adcontrol::BillingPayment void_payment;
    void_payment.invoice_id = manual.id;
    void_payment.amount_cents = 100;
    assert(!db.record_billing_payment(void_payment, error));
    assert(error == "A void invoice cannot receive payments");

    // ------------------------------------------------------------------
    // Paid learning course: checkout invoice grants access on settlement
    // ------------------------------------------------------------------
    adcontrol::LearningTopic topic;
    topic.id = adcontrol::utils::generate_uuid();
    topic.slug = "billing-topic";
    topic.title = "Billing Topic";
    topic.status = "published";
    topic.created_at = now;
    topic.updated_at = now;
    assert(db.create_learning_topic(topic, error));

    adcontrol::LearningCourse course;
    course.id = adcontrol::utils::generate_uuid();
    course.slug = "paid-billing-course";
    course.topic_id = topic.id;
    course.title = "Paid Billing Course";
    course.summary = "Learn how settlement works.";
    course.level = "beginner";
    course.status = "published";
    course.author_id = owner.id;
    course.access_type = "paid";
    course.price_cents = 4900;
    course.currency = "USD";
    course.published_at = now;
    course.created_at = now;
    course.updated_at = now;
    assert(db.create_learning_course(course, error));

    auto stored_course = db.get_learning_course(course.id);
    assert(stored_course.has_value());
    assert(stored_course->access_type == "paid");
    assert(stored_course->price_cents == 4900);
    assert(stored_course->currency == "USD");
    assert(stored_course->to_json()["price_cents"] == 4900);
    assert(stored_course->to_json()["access_type"] == "paid");

    adcontrol::User learner;
    learner.id = adcontrol::utils::generate_uuid();
    learner.email = "learner@example.test";
    learner.salt = adcontrol::utils::random_hex(16);
    learner.password_hash = adcontrol::utils::hash_password("learner-password", learner.salt);
    learner.role = "learner";
    learner.created_at = now;
    learner.updated_at = now;
    assert(db.create_user(learner, error));

    adcontrol::BillingInvoice course_invoice;
    course_invoice.user_id = learner.id;
    course_invoice.context = "course";
    course_invoice.currency = course.currency;
    course_invoice.notes = "Course purchase: " + course.title;
    adcontrol::BillingInvoiceItem course_item;
    course_item.kind = "course";
    course_item.description = course.title;
    course_item.quantity = 1;
    course_item.unit_amount_cents = course.price_cents;
    course_item.amount_cents = course.price_cents;
    course_item.course_id = course.id;
    course_invoice.items.push_back(course_item);
    assert(db.create_billing_invoice(course_invoice, error));
    assert(course_invoice.total_cents == 4900);

    // Creating an invoice does not grant access; settlement does.
    assert(!db.get_course_entitlement(learner.id, course.id).has_value());
    assert(db.list_user_entitlements(learner.id).empty());

    adcontrol::BillingPayment course_payment;
    course_payment.invoice_id = course_invoice.id;
    course_payment.amount_cents = 4900;
    course_payment.method = "manual";
    assert(db.record_billing_payment(course_payment, error));

    auto entitlement = db.get_course_entitlement(learner.id, course.id);
    assert(entitlement.has_value());
    assert(entitlement->status == "active");
    assert(entitlement->invoice_id == course_invoice.id);
    assert(!entitlement->granted_at.empty());
    assert(db.list_user_entitlements(learner.id).size() == 1);

    auto paid_course_invoice = db.get_billing_invoice(course_invoice.id);
    assert(paid_course_invoice->status == "paid");
    assert(paid_course_invoice->user_id == learner.id);

    auto user_invoices = db.list_billing_invoices(std::nullopt, learner.id, "course",
                                                  std::nullopt, 100, 0);
    assert(user_invoices.size() == 1);
    auto found_course_invoice = db.find_billing_invoice("", learner.id, "course", "", "", false);
    assert(found_course_invoice.has_value());
    assert(found_course_invoice->id == course_invoice.id);

    // Revocation removes active access without deleting the audit trail.
    assert(db.revoke_course_entitlement(learner.id, course.id));
    auto revoked = db.get_course_entitlement(learner.id, course.id);
    assert(revoked.has_value());
    assert(revoked->status == "revoked");
    assert(!revoked->revoked_at.empty());
    assert(db.list_user_entitlements(learner.id).empty());
    assert(!db.revoke_course_entitlement(learner.id, course.id));

    // A zero-total (fully discounted) course invoice is settled immediately and
    // grants access atomically.
    adcontrol::BillingInvoice free_course_invoice;
    free_course_invoice.user_id = learner.id;
    free_course_invoice.context = "course";
    free_course_invoice.currency = course.currency;
    adcontrol::BillingInvoiceItem free_course_item;
    free_course_item.kind = "course";
    free_course_item.description = "Scholarship: " + course.title;
    free_course_item.quantity = 1;
    free_course_item.unit_amount_cents = 0;
    free_course_item.amount_cents = 0;
    free_course_item.course_id = course.id;
    free_course_invoice.items.push_back(free_course_item);
    assert(db.create_billing_invoice(free_course_invoice, error));
    assert(free_course_invoice.total_cents == 0);
    assert(free_course_invoice.status == "paid");
    auto regranted = db.get_course_entitlement(learner.id, course.id);
    assert(regranted.has_value());
    assert(regranted->status == "active");
    assert(regranted->invoice_id == free_course_invoice.id);

    // ------------------------------------------------------------------
    // Publisher payouts
    // ------------------------------------------------------------------
    adcontrol::PublisherPayout payout;
    payout.publisher_id = publisher.id;
    payout.period_start = subscription->current_period_start;
    payout.period_end = subscription->current_period_end;
    payout.amount_cents = revenue.publisher_cents;
    payout.method = "bank_transfer";
    payout.reference = "PAYOUT-001";
    payout.recorded_by = "admin@example.test";
    assert(db.create_publisher_payout(payout, error));
    assert(payout.status == "paid");
    assert(!payout.paid_at.empty());

    auto payouts = db.list_publisher_payouts(publisher.id, 100, 0);
    assert(payouts.size() == 1);
    assert(payouts[0].amount_cents == revenue.publisher_cents);
    assert(payouts[0].reference == "PAYOUT-001");

    adcontrol::PublisherPayout invalid_payout;
    invalid_payout.publisher_id = publisher.id;
    assert(!db.create_publisher_payout(invalid_payout, error));
    assert(error == "Payout amount must be greater than zero");
    invalid_payout.publisher_id = "missing-publisher";
    invalid_payout.amount_cents = 100;
    assert(!db.create_publisher_payout(invalid_payout, error));
    assert(error == "Publisher not found");

    // JSON views never expose internal secrets.
    auto plan_json = stored_free->to_json();
    assert(plan_json["included_impressions"] == 2);
    assert(plan_json["revenue_share_percent"] == 10);
    assert(stored_invoice->to_json()["balance_cents"] == 0);
    assert(entitlement->to_json()["status"] == "active");

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Monetization, invoices, paid learning, and payout tests passed."
              << std::endl;
}

void test_conversion_tracking_and_reconciliation() {
    adcontrol::Database db;
    const std::string test_db = "test_conversions_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::Publisher publisher;
    publisher.name = "Affiliate Publisher";
    publisher.status = "active";
    publisher.contact_email = "affiliate@publisher.test";
    publisher.timezone = "UTC";
    adcontrol::User owner;
    owner.email = "affiliate-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));

    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "Affiliate Site";
    site.primary_domain = "affiliate.test";
    site.allowed_domains = {"affiliate.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::Placement placement;
    placement.site_id = site.id;
    placement.placement_key = "conversion-slot";
    placement.name = "Conversion Slot";
    placement.format = "product_card";
    placement.fallback_behavior = "hide";
    placement.status = "active";
    assert(db.create_placement(placement));

    adcontrol::AffiliateOffer offer;
    offer.publisher_id = publisher.id;
    offer.offer_name = "Reconciliation offer";
    offer.product_title = "Tracked Product";
    offer.affiliate_url = "https://shop.example/product/1?aff=1";
    offer.status = "active";
    offer.validation_status = "valid";
    assert(db.create_affiliate_offer(offer));

    adcontrol::Campaign campaign;
    campaign.site_id = site.id;
    campaign.placement_id = placement.id;
    campaign.name = "Tracked Campaign";
    campaign.destination_url = offer.affiliate_url;
    campaign.format = "product_card";
    campaign.source_type = "affiliate";
    campaign.affiliate_offer_id = offer.id;
    campaign.status = "active";
    assert(db.create_campaign(campaign));

    adcontrol::ConversionSource source;
    source.site_id = site.id;
    source.publisher_id = publisher.id;
    source.name = "Thank-you page";
    source.public_key = "csrc_test_" + adcontrol::utils::random_hex(8);
    source.encrypted_secret = "v1:test:test:test";
    source.default_campaign_id = campaign.id;
    source.default_offer_id = offer.id;
    assert(db.create_conversion_source(source));
    assert(!source.id.empty());
    assert(!source.to_json().contains("encrypted_secret"));

    auto by_key = db.get_conversion_source_by_public_key(source.public_key);
    assert(by_key.has_value());
    assert(by_key->id == source.id);
    assert(db.list_conversion_sources(publisher.id).size() == 1);
    assert(db.list_conversion_sources("another-publisher").empty());

    adcontrol::ConversionEvent event;
    event.source_id = source.id;
    event.site_id = site.id;
    event.publisher_id = publisher.id;
    event.campaign_id = campaign.id;
    event.offer_id = offer.id;
    event.external_id = "ORDER-1001";
    event.event_name = "purchase";
    event.order_id = "ORDER-1001";
    event.value_cents = 4999;
    event.commission_cents = 499;
    event.utm_source = "newsletter";
    event.utm_medium = "email";
    event.utm_campaign = "autumn";
    event.first_touch = R"({"utm_source":"google","utm_medium":"cpc"})";
    event.last_touch = R"({"utm_source":"newsletter","utm_medium":"email"})";
    event.occurred_at = adcontrol::utils::get_current_iso8601();
    event.ingestion = "webhook";
    bool inserted = false;
    std::string error;
    assert(db.record_conversion(event, inserted, error));
    assert(inserted);
    assert(!event.id.empty());

    // A replay with the same source and external id is idempotent.
    adcontrol::ConversionEvent replay;
    replay.source_id = source.id;
    replay.site_id = site.id;
    replay.external_id = "ORDER-1001";
    replay.value_cents = 999999;
    replay.occurred_at = adcontrol::utils::get_current_iso8601();
    assert(db.record_conversion(replay, inserted, error));
    assert(!inserted);
    assert(replay.id == event.id);
    auto stored = db.get_conversion_event(event.id);
    assert(stored.has_value());
    assert(stored->value_cents == 4999);
    assert(stored->commission_cents == 499);
    assert(stored->utm_source == "newsletter");

    auto summary = db.get_conversion_summary(publisher.id, site.id, campaign.id);
    assert(summary["conversions"] == 1);
    assert(summary["pending_conversions"] == 1);
    assert(summary["gross_value_cents"] == 4999);
    assert(summary["commission_cents"] == 499);
    assert(summary["approved_value_cents"] == 0);

    assert(db.update_conversion_reconciliation(event.id, "approved", "Verified in affiliate dashboard"));
    auto approved = db.get_conversion_event(event.id);
    assert(approved.has_value());
    assert(approved->status == "approved");
    assert(!approved->reconciled_at.empty());
    summary = db.get_conversion_summary(publisher.id, site.id, campaign.id);
    assert(summary["approved_conversions"] == 1);
    assert(summary["approved_value_cents"] == 4999);
    assert(summary["approved_commission_cents"] == 499);
    auto analytics = db.get_analytics_summary(site.id, std::nullopt, campaign.id);
    assert(analytics["confirmed_conversions"] == 1);
    assert(analytics["confirmed_commission"] == 499);
    assert(analytics["provenance"]["conversions"] == "conversion_source_reported");

    auto reconciliation = db.get_conversion_reconciliation(publisher.id, site.id);
    assert(reconciliation["rows"].is_array());
    assert(reconciliation["rows"].size() == 1);
    assert(reconciliation["rows"][0]["offer_id"] == offer.id);
    assert(reconciliation["rows"][0]["status"] == "approved");
    assert(reconciliation["rows"][0]["commission_cents"] == 499);

    // Tenant filters never expose another publisher's conversions.
    assert(db.list_conversion_events("another-publisher", std::nullopt, std::nullopt,
                                     std::nullopt, "", "", 100, 0).empty());
    assert(db.list_conversion_events(publisher.id, std::nullopt, std::nullopt,
                                     std::nullopt, "", "", 100, 0).size() == 1);

    source.status = "disabled";
    assert(db.update_conversion_source(source));
    auto disabled = db.get_conversion_source(source.id);
    assert(disabled.has_value());
    assert(disabled->status == "disabled");

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Conversion attribution, idempotency, tenant scope, and reconciliation tests passed."
              << std::endl;
}

void test_automation_workflows_and_worker() {
    adcontrol::Database db;
    const std::string test_db = "test_automation_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::Publisher publisher;
    publisher.name = "Automation Publisher";
    publisher.status = "active";
    publisher.contact_email = "automation@publisher.test";
    publisher.timezone = "UTC";
    adcontrol::User owner;
    owner.email = "automation-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));

    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "Automation Site";
    site.primary_domain = "automation.test";
    site.allowed_domains = {"automation.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::Placement placement;
    placement.site_id = site.id;
    placement.placement_key = "automation-slot";
    placement.name = "Automation Slot";
    placement.format = "responsive_banner";
    placement.fallback_behavior = "hide";
    placement.status = "active";
    assert(db.create_placement(placement));

    adcontrol::Campaign campaign;
    campaign.site_id = site.id;
    campaign.placement_id = placement.id;
    campaign.name = "Automation Campaign";
    campaign.destination_url = "https://advertiser.example/automation";
    campaign.format = "responsive_banner";
    campaign.source_type = "house";
    campaign.status = "active";
    assert(db.create_campaign(campaign));

    const std::string now = adcontrol::utils::get_current_iso8601();
    const std::string past = adcontrol::utils::iso8601_from_epoch(
        adcontrol::utils::utc_now_epoch() - 120);

    adcontrol::AutomationWorkflow manual;
    manual.publisher_id = publisher.id;
    manual.site_id = site.id;
    manual.name = "Manual notification";
    manual.status = "active";
    manual.trigger_type = "manual";
    manual.created_by = owner.email;
    adcontrol::AutomationStep notify;
    notify.type = "action";
    notify.config = R"({"action":"send_notification","kind":"info","title":"Manual run complete","body":"Worker executed the manual workflow."})";
    manual.steps.push_back(notify);
    std::string error;
    assert(db.create_automation_workflow(manual, error));
    assert(manual.steps[0].id.empty() == false);

    std::string run_id;
    assert(db.queue_manual_automation_run(manual.id, "{}", "manual:test-1", run_id, error));
    assert(!run_id.empty());
    // A duplicate manual trigger key is idempotent.
    std::string duplicate_id;
    assert(!db.queue_manual_automation_run(manual.id, "{}", "manual:test-1", duplicate_id, error));

    int processed = adcontrol::run_automation_tick(db);
    assert(processed >= 1);
    auto manual_run = db.get_automation_run(run_id);
    assert(manual_run.has_value());
    if (manual_run->status != "completed") {
        std::cerr << "Unexpected manual run status=" << manual_run->status
                  << " error=" << manual_run->error
                  << " log=" << manual_run->log << std::endl;
    }
    assert(manual_run->status == "completed");
    auto notifications = db.list_notifications(publisher.id, std::nullopt, 100);
    assert(!notifications.empty());
    assert(db.count_unread_notifications(publisher.id, std::nullopt) >= 1);
    assert(db.mark_notification_read(notifications[0].id));
    auto read_notification = db.get_notification(notifications[0].id);
    assert(read_notification.has_value());
    assert(!read_notification->read_at.empty());

    // Performance trigger + auto-pause action.
    adcontrol::AutomationWorkflow performance;
    performance.publisher_id = publisher.id;
    performance.site_id = site.id;
    performance.name = "Pause weak campaign";
    performance.status = "active";
    performance.trigger_type = "performance";
    performance.trigger_config = R"({
        "metric":"ctr","comparator":"lt","threshold":100,
        "lookback_hours":24,"cooldown_minutes":60,"min_impressions":0,
        "site_id":")" + site.id + R"("
    })";
    performance.created_by = owner.email;
    adcontrol::AutomationStep pause;
    pause.type = "action";
    pause.config = R"({"action":"pause_campaign","campaign_id":")" + campaign.id + R"("})";
    performance.steps.push_back(pause);
    assert(db.create_automation_workflow(performance, error));

    assert(db.evaluate_performance_triggers(now) >= 1);
    auto due = db.list_due_automation_runs(now, 50);
    assert(!due.empty());
    adcontrol::run_automation_tick(db, 50);
    auto paused_campaign = db.get_campaign(campaign.id);
    assert(paused_campaign.has_value());
    assert(paused_campaign->status == "paused");

    // Scheduled activation becomes a due run and advances its next occurrence.
    adcontrol::AutomationWorkflow scheduled;
    scheduled.publisher_id = publisher.id;
    scheduled.site_id = site.id;
    scheduled.name = "Scheduled notification";
    scheduled.status = "active";
    scheduled.trigger_type = "schedule";
    scheduled.trigger_config = R"({"run_at":")" + past + R"(","recurrence":"daily"})";
    scheduled.created_by = owner.email;
    adcontrol::AutomationStep scheduled_notify;
    scheduled_notify.type = "action";
    scheduled_notify.config = R"({"action":"send_notification","title":"Scheduled run fired","body":"Daily schedule"})";
    scheduled.steps.push_back(scheduled_notify);
    assert(db.create_automation_workflow(scheduled, error));
    assert(scheduled.next_run_at == past);
    assert(db.create_due_scheduled_runs(now) >= 1);
    auto scheduled_after = db.get_automation_workflow(scheduled.id);
    assert(scheduled_after.has_value());
    assert(scheduled_after->next_run_at > now);

    // Conversion events queue matching workflows exactly once.
    adcontrol::AutomationWorkflow conversion;
    conversion.publisher_id = publisher.id;
    conversion.site_id = site.id;
    conversion.name = "Conversion alert";
    conversion.status = "active";
    conversion.trigger_type = "conversion";
    conversion.trigger_config = R"({"event_name":"purchase"})";
    conversion.created_by = owner.email;
    adcontrol::AutomationStep conversion_notify;
    conversion_notify.type = "action";
    conversion_notify.config = R"({"action":"send_notification","title":"New purchase","body":"A tracked conversion arrived."})";
    conversion.steps.push_back(conversion_notify);
    assert(db.create_automation_workflow(conversion, error));

    const std::string conversion_payload = R"({"event_name":"purchase","value_cents":2500,"campaign_id":")" + campaign.id + R"("})";
    assert(db.enqueue_automation_event("conversion", publisher.id, site.id,
                                       conversion_payload, "conversion:test-1", error) == 1);
    assert(db.enqueue_automation_event("conversion", publisher.id, site.id,
                                       conversion_payload, "conversion:test-1", error) == 0);
    auto conversion_runs = db.list_automation_runs(conversion.id, publisher.id, 10);
    assert(conversion_runs.size() == 1);

    // Workflow deletion cascades through steps and runs.
    assert(db.delete_automation_workflow(conversion.id));
    auto deleted = db.get_automation_workflow(conversion.id);
    assert(!deleted.has_value());

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Automation workflows, schedules, performance triggers, worker, and notifications tests passed."
              << std::endl;
}

void test_crm_leads_and_tasks() {
    adcontrol::Database db;
    const std::string test_db = "test_crm_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::Publisher publisher;
    publisher.name = "CRM Publisher";
    publisher.status = "active";
    adcontrol::User owner;
    owner.email = "crm-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));

    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "CRM Site";
    site.primary_domain = "crm.test";
    site.allowed_domains = {"crm.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::PartnerInquiry inquiry;
    inquiry.kind = "client";
    inquiry.name = "Casey Client";
    inquiry.email = "casey@example.com";
    inquiry.company = "Casey Coffee";
    inquiry.website = "https://casey.example.com";
    inquiry.message = "We want to promote our coffee subscription with a six-week campaign across two placements.";
    inquiry.source = "partner_page";
    inquiry.utm_source = "newsletter";
    inquiry.utm_campaign = "autumn";
    assert(db.create_partner_inquiry(inquiry));

    adcontrol::CrmLead lead;
    bool created = false;
    std::string error;
    assert(db.create_crm_lead_from_inquiry(inquiry, lead, created, error));
    assert(created);
    assert(lead.stage == "new");
    assert(lead.score > 0);
    assert(lead.inquiry_id == inquiry.id);
    assert(lead.utm_source == "newsletter");
    assert(db.list_crm_activities(lead.id).size() == 1);

    adcontrol::CrmLead duplicate;
    assert(db.create_crm_lead_from_inquiry(inquiry, duplicate, created, error));
    assert(!created);
    assert(duplicate.id == lead.id);

    auto admin = db.get_user_by_email("admin@adcontrol.local");
    assert(admin.has_value());
    lead.owner_user_id = admin->id;
    lead.stage = "qualified";
    lead.value_cents = 250000;
    lead.next_follow_up_at = adcontrol::utils::iso8601_days_ago(-3);
    assert(db.update_crm_lead(lead, error));

    adcontrol::CrmActivity activity;
    activity.lead_id = lead.id;
    activity.type = "call";
    activity.subject = "Discovery call";
    activity.body = "Discussed campaign timing and budget.";
    activity.created_by = admin->email;
    assert(db.create_crm_activity(activity, error));
    auto after_activity = db.get_crm_lead(lead.id);
    assert(after_activity.has_value());
    assert(!after_activity->last_contact_at.empty());

    adcontrol::CrmTask task;
    task.lead_id = lead.id;
    task.title = "Send proposal";
    task.due_at = adcontrol::utils::iso8601_days_ago(-2);
    task.assigned_to = admin->email;
    assert(db.create_crm_task(task, error));
    auto with_task = db.get_crm_lead(lead.id);
    assert(with_task.has_value());
    assert(with_task->next_follow_up_at == task.due_at);

    adcontrol::CrmTask update_task = task;
    update_task.status = "done";
    assert(db.update_crm_task(update_task, error));
    auto after_task = db.get_crm_lead(lead.id);
    assert(after_task.has_value());
    assert(after_task->next_follow_up_at.empty());

    auto pipeline = db.get_crm_pipeline();
    assert(pipeline["stages"].is_array());
    auto summary = db.get_crm_summary();
    assert(summary["total_leads"] == 1);
    assert(summary["open_leads"] == 1);
    assert(summary["pipeline_value_cents"] == 250000);

    auto search = db.list_crm_leads(std::nullopt, "Casey", 20, 0);
    assert(search.size() == 1);
    assert(db.list_crm_leads("won", "", 20, 0).empty());

    assert(db.delete_crm_lead(lead.id));
    assert(!db.get_crm_lead(lead.id).has_value());

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] CRM inquiry conversion, scoring, activities, tasks, and pipeline tests passed."
              << std::endl;
}

void test_seo_geo_intelligence() {
    adcontrol::Database db;
    const std::string test_db = "test_seo_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::Publisher publisher;
    publisher.name = "SEO Publisher";
    publisher.status = "active";
    adcontrol::User owner;
    owner.email = "seo-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));

    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "SEO Site";
    site.primary_domain = "seo.test";
    site.allowed_domains = {"seo.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::SeoProject project;
    project.site_id = site.id;
    project.publisher_id = publisher.id;
    project.name = "SEO Project";
    project.domain = "seo.test";
    project.country = "US";
    project.language = "en";
    std::string error;
    assert(db.create_seo_project(project, error));
    assert(db.list_seo_projects(publisher.id).size() == 1);
    assert(db.list_seo_projects("other-publisher").empty());

    adcontrol::SeoKeyword keyword;
    keyword.project_id = project.id;
    keyword.keyword = "affiliate marketing";
    keyword.target_url = "https://seo.test/guide";
    keyword.intent = "informational";
    keyword.priority = 1;
    assert(db.create_seo_keyword(keyword, error));
    assert(keyword.current_position == 0);

    adcontrol::SeoRanking ranking;
    ranking.keyword_id = keyword.id;
    ranking.position = 8;
    ranking.url = keyword.target_url;
    ranking.source = "manual";
    adcontrol::SeoKeyword updated_keyword = keyword;
    assert(db.record_seo_ranking(updated_keyword, ranking, error));
    assert(updated_keyword.current_position == 8);
    assert(db.list_seo_rankings(keyword.id).size() == 1);

    ranking.position = 5;
    ranking.id.clear();
    ranking.created_at.clear();
    assert(db.record_seo_ranking(updated_keyword, ranking, error));
    assert(updated_keyword.previous_position == 8);
    assert(updated_keyword.current_position == 5);

    adcontrol::SeoKeyword second;
    second.project_id = project.id;
    second.keyword = "affiliate tools";
    second.target_url = "https://seo.test/tools";
    assert(db.create_seo_keyword(second, error));

    adcontrol::SeoCompetitor competitor;
    competitor.project_id = project.id;
    competitor.domain = "competitor.test";
    competitor.name = "Competitor";
    assert(db.create_seo_competitor(competitor, error));
    adcontrol::SeoCompetitorKeyword competitor_keyword;
    competitor_keyword.competitor_id = competitor.id;
    competitor_keyword.keyword = "affiliate marketing";
    competitor_keyword.position = 2;
    assert(db.create_seo_competitor_keyword(competitor_keyword, error));
    assert(db.list_seo_competitor_keywords(competitor.id).size() == 1);

    adcontrol::SeoBacklink backlink;
    backlink.project_id = project.id;
    backlink.source_url = "https://blog.example/post";
    backlink.target_url = keyword.target_url;
    backlink.anchor_text = "affiliate marketing guide";
    backlink.domain = "blog.example";
    backlink.status = "active";
    assert(db.create_seo_backlink(backlink, error));
    backlink.status = "lost";
    assert(db.update_seo_backlink(backlink));

    adcontrol::SeoAudit audit;
    audit.project_id = project.id;
    audit.target_url = "https://seo.test/";
    audit.score = 82;
    adcontrol::SeoAuditIssue issue;
    issue.severity = "medium";
    issue.category = "content";
    issue.code = "title_length";
    issue.message = "Title is too short";
    issue.recommendation = "Write a descriptive title";
    issue.page_url = audit.target_url;
    audit.issues.push_back(issue);
    assert(db.create_seo_audit(audit, error));
    auto stored_audit = db.get_seo_audit(audit.id, true);
    assert(stored_audit.has_value());
    assert(stored_audit->issues.size() == 1);
    assert(db.list_seo_audits(project.id).size() == 1);

    adcontrol::GeoQuery geo;
    geo.project_id = project.id;
    geo.query_text = "best affiliate marketing tools";
    geo.engines = {"chatgpt", "perplexity"};
    assert(db.create_geo_query(geo, error));
    adcontrol::GeoMention mention;
    mention.query_id = geo.id;
    mention.engine = "chatgpt";
    mention.mentioned = true;
    mention.position = 3;
    mention.snippet = "Recommended in the answer.";
    assert(db.create_geo_mention(mention, error));
    assert(db.list_geo_mentions(geo.id).size() == 1);

    auto summary = db.get_seo_summary(project.id);
    assert(summary["keywords"] == 2);
    assert(summary["tracked_keywords"] == 1);
    assert(summary["top10_keywords"] == 1);
    assert(summary["backlinks"] == 1);
    assert(summary["lost_backlinks"] == 1);
    assert(summary["latest_audit_score"] == 82);
    assert(summary["geo_checks"] == 1);
    assert(summary["geo_mentions"] == 1);
    assert(summary["geo_visibility_percent"] == 100.0);

    assert(db.delete_seo_project(project.id));
    assert(db.list_seo_keywords(project.id).empty());

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] SEO projects, keywords, rankings, competitors, backlinks, audits, and GEO tests passed."
              << std::endl;
}

void test_experiments_and_audience_segments() {
    adcontrol::Database db;
    const std::string test_db = "test_experiments_" + adcontrol::utils::random_hex(6) + ".db";
    assert(db.init(test_db));

    adcontrol::Publisher publisher;
    publisher.name = "Experiment Publisher";
    publisher.status = "active";
    adcontrol::User owner;
    owner.email = "experiment-owner@publisher.test";
    owner.salt = adcontrol::utils::random_hex(16);
    owner.password_hash = adcontrol::utils::hash_password("a-strong-password", owner.salt);
    assert(db.create_publisher_with_owner(publisher, owner));

    adcontrol::Site site;
    site.publisher_id = publisher.id;
    site.name = "Experiment Site";
    site.primary_domain = "experiment.test";
    site.allowed_domains = {"experiment.test"};
    site.status = "active";
    assert(db.create_site(site));

    adcontrol::Placement placement;
    placement.site_id = site.id;
    placement.placement_key = "experiment-slot";
    placement.name = "Experiment Slot";
    placement.format = "responsive_banner";
    placement.fallback_behavior = "hide";
    placement.status = "active";
    assert(db.create_placement(placement));

    auto make_campaign = [&](const std::string& name) {
        adcontrol::Campaign campaign;
        campaign.site_id = site.id;
        campaign.placement_id = placement.id;
        campaign.name = name;
        campaign.destination_url = "https://advertiser.example/" + name;
        campaign.format = "responsive_banner";
        campaign.source_type = "house";
        campaign.status = "active";
        campaign.weight = 100;
        assert(db.create_campaign(campaign));
        return campaign;
    };
    adcontrol::Campaign control = make_campaign("control");
    adcontrol::Campaign challenger = make_campaign("challenger");

    // True weighted rotation exposes every deliverable campaign, not just the
    // highest-weight campaign per placement.
    auto candidates = db.get_delivery_campaign_candidates(site.id, adcontrol::utils::get_current_iso8601());
    assert(candidates.size() == 2);

    adcontrol::AudienceSegment segment;
    segment.site_id = site.id;
    segment.publisher_id = publisher.id;
    segment.name = "Newsletter readers";
    segment.rules = R"({"utm_source":"newsletter","device":"desktop"})";
    std::string error;
    assert(db.create_audience_segment(segment, error));
    assert(db.list_audience_segments(site.id).size() == 1);

    control.audience_segment_id = segment.id;
    assert(db.update_campaign(control));
    auto scoped = db.get_campaign(control.id);
    assert(scoped.has_value());
    assert(scoped->audience_segment_id == segment.id);

    adcontrol::Experiment experiment;
    experiment.site_id = site.id;
    experiment.placement_id = placement.id;
    experiment.name = "Creative challenger";
    experiment.status = "running";
    experiment.objective = "ctr";
    experiment.min_impressions = 1;
    experiment.min_lift_percent = 10;
    experiment.auto_winner = true;
    adcontrol::ExperimentVariant control_variant;
    control_variant.campaign_id = control.id;
    control_variant.name = "Control";
    control_variant.weight = 100;
    control_variant.is_control = true;
    experiment.variants.push_back(control_variant);
    adcontrol::ExperimentVariant challenger_variant;
    challenger_variant.campaign_id = challenger.id;
    challenger_variant.name = "Challenger";
    challenger_variant.weight = 100;
    experiment.variants.push_back(challenger_variant);
    assert(db.create_experiment(experiment, error));
    assert(db.get_running_experiment_for_placement(placement.id).has_value());

    const std::string now = adcontrol::utils::get_current_iso8601();
    auto record = [&](const adcontrol::Campaign& campaign, const std::string& type, const std::string& id) {
        adcontrol::AdEvent event;
        event.site_id = site.id;
        event.placement_id = placement.id;
        event.campaign_id = campaign.id;
        event.source_type = "house";
        event.event_type = type;
        event.event_id = id;
        event.occurred_at = now;
        assert(db.record_event(event));
    };
    for (int i = 0; i < 10; ++i) {
        record(control, "impression", "exp-control-imp-" + std::to_string(i));
        record(challenger, "impression", "exp-challenger-imp-" + std::to_string(i));
    }
    for (int i = 0; i < 5; ++i) {
        record(control, "click", "exp-control-click-" + std::to_string(i));
    }
    record(challenger, "click", "exp-challenger-click-0");

    auto metrics = db.get_experiment_metrics(experiment.id);
    assert(metrics["variants"].size() == 2);
    assert(metrics["total_impressions"] == 20);
    for (const auto& row : metrics["variants"]) {
        assert(row["impressions"] == 10);
        if (row["campaign_id"] == control.id) assert(row["ctr"] == 50.0);
        if (row["campaign_id"] == challenger.id) assert(row["ctr"] == 10.0);
    }

    assert(db.optimize_experiments(now) == 1);
    auto completed = db.get_experiment(experiment.id);
    assert(completed.has_value());
    assert(completed->status == "completed");
    assert(completed->winner_campaign_id == control.id);
    auto winner_campaign = db.get_campaign(control.id);
    auto loser_campaign = db.get_campaign(challenger.id);
    assert(winner_campaign.has_value() && winner_campaign->status == "active");
    assert(loser_campaign.has_value() && loser_campaign->status == "paused");
    bool winner_weight_found = false;
    for (const auto& variant : completed->variants) {
        if (variant.campaign_id == control.id) {
            assert(variant.weight == 100);
            winner_weight_found = true;
        }
        if (variant.campaign_id == challenger.id) assert(variant.weight == 0);
    }
    assert(winner_weight_found);

    assert(db.delete_audience_segment(segment.id));
    auto unscoped = db.get_campaign(control.id);
    assert(unscoped.has_value());
    assert(unscoped->audience_segment_id.empty());

    db.close();
    std::remove(test_db.c_str());
    std::filesystem::remove(test_db + "-wal");
    std::filesystem::remove(test_db + "-shm");

    std::cout << "[PASS] Weighted rotation candidates, audience segments, experiment metrics, and auto-winner tests passed."
              << std::endl;
}

int main() {
    std::cout << "Running AdControl Unit Tests..." << std::endl;
    test_url_safety();
    test_domain_matching();
    test_schedule_check();
    test_password_and_crypto();
    test_secret_encryption();
    test_smtp_validation_and_message();
    test_database_and_deduplication();
    test_phase2_tenancy_and_affiliate_metrics();
    test_partner_inquiries();
    test_email_settings_persistence();
    test_monetization_billing_and_paid_learning();
    test_conversion_tracking_and_reconciliation();
    test_automation_workflows_and_worker();
    test_crm_leads_and_tasks();
    test_seo_geo_intelligence();
    test_experiments_and_audience_segments();
    test_online_backup();
    std::cout << "\nALL ADCONTROL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
