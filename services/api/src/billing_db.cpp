#include "db.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace adcontrol {

namespace {

std::string text_col(sqlite3_stmt* stmt, int index) {
    const unsigned char* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

long long int64_col(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_int64(stmt, index);
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

std::string features_to_json(const std::vector<std::string>& features) {
    return nlohmann::json(features).dump();
}

std::vector<std::string> features_from_json(const std::string& raw) {
    std::vector<std::string> result;
    try {
        auto parsed = nlohmann::json::parse(raw);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (item.is_string()) result.push_back(item.get<std::string>());
            }
        }
    } catch (...) {}
    return result;
}

int clamp_limit(int limit, int fallback = 100, int maximum = 500) {
    if (limit <= 0) return fallback;
    return std::min(limit, maximum);
}

BillingPlan plan_from_row(sqlite3_stmt* stmt) {
    BillingPlan plan;
    plan.id = text_col(stmt, 0);
    plan.name = text_col(stmt, 1);
    plan.description = text_col(stmt, 2);
    plan.price_cents = sqlite3_column_int(stmt, 3);
    plan.currency = text_col(stmt, 4);
    plan.billing_interval = text_col(stmt, 5);
    plan.included_campaigns = sqlite3_column_int(stmt, 6);
    plan.included_impressions = sqlite3_column_int(stmt, 7);
    plan.overage_per_1000_impressions_cents = sqlite3_column_int(stmt, 8);
    plan.revenue_share_percent = sqlite3_column_int(stmt, 9);
    plan.features = features_from_json(text_col(stmt, 10));
    plan.status = text_col(stmt, 11);
    plan.sort_order = sqlite3_column_int(stmt, 12);
    plan.created_at = text_col(stmt, 13);
    plan.updated_at = text_col(stmt, 14);
    return plan;
}

const char* PLAN_SELECT =
    "SELECT id, name, description, price_cents, currency, billing_interval, "
    "included_campaigns, included_impressions, overage_per_1000_impressions_cents, "
    "revenue_share_percent, features, status, sort_order, created_at, updated_at "
    "FROM billing_plans ";

PublisherSubscription subscription_from_row(sqlite3_stmt* stmt) {
    PublisherSubscription subscription;
    subscription.id = text_col(stmt, 0);
    subscription.publisher_id = text_col(stmt, 1);
    subscription.plan_id = text_col(stmt, 2);
    subscription.status = text_col(stmt, 3);
    subscription.current_period_start = text_col(stmt, 4);
    subscription.current_period_end = text_col(stmt, 5);
    subscription.cancel_at_period_end = sqlite3_column_int(stmt, 6) != 0;
    subscription.created_at = text_col(stmt, 7);
    subscription.updated_at = text_col(stmt, 8);
    return subscription;
}

const char* SUBSCRIPTION_SELECT =
    "SELECT id, publisher_id, plan_id, status, current_period_start, "
    "current_period_end, cancel_at_period_end, created_at, updated_at "
    "FROM publisher_subscriptions ";

BillingInvoiceItem invoice_item_from_row(sqlite3_stmt* stmt) {
    BillingInvoiceItem item;
    item.id = text_col(stmt, 0);
    item.invoice_id = text_col(stmt, 1);
    item.kind = text_col(stmt, 2);
    item.description = text_col(stmt, 3);
    item.quantity = sqlite3_column_int(stmt, 4);
    item.unit_amount_cents = sqlite3_column_int(stmt, 5);
    item.amount_cents = sqlite3_column_int(stmt, 6);
    item.course_id = text_col(stmt, 7);
    item.metadata = text_col(stmt, 8);
    item.created_at = text_col(stmt, 9);
    return item;
}

const char* INVOICE_ITEM_SELECT =
    "SELECT id, invoice_id, kind, description, quantity, unit_amount_cents, "
    "amount_cents, COALESCE(course_id, ''), metadata, created_at "
    "FROM billing_invoice_items WHERE invoice_id = ? ORDER BY created_at, id;";

std::vector<BillingInvoiceItem> load_invoice_items(sqlite3* db, const std::string& invoice_id) {
    std::vector<BillingInvoiceItem> items;
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db, INVOICE_ITEM_SELECT, &stmt)) return items;
    bind_text(stmt, 1, invoice_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) items.push_back(invoice_item_from_row(stmt));
    finalize(stmt);
    return items;
}

BillingInvoice invoice_from_row(sqlite3_stmt* stmt) {
    BillingInvoice invoice;
    invoice.id = text_col(stmt, 0);
    invoice.publisher_id = text_col(stmt, 1);
    invoice.user_id = text_col(stmt, 2);
    invoice.context = text_col(stmt, 3);
    invoice.period_start = text_col(stmt, 4);
    invoice.period_end = text_col(stmt, 5);
    invoice.currency = text_col(stmt, 6);
    invoice.subtotal_cents = sqlite3_column_int(stmt, 7);
    invoice.tax_cents = sqlite3_column_int(stmt, 8);
    invoice.total_cents = sqlite3_column_int(stmt, 9);
    invoice.amount_paid_cents = sqlite3_column_int(stmt, 10);
    invoice.status = text_col(stmt, 11);
    invoice.due_at = text_col(stmt, 12);
    invoice.issued_at = text_col(stmt, 13);
    invoice.paid_at = text_col(stmt, 14);
    invoice.notes = text_col(stmt, 15);
    invoice.created_at = text_col(stmt, 16);
    invoice.updated_at = text_col(stmt, 17);
    return invoice;
}

