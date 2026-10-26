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
    assert(summary["provenance"]["conversions"] == "not_connected");

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

int main() {
    std::cout << "Running AdControl Unit Tests..." << std::endl;
    test_url_safety();
    test_domain_matching();
    test_schedule_check();
    test_password_and_crypto();
    test_database_and_deduplication();
    test_phase2_tenancy_and_affiliate_metrics();
    test_partner_inquiries();
    test_online_backup();
    std::cout << "\nALL ADCONTROL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