const char* INVOICE_SELECT =
    "SELECT id, COALESCE(publisher_id, ''), COALESCE(user_id, ''), context, "
    "period_start, period_end, currency, subtotal_cents, tax_cents, total_cents, "
    "amount_paid_cents, status, COALESCE(due_at, ''), COALESCE(issued_at, ''), "
    "COALESCE(paid_at, ''), COALESCE(notes, ''), created_at, updated_at "
    "FROM billing_invoices ";

std::optional<BillingInvoice> load_invoice(sqlite3* db, const std::string& id,
                                           bool include_items = true) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(INVOICE_SELECT) + "WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<BillingInvoice> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = invoice_from_row(stmt);
        if (include_items) result->items = load_invoice_items(db, result->id);
    }
    finalize(stmt);
    return result;
}

BillingPayment payment_from_row(sqlite3_stmt* stmt) {
    BillingPayment payment;
    payment.id = text_col(stmt, 0);
    payment.invoice_id = text_col(stmt, 1);
    payment.publisher_id = text_col(stmt, 2);
    payment.user_id = text_col(stmt, 3);
    payment.amount_cents = sqlite3_column_int(stmt, 4);
    payment.currency = text_col(stmt, 5);
    payment.method = text_col(stmt, 6);
    payment.reference = text_col(stmt, 7);
    payment.notes = text_col(stmt, 8);
    payment.received_at = text_col(stmt, 9);
    payment.recorded_by = text_col(stmt, 10);
    payment.created_at = text_col(stmt, 11);
    return payment;
}

const char* PAYMENT_SELECT =
    "SELECT id, invoice_id, COALESCE(publisher_id, ''), COALESCE(user_id, ''), "
    "amount_cents, currency, method, reference, notes, received_at, "
    "recorded_by, created_at FROM billing_payments ";

CourseEntitlement entitlement_from_row(sqlite3_stmt* stmt) {
    CourseEntitlement entitlement;
    entitlement.id = text_col(stmt, 0);
    entitlement.user_id = text_col(stmt, 1);
    entitlement.course_id = text_col(stmt, 2);
    entitlement.invoice_id = text_col(stmt, 3);
    entitlement.status = text_col(stmt, 4);
    entitlement.granted_at = text_col(stmt, 5);
    entitlement.revoked_at = text_col(stmt, 6);
    entitlement.created_at = text_col(stmt, 7);
    return entitlement;
}

const char* ENTITLEMENT_SELECT =
    "SELECT id, user_id, course_id, COALESCE(invoice_id, ''), status, "
    "granted_at, COALESCE(revoked_at, ''), created_at "
    "FROM learning_course_entitlements ";

PublisherPayout payout_from_row(sqlite3_stmt* stmt) {
    PublisherPayout payout;
    payout.id = text_col(stmt, 0);
    payout.publisher_id = text_col(stmt, 1);
    payout.period_start = text_col(stmt, 2);
    payout.period_end = text_col(stmt, 3);
    payout.amount_cents = int64_col(stmt, 4);
    payout.currency = text_col(stmt, 5);
    payout.method = text_col(stmt, 6);
    payout.reference = text_col(stmt, 7);
    payout.status = text_col(stmt, 8);
    payout.paid_at = text_col(stmt, 9);
    payout.recorded_by = text_col(stmt, 10);
    payout.notes = text_col(stmt, 11);
    payout.created_at = text_col(stmt, 12);
    return payout;
}

const char* PAYOUT_SELECT =
    "SELECT id, publisher_id, period_start, period_end, amount_cents, currency, "
    "method, reference, status, COALESCE(paid_at, ''), recorded_by, notes, created_at "
    "FROM publisher_payouts ";

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool grant_entitlement_locked(sqlite3* db,
                              const std::string& user_id,
                              const std::string& course_id,
                              const std::string& invoice_id,
                              std::string& error) {
    if (user_id.empty() || course_id.empty()) {
        error = "A learner and course are required";
        return false;
    }
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO learning_course_entitlements
            (id, user_id, course_id, invoice_id, status, granted_at, revoked_at, created_at)
        VALUES (?, ?, ?, ?, 'active', ?, NULL, ?)
        ON CONFLICT(user_id, course_id) DO UPDATE SET
            invoice_id = excluded.invoice_id,
            status = 'active',
            granted_at = excluded.granted_at,
            revoked_at = NULL;
    )";
    if (!prepare(db, sql, &stmt)) {
        error = "Could not grant course access";
        return false;
    }
    bind_text(stmt, 1, utils::generate_uuid());
    bind_text(stmt, 2, user_id);
    bind_text(stmt, 3, course_id);
    if (invoice_id.empty()) sqlite3_bind_null(stmt, 4);
    else bind_text(stmt, 4, invoice_id);
    bind_text(stmt, 5, now);
    bind_text(stmt, 6, now);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not grant course access";
        return false;
    }
    return true;
}

std::optional<BillingPlan> load_plan(sqlite3* db, const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(PLAN_SELECT) + "WHERE id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, id);
    std::optional<BillingPlan> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = plan_from_row(stmt);
    finalize(stmt);
    return result;
}

std::optional<PublisherSubscription> load_subscription(sqlite3* db, const std::string& publisher_id) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(SUBSCRIPTION_SELECT) + "WHERE publisher_id = ? LIMIT 1;";
    if (!prepare(db, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, publisher_id);
    std::optional<PublisherSubscription> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = subscription_from_row(stmt);
    finalize(stmt);
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Plans
// ---------------------------------------------------------------------------

bool Database::seed_billing_plans() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    struct Seed {
        const char* id;
        const char* name;
        const char* description;
        int price_cents;
        int campaigns;
        int impressions;
        int overage;
        int share;
        const char* features;
        int sort_order;
    };
    const Seed seeds[] = {
        {"free", "Free", "Start monetizing a single site with core delivery and reporting.",
         0, 3, 50000, 0, 10,
         R"(["Up to 3 campaigns","50,000 impressions per month","10% publisher revenue share","Community support"])",
         10},
        {"pro", "Pro", "For growing publishers who need more inventory and overage billing.",
         2900, 25, 500000, 500, 15,
         R"(["Up to 25 campaigns","500,000 impressions per month","$5 per additional 1,000 impressions","15% publisher revenue share","Priority email support"])",
         20},
        {"business", "Business", "High-volume delivery with predictable overage pricing.",
         9900, 100, 2000000, 300, 20,
         R"(["Up to 100 campaigns","2,000,000 impressions per month","$3 per additional 1,000 impressions","20% publisher revenue share","Dedicated onboarding"])",
         30},
        {"enterprise", "Enterprise", "Unlimited inventory with a negotiated revenue-share agreement.",
         49900, -1, -1, 0, 25,
         R"(["Unlimited campaigns","Unlimited impressions","25% publisher revenue share","Account manager","Custom terms"])",
         40},
    };

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO billing_plans
            (id, name, description, price_cents, currency, billing_interval,
             included_campaigns, included_impressions,
             overage_per_1000_impressions_cents, revenue_share_percent,
             features, status, sort_order, created_at, updated_at)
        VALUES (?, ?, ?, ?, 'USD', 'month', ?, ?, ?, ?, ?, 'active', ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        std::cerr << "Could not prepare billing plan seed" << std::endl;
        return false;
    }
    bool ok = true;
    for (const auto& seed : seeds) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bind_text(stmt, 1, seed.id);
        bind_text(stmt, 2, seed.name);
        bind_text(stmt, 3, seed.description);
        sqlite3_bind_int(stmt, 4, seed.price_cents);
        sqlite3_bind_int(stmt, 5, seed.campaigns);
        sqlite3_bind_int(stmt, 6, seed.impressions);
        sqlite3_bind_int(stmt, 7, seed.overage);
        sqlite3_bind_int(stmt, 8, seed.share);
        bind_text(stmt, 9, seed.features);
        sqlite3_bind_int(stmt, 10, seed.sort_order);
        bind_text(stmt, 11, now);
        bind_text(stmt, 12, now);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Could not seed billing plan " << seed.id << ": "
                      << sqlite3_errmsg(db_) << std::endl;
            ok = false;
            break;
        }
    }
    finalize(stmt);
    return ok;
}

std::vector<BillingPlan> Database::list_billing_plans(bool active_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BillingPlan> plans;
    std::string sql = std::string(PLAN_SELECT);
    if (active_only) sql += "WHERE status = 'active' ";
    sql += "ORDER BY sort_order, price_cents, name;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return plans;
    while (sqlite3_step(stmt) == SQLITE_ROW) plans.push_back(plan_from_row(stmt));
    finalize(stmt);
    return plans;
}

std::optional<BillingPlan> Database::get_billing_plan(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_plan(db_, id);
}

bool Database::update_billing_plan(const BillingPlan& plan) {
    if (plan.id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE billing_plans SET
            name = ?, description = ?, price_cents = ?, currency = ?,
            billing_interval = ?, included_campaigns = ?, included_impressions = ?,
            overage_per_1000_impressions_cents = ?, revenue_share_percent = ?,
            features = ?, status = ?, sort_order = ?, updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, plan.name);
    bind_text(stmt, 2, plan.description);
    sqlite3_bind_int(stmt, 3, std::max(0, plan.price_cents));
    bind_text(stmt, 4, plan.currency.empty() ? "USD" : plan.currency);
    bind_text(stmt, 5, plan.billing_interval.empty() ? "month" : plan.billing_interval);
    sqlite3_bind_int(stmt, 6, plan.included_campaigns);
    sqlite3_bind_int(stmt, 7, plan.included_impressions);
    sqlite3_bind_int(stmt, 8, std::max(0, plan.overage_per_1000_impressions_cents));
    sqlite3_bind_int(stmt, 9, std::min(100, std::max(0, plan.revenue_share_percent)));
    bind_text(stmt, 10, features_to_json(plan.features));
    bind_text(stmt, 11, plan.status.empty() ? "active" : plan.status);
    sqlite3_bind_int(stmt, 12, plan.sort_order);
    bind_text(stmt, 13, now);
    bind_text(stmt, 14, plan.id);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

// ---------------------------------------------------------------------------
// Subscriptions and usage
// ---------------------------------------------------------------------------

std::optional<PublisherSubscription> Database::get_publisher_subscription(const std::string& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_subscription(db_, publisher_id);
}

std::optional<PublisherSubscription> Database::ensure_publisher_subscription(
    const std::string& publisher_id, std::string& error) {
    if (publisher_id.empty()) {
        error = "A publisher is required";
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto existing = load_subscription(db_, publisher_id);
    if (existing) return existing;

    sqlite3_stmt* check = nullptr;
    if (prepare(db_, "SELECT 1 FROM publishers WHERE id = ? LIMIT 1;", &check)) {
        bind_text(check, 1, publisher_id);
        const bool found = sqlite3_step(check) == SQLITE_ROW;
        finalize(check);
        if (!found) {
            error = "Publisher not found";
            return std::nullopt;
        }
    }

    auto free_plan = load_plan(db_, "free");
    if (!free_plan) {
        error = "The Free billing plan is not configured";
        return std::nullopt;
    }

    const std::time_t now = utils::utc_now_epoch();
    PublisherSubscription subscription;
    subscription.id = utils::generate_uuid();
    subscription.publisher_id = publisher_id;
    subscription.plan_id = free_plan->id;
    subscription.status = "active";
    subscription.current_period_start = utils::month_start_iso(now);
    subscription.current_period_end = utils::month_end_iso(now);
    subscription.created_at = utils::get_current_iso8601();
    subscription.updated_at = subscription.created_at;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO publisher_subscriptions
            (id, publisher_id, plan_id, status, current_period_start,
             current_period_end, cancel_at_period_end, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not create the subscription";
        return std::nullopt;
    }
    bind_text(stmt, 1, subscription.id);
    bind_text(stmt, 2, subscription.publisher_id);
    bind_text(stmt, 3, subscription.plan_id);
    bind_text(stmt, 4, subscription.status);
    bind_text(stmt, 5, subscription.current_period_start);
    bind_text(stmt, 6, subscription.current_period_end);
    bind_text(stmt, 7, subscription.created_at);
    bind_text(stmt, 8, subscription.updated_at);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not create the subscription";
        return std::nullopt;
    }
    // A concurrent request may have won the UNIQUE race; always return the row
    // that is actually stored.
    auto stored = load_subscription(db_, publisher_id);
    if (!stored) error = "Could not load the subscription";
    return stored;
}

std::vector<PublisherSubscription> Database::list_publisher_subscriptions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PublisherSubscription> subscriptions;
    std::string sql = std::string(SUBSCRIPTION_SELECT) + "ORDER BY created_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return subscriptions;
    while (sqlite3_step(stmt) == SQLITE_ROW) subscriptions.push_back(subscription_from_row(stmt));
    finalize(stmt);
    return subscriptions;
}

bool Database::update_publisher_subscription_plan(const std::string& publisher_id,
                                                  const std::string& plan_id,
                                                  std::string& error) {
    if (publisher_id.empty() || plan_id.empty()) {
        error = "A publisher and plan are required";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto plan = load_plan(db_, plan_id);
    if (!plan || plan->status != "active") {
        error = "Plan not found or inactive";
        return false;
    }

    auto existing = load_subscription(db_, publisher_id);
    const std::string now = utils::get_current_iso8601();
    if (!existing) {
        sqlite3_stmt* check = nullptr;
        if (prepare(db_, "SELECT 1 FROM publishers WHERE id = ? LIMIT 1;", &check)) {
            bind_text(check, 1, publisher_id);
            const bool found = sqlite3_step(check) == SQLITE_ROW;
            finalize(check);
            if (!found) {
                error = "Publisher not found";
                return false;
            }
        }
        const std::time_t epoch = utils::utc_now_epoch();
        PublisherSubscription subscription;
        subscription.id = utils::generate_uuid();
        subscription.publisher_id = publisher_id;
        subscription.plan_id = plan_id;
        subscription.status = "active";
        subscription.current_period_start = utils::month_start_iso(epoch);
        subscription.current_period_end = utils::month_end_iso(epoch);
        subscription.created_at = now;
        subscription.updated_at = now;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            INSERT INTO publisher_subscriptions
                (id, publisher_id, plan_id, status, current_period_start,
                 current_period_end, cancel_at_period_end, created_at, updated_at)
            VALUES (?, ?, ?, 'active', ?, ?, 0, ?, ?);
        )";
        if (!prepare(db_, sql, &stmt)) {
            error = "Could not create the subscription";
            return false;
        }
        bind_text(stmt, 1, subscription.id);
        bind_text(stmt, 2, subscription.publisher_id);
        bind_text(stmt, 3, subscription.plan_id);
        bind_text(stmt, 4, subscription.current_period_start);
        bind_text(stmt, 5, subscription.current_period_end);
        bind_text(stmt, 6, subscription.created_at);
        bind_text(stmt, 7, subscription.updated_at);
        const int rc = sqlite3_step(stmt);
        finalize(stmt);
        if (rc != SQLITE_DONE) {
            error = "Could not create the subscription";
            return false;
        }
        return true;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE publisher_subscriptions SET
            plan_id = ?, status = 'active', cancel_at_period_end = 0, updated_at = ?
        WHERE publisher_id = ?;
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not update the subscription";
        return false;
    }
    bind_text(stmt, 1, plan_id);
    bind_text(stmt, 2, now);
    bind_text(stmt, 3, publisher_id);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not update the subscription";
        return false;
    }
    return true;
}

long long Database::count_publisher_impressions(const std::string& publisher_id,
                                                 const std::string& period_start,
                                                 const std::string& period_end) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT COUNT(*) FROM ad_events e
        JOIN sites s ON s.id = e.site_id
        WHERE s.publisher_id = ? AND e.event_type = 'impression'
          AND e.occurred_at >= ? AND e.occurred_at < ?;
    )";
    if (!prepare(db_, sql, &stmt)) return 0;
    bind_text(stmt, 1, publisher_id);
    bind_text(stmt, 2, period_start);
    bind_text(stmt, 3, period_end);
    long long count = sqlite3_step(stmt) == SQLITE_ROW ? int64_col(stmt, 0) : 0;
    finalize(stmt);
    return count;
}

int Database::count_publisher_campaigns(const std::string& publisher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT COUNT(*) FROM campaigns c
        JOIN sites s ON s.id = c.site_id
        WHERE s.publisher_id = ? AND c.status <> 'archived';
    )";
    if (!prepare(db_, sql, &stmt)) return 0;
    bind_text(stmt, 1, publisher_id);
    const int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
    finalize(stmt);
    return count;
}

PublisherUsage Database::get_publisher_usage(const std::string& publisher_id,
                                              const std::string& period_start,
                                              const std::string& period_end) {
    PublisherUsage usage;
    usage.period_start = period_start;
    usage.period_end = period_end;
    usage.impressions = count_publisher_impressions(publisher_id, period_start, period_end);
    usage.campaigns = count_publisher_campaigns(publisher_id);

    std::string error;
    auto subscription = ensure_publisher_subscription(publisher_id, error);
    auto plan = subscription ? get_billing_plan(subscription->plan_id) : std::nullopt;
    if (plan) {
        usage.included_impressions = plan->included_impressions;
        usage.included_campaigns = plan->included_campaigns;
        usage.revenue_share_percent = plan->revenue_share_percent;
        usage.hard_cap = plan->included_impressions >= 0 &&
                         plan->overage_per_1000_impressions_cents <= 0;
        if (plan->included_impressions >= 0 &&
            usage.impressions > plan->included_impressions) {
            if (plan->overage_per_1000_impressions_cents > 0) {
                usage.overage_impressions = usage.impressions - plan->included_impressions;
                const long long blocks = (usage.overage_impressions + 999) / 1000;
                usage.overage_cents = blocks * plan->overage_per_1000_impressions_cents;
            } else {
                usage.overage_impressions = usage.impressions - plan->included_impressions;
            }
        }
        usage.impression_quota_exceeded =
            plan->included_impressions >= 0 && usage.impressions >= plan->included_impressions;
        usage.campaign_quota_exceeded =
            plan->included_campaigns >= 0 && usage.campaigns >= plan->included_campaigns;
    }
    return usage;
}

// ---------------------------------------------------------------------------
// Invoices and payments
// ---------------------------------------------------------------------------

bool Database::create_billing_invoice(BillingInvoice& invoice, std::string& error) {
    if (invoice.items.empty()) {
        error = "An invoice needs at least one line item";
        return false;
    }
    if (invoice.publisher_id.empty() && invoice.user_id.empty()) {
        error = "An invoice needs a publisher or learner";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    if (invoice.id.empty()) invoice.id = utils::generate_uuid();
    invoice.created_at = now;
    invoice.updated_at = now;
    if (invoice.context.empty()) invoice.context = invoice.publisher_id.empty() ? "course" : "subscription";
    if (invoice.currency.empty()) invoice.currency = "USD";
    if (invoice.status.empty()) invoice.status = "open";
    if (invoice.issued_at.empty()) invoice.issued_at = now;

    int subtotal = 0;
    for (auto& item : invoice.items) {
        if (item.id.empty()) item.id = utils::generate_uuid();
        item.invoice_id = invoice.id;
        if (item.created_at.empty()) item.created_at = now;
        if (item.amount_cents == 0 && item.quantity > 0) {
            item.amount_cents = item.quantity * item.unit_amount_cents;
        }
        if (item.amount_cents < 0) {
            error = "Invoice line amounts cannot be negative";
            return false;
        }
        subtotal += item.amount_cents;
    }
    invoice.subtotal_cents = subtotal;
    if (invoice.tax_cents < 0) invoice.tax_cents = 0;
    invoice.total_cents = invoice.subtotal_cents + invoice.tax_cents;
    if (invoice.amount_paid_cents < 0) invoice.amount_paid_cents = 0;
    if (invoice.amount_paid_cents >= invoice.total_cents) {
        invoice.status = "paid";
        if (invoice.paid_at.empty()) invoice.paid_at = now;
    } else if (invoice.amount_paid_cents > 0) {
        invoice.status = "partially_paid";
    }

    if (!exec(db_, "BEGIN IMMEDIATE;")) {
        error = "Could not start the invoice transaction";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* invoice_sql = R"(
        INSERT INTO billing_invoices
            (id, publisher_id, user_id, context, period_start, period_end, currency,
             subtotal_cents, tax_cents, total_cents, amount_paid_cents, status,
             due_at, issued_at, paid_at, notes, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, invoice_sql, &stmt)) {
        exec(db_, "ROLLBACK;");
        error = "Could not create the invoice";
        return false;
    }
    bind_text(stmt, 1, invoice.id);
    if (invoice.publisher_id.empty()) sqlite3_bind_null(stmt, 2);
    else bind_text(stmt, 2, invoice.publisher_id);
    if (invoice.user_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, invoice.user_id);
    bind_text(stmt, 4, invoice.context);
    bind_text(stmt, 5, invoice.period_start);
    bind_text(stmt, 6, invoice.period_end);
    bind_text(stmt, 7, invoice.currency);
    sqlite3_bind_int(stmt, 8, invoice.subtotal_cents);
    sqlite3_bind_int(stmt, 9, invoice.tax_cents);
    sqlite3_bind_int(stmt, 10, invoice.total_cents);
    sqlite3_bind_int(stmt, 11, invoice.amount_paid_cents);
    bind_text(stmt, 12, invoice.status);
    if (invoice.due_at.empty()) sqlite3_bind_null(stmt, 13);
    else bind_text(stmt, 13, invoice.due_at);
    bind_text(stmt, 14, invoice.issued_at);
    if (invoice.paid_at.empty()) sqlite3_bind_null(stmt, 15);
    else bind_text(stmt, 15, invoice.paid_at);
    bind_text(stmt, 16, invoice.notes);
    bind_text(stmt, 17, invoice.created_at);
    bind_text(stmt, 18, invoice.updated_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        exec(db_, "ROLLBACK;");
        error = "Could not create the invoice";
        return false;
    }

    const char* item_sql = R"(
        INSERT INTO billing_invoice_items
            (id, invoice_id, kind, description, quantity, unit_amount_cents,
             amount_cents, course_id, metadata, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    for (const auto& item : invoice.items) {
        if (!prepare(db_, item_sql, &stmt)) {
            exec(db_, "ROLLBACK;");
            error = "Could not create the invoice lines";
            return false;
        }
        bind_text(stmt, 1, item.id);
        bind_text(stmt, 2, item.invoice_id);
        bind_text(stmt, 3, item.kind.empty() ? "adjustment" : item.kind);
        bind_text(stmt, 4, item.description);
        sqlite3_bind_int(stmt, 5, std::max(1, item.quantity));
        sqlite3_bind_int(stmt, 6, item.unit_amount_cents);
        sqlite3_bind_int(stmt, 7, item.amount_cents);
        if (item.course_id.empty()) sqlite3_bind_null(stmt, 8);
        else bind_text(stmt, 8, item.course_id);
        bind_text(stmt, 9, item.metadata.empty() ? "{}" : item.metadata);
        bind_text(stmt, 10, item.created_at);
        rc = sqlite3_step(stmt);
        finalize(stmt);
        if (rc != SQLITE_DONE) {
            exec(db_, "ROLLBACK;");
            error = "Could not create the invoice lines";
            return false;
        }
    }

    if (!exec(db_, "COMMIT;")) {
        exec(db_, "ROLLBACK;");
        error = "Could not commit the invoice";
        return false;
    }

    // A zero-total invoice (fully discounted course) is already settled and
    // grants access immediately.
    if (invoice.total_cents == 0) {
        for (const auto& item : invoice.items) {
            if (item.course_id.empty()) continue;
            std::string grant_error;
            if (!grant_entitlement_locked(db_, invoice.user_id, item.course_id,
                                          invoice.id, grant_error)) {
                error = grant_error;
                return false;
            }
        }
    }
    return true;
}

std::optional<BillingInvoice> Database::get_billing_invoice(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return load_invoice(db_, id, true);
}

std::optional<BillingInvoice> Database::find_billing_invoice(
    const std::string& publisher_id,
    const std::string& user_id,
    const std::string& context,
    const std::string& period_start,
    const std::string& period_end,
    bool open_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = std::string(INVOICE_SELECT) + "WHERE 1=1 ";
    if (!publisher_id.empty()) sql += "AND publisher_id = ? ";
    else sql += "AND (publisher_id IS NULL OR publisher_id = '') ";
    if (!user_id.empty()) sql += "AND user_id = ? ";
    else sql += "AND (user_id IS NULL OR user_id = '') ";
    if (!context.empty()) sql += "AND context = ? ";
    if (!period_start.empty()) sql += "AND period_start = ? ";
    if (!period_end.empty()) sql += "AND period_end = ? ";
    if (open_only) sql += "AND status IN ('open', 'partially_paid', 'overdue') ";
    sql += "ORDER BY created_at DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    int index = 1;
    if (!publisher_id.empty()) bind_text(stmt, index++, publisher_id);
    if (!user_id.empty()) bind_text(stmt, index++, user_id);
    if (!context.empty()) bind_text(stmt, index++, context);
    if (!period_start.empty()) bind_text(stmt, index++, period_start);
    if (!period_end.empty()) bind_text(stmt, index++, period_end);
    std::optional<BillingInvoice> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = invoice_from_row(stmt);
        if (result) result->items = load_invoice_items(db_, result->id);
    }
    finalize(stmt);
    return result;
}

std::vector<BillingInvoice> Database::list_billing_invoices(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& user_id,
    const std::optional<std::string>& context,
    const std::optional<std::string>& status,
    int limit,
    int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BillingInvoice> invoices;
    std::string sql = std::string(INVOICE_SELECT) + "WHERE 1=1 ";
    if (publisher_id) sql += "AND publisher_id = ? ";
    if (user_id) sql += "AND user_id = ? ";
    if (context) sql += "AND context = ? ";
    if (status) sql += "AND status = ? ";
    sql += "ORDER BY created_at DESC LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return invoices;
    int index = 1;
    if (publisher_id) bind_text(stmt, index++, *publisher_id);
    if (user_id) bind_text(stmt, index++, *user_id);
    if (context) bind_text(stmt, index++, *context);
    if (status) bind_text(stmt, index++, *status);
    sqlite3_bind_int(stmt, index++, clamp_limit(limit));
    sqlite3_bind_int(stmt, index++, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BillingInvoice invoice = invoice_from_row(stmt);
        invoice.items = load_invoice_items(db_, invoice.id);
        invoices.push_back(std::move(invoice));
    }
    finalize(stmt);
    return invoices;
}

bool Database::record_billing_payment(BillingPayment& payment, std::string& error) {
    if (payment.invoice_id.empty()) {
        error = "An invoice is required";
        return false;
    }
    if (payment.amount_cents <= 0) {
        error = "Payment amount must be greater than zero";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto invoice = load_invoice(db_, payment.invoice_id, true);
    if (!invoice) {
        error = "Invoice not found";
        return false;
    }
    if (invoice->status == "void") {
        error = "A void invoice cannot receive payments";
        return false;
    }
    if (invoice->status == "paid") {
        error = "This invoice is already paid";
        return false;
    }
    const int balance = std::max(0, invoice->total_cents - invoice->amount_paid_cents);
    if (payment.amount_cents > balance) {
        error = "Payment exceeds the outstanding balance";
        return false;
    }

    const std::string now = utils::get_current_iso8601();
    if (payment.id.empty()) payment.id = utils::generate_uuid();
    if (payment.currency.empty()) payment.currency = invoice->currency;
    if (payment.currency != invoice->currency) {
        error = "Payment currency must match the invoice currency";
        return false;
    }
    if (payment.method.empty()) payment.method = "manual";
    if (payment.received_at.empty()) payment.received_at = now;
    payment.created_at = now;
    payment.publisher_id = invoice->publisher_id;
    payment.user_id = invoice->user_id;

    if (!exec(db_, "BEGIN IMMEDIATE;")) {
        error = "Could not start the payment transaction";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO billing_payments
            (id, invoice_id, publisher_id, user_id, amount_cents, currency,
             method, reference, notes, received_at, recorded_by, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        exec(db_, "ROLLBACK;");
        error = "Could not record the payment";
        return false;
    }
    bind_text(stmt, 1, payment.id);
    bind_text(stmt, 2, payment.invoice_id);
    if (payment.publisher_id.empty()) sqlite3_bind_null(stmt, 3);
    else bind_text(stmt, 3, payment.publisher_id);
    if (payment.user_id.empty()) sqlite3_bind_null(stmt, 4);
    else bind_text(stmt, 4, payment.user_id);
    sqlite3_bind_int(stmt, 5, payment.amount_cents);
    bind_text(stmt, 6, payment.currency);
    bind_text(stmt, 7, payment.method);
    bind_text(stmt, 8, payment.reference);
    bind_text(stmt, 9, payment.notes);
    bind_text(stmt, 10, payment.received_at);
    bind_text(stmt, 11, payment.recorded_by);
    bind_text(stmt, 12, payment.created_at);
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        exec(db_, "ROLLBACK;");
        error = "Could not record the payment";
        return false;
    }

    const int new_paid = invoice->amount_paid_cents + payment.amount_cents;
    const bool fully_paid = new_paid >= invoice->total_cents;
    const std::string new_status = fully_paid ? "paid" : "partially_paid";

    const char* update_sql = R"(
        UPDATE billing_invoices SET
            amount_paid_cents = ?, status = ?,
            paid_at = CASE WHEN ? = 'paid' THEN COALESCE(NULLIF(paid_at, ''), ?) ELSE paid_at END,
            updated_at = ?
        WHERE id = ?;
    )";
    if (!prepare(db_, update_sql, &stmt)) {
        exec(db_, "ROLLBACK;");
        error = "Could not update the invoice";
        return false;
    }
    sqlite3_bind_int(stmt, 1, new_paid);
    bind_text(stmt, 2, new_status);
    bind_text(stmt, 3, new_status);
    bind_text(stmt, 4, now);
    bind_text(stmt, 5, now);
    bind_text(stmt, 6, invoice->id);
    rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        exec(db_, "ROLLBACK;");
        error = "Could not update the invoice";
        return false;
    }

    // Course purchases grant access in the same transaction as settlement.
    if (fully_paid) {
        for (const auto& item : invoice->items) {
            if (item.course_id.empty()) continue;
            if (!grant_entitlement_locked(db_, invoice->user_id, item.course_id,
                                          invoice->id, error)) {
                exec(db_, "ROLLBACK;");
                return false;
            }
        }
    }

    if (!exec(db_, "COMMIT;")) {
        exec(db_, "ROLLBACK;");
        error = "Could not commit the payment";
        return false;
    }
    return true;
}

std::vector<BillingPayment> Database::list_billing_payments(
    const std::optional<std::string>& publisher_id,
    const std::optional<std::string>& user_id,
    const std::optional<std::string>& invoice_id,
    int limit,
    int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BillingPayment> payments;
    std::string sql = std::string(PAYMENT_SELECT) + "WHERE 1=1 ";
    if (publisher_id) sql += "AND publisher_id = ? ";
    if (user_id) sql += "AND user_id = ? ";
    if (invoice_id) sql += "AND invoice_id = ? ";
    sql += "ORDER BY received_at DESC, created_at DESC LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return payments;
    int index = 1;
    if (publisher_id) bind_text(stmt, index++, *publisher_id);
    if (user_id) bind_text(stmt, index++, *user_id);
    if (invoice_id) bind_text(stmt, index++, *invoice_id);
    sqlite3_bind_int(stmt, index++, clamp_limit(limit));
    sqlite3_bind_int(stmt, index++, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) payments.push_back(payment_from_row(stmt));
    finalize(stmt);
    return payments;
}

bool Database::void_billing_invoice(const std::string& invoice_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE billing_invoices SET status = 'void', updated_at = ?
        WHERE id = ? AND status IN ('open', 'partially_paid', 'overdue');
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, now);
    bind_text(stmt, 2, invoice_id);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

// ---------------------------------------------------------------------------
// Course entitlements
// ---------------------------------------------------------------------------

bool Database::grant_course_entitlement(const std::string& user_id,
                                         const std::string& course_id,
                                         const std::string& invoice_id,
                                         std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return grant_entitlement_locked(db_, user_id, course_id, invoice_id, error);
}

std::optional<CourseEntitlement> Database::get_course_entitlement(const std::string& user_id,
                                                                   const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string(ENTITLEMENT_SELECT) +
                      "WHERE user_id = ? AND course_id = ? LIMIT 1;";
    if (!prepare(db_, sql, &stmt)) return std::nullopt;
    bind_text(stmt, 1, user_id);
    bind_text(stmt, 2, course_id);
    std::optional<CourseEntitlement> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = entitlement_from_row(stmt);
    finalize(stmt);
    return result;
}

std::vector<CourseEntitlement> Database::list_user_entitlements(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CourseEntitlement> entitlements;
    std::string sql = std::string(ENTITLEMENT_SELECT) +
                      "WHERE user_id = ? AND status = 'active' ORDER BY granted_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return entitlements;
    bind_text(stmt, 1, user_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) entitlements.push_back(entitlement_from_row(stmt));
    finalize(stmt);
    return entitlements;
}

bool Database::revoke_course_entitlement(const std::string& user_id, const std::string& course_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string now = utils::get_current_iso8601();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE learning_course_entitlements SET status = 'revoked', revoked_at = ?
        WHERE user_id = ? AND course_id = ? AND status = 'active';
    )";
    if (!prepare(db_, sql, &stmt)) return false;
    bind_text(stmt, 1, now);
    bind_text(stmt, 2, user_id);
    bind_text(stmt, 3, course_id);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

// ---------------------------------------------------------------------------
// Publisher revenue share and payouts
// ---------------------------------------------------------------------------

PublisherRevenueSummary Database::get_publisher_revenue(const std::string& publisher_id,
                                                         const std::string& period_start,
                                                         const std::string& period_end) {
    PublisherRevenueSummary summary;
    summary.period_start = period_start;
    summary.period_end = period_end;

    std::string error;
    auto subscription = ensure_publisher_subscription(publisher_id, error);
    auto plan = subscription ? get_billing_plan(subscription->plan_id) : std::nullopt;
    summary.share_percent = plan ? plan->revenue_share_percent : 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            SELECT COUNT(*), COALESCE(SUM(c.cpm_cents), 0)
            FROM ad_events e
            JOIN sites s ON s.id = e.site_id
            JOIN campaigns c ON c.id = e.campaign_id
            WHERE s.publisher_id = ? AND e.event_type = 'impression'
              AND c.source_type = 'direct' AND c.cpm_cents > 0
              AND e.occurred_at >= ? AND e.occurred_at < ?;
        )";
        if (prepare(db_, sql, &stmt)) {
            bind_text(stmt, 1, publisher_id);
            bind_text(stmt, 2, period_start);
            bind_text(stmt, 3, period_end);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                summary.direct_impressions = int64_col(stmt, 0);
                const long long cpm_sum_cents = int64_col(stmt, 1);
                // CPM is cents per 1,000 impressions.
                summary.gross_cents = cpm_sum_cents / 1000;
            }
            finalize(stmt);
        }
    }

    summary.publisher_cents =
        summary.gross_cents * std::max(0, std::min(100, summary.share_percent)) / 100;
    return summary;
}

bool Database::create_publisher_payout(PublisherPayout& payout, std::string& error) {
    if (payout.publisher_id.empty()) {
        error = "A publisher is required";
        return false;
    }
    if (payout.amount_cents <= 0) {
        error = "Payout amount must be greater than zero";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* check = nullptr;
    if (prepare(db_, "SELECT 1 FROM publishers WHERE id = ? LIMIT 1;", &check)) {
        bind_text(check, 1, payout.publisher_id);
        const bool found = sqlite3_step(check) == SQLITE_ROW;
        finalize(check);
        if (!found) {
            error = "Publisher not found";
            return false;
        }
    }

    const std::string now = utils::get_current_iso8601();
    if (payout.id.empty()) payout.id = utils::generate_uuid();
    if (payout.currency.empty()) payout.currency = "USD";
    if (payout.method.empty()) payout.method = "manual";
    if (payout.status.empty()) payout.status = "paid";
    if (payout.status == "paid" && payout.paid_at.empty()) payout.paid_at = now;
    payout.created_at = now;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO publisher_payouts
            (id, publisher_id, period_start, period_end, amount_cents, currency,
             method, reference, status, paid_at, recorded_by, notes, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (!prepare(db_, sql, &stmt)) {
        error = "Could not record the payout";
        return false;
    }
    bind_text(stmt, 1, payout.id);
    bind_text(stmt, 2, payout.publisher_id);
    bind_text(stmt, 3, payout.period_start);
    bind_text(stmt, 4, payout.period_end);
    sqlite3_bind_int64(stmt, 5, payout.amount_cents);
    bind_text(stmt, 6, payout.currency);
    bind_text(stmt, 7, payout.method);
    bind_text(stmt, 8, payout.reference);
    bind_text(stmt, 9, payout.status);
    if (payout.paid_at.empty()) sqlite3_bind_null(stmt, 10);
    else bind_text(stmt, 10, payout.paid_at);
    bind_text(stmt, 11, payout.recorded_by);
    bind_text(stmt, 12, payout.notes);
    bind_text(stmt, 13, payout.created_at);
    const int rc = sqlite3_step(stmt);
    finalize(stmt);
    if (rc != SQLITE_DONE) {
        error = "Could not record the payout";
        return false;
    }
    return true;
}

std::vector<PublisherPayout> Database::list_publisher_payouts(
    const std::optional<std::string>& publisher_id, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PublisherPayout> payouts;
    std::string sql = std::string(PAYOUT_SELECT) + "WHERE 1=1 ";
    if (publisher_id) sql += "AND publisher_id = ? ";
    sql += "ORDER BY created_at DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepare(db_, sql, &stmt)) return payouts;
    int index = 1;
    if (publisher_id) bind_text(stmt, index++, *publisher_id);
    sqlite3_bind_int(stmt, index++, clamp_limit(limit));
    sqlite3_bind_int(stmt, index++, std::max(0, offset));
    while (sqlite3_step(stmt) == SQLITE_ROW) payouts.push_back(payout_from_row(stmt));
    finalize(stmt);
    return payouts;
}

} // namespace adcontrol
