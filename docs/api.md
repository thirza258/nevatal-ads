# API reference

Base URL: **https://marketing.nevatal.id**

The same API is reachable locally at `http://127.0.0.1:${HTTP_PORT:-18080}` (via
nginx) or directly at the API container's `:8088` in development.

All request and response bodies are JSON unless noted. Timestamps are ISO-8601
UTC (`YYYY-MM-DDTHH:MM:SSZ`). Errors have the shape:

```json
{ "error": "Human-readable message" }
```

## Conventions

| Item | Value |
| --- | --- |
| Authentication | `Authorization: Bearer <token>` from `POST /api/v1/auth/login` or `POST /api/v3/auth/register` |
| Session lifetime | 30 days, server-side; logout or password change invalidates it |
| Operations roles | `admin`, `publisher`, `analyst` (the schema carries all three; only admin and publisher-owner accounts are provisioned in this release) |
| Learning roles | `learner`, `instructor`, `editor` (plus `admin` for learning administration) |
| Tenant scope | Publishers are pinned to their own `publisher_id`; administrators may filter by publisher |
| Learning scope | Instructors manage only their own courses; editors and admins manage all courses; learners see only their own enrollments and attempts |
| Admin data caching | `Cache-Control: no-store` through nginx |
| Public config caching | `Cache-Control: public, max-age=30, stale-while-revalidate=30` |
| SDK assets caching | `Cache-Control: public, max-age=300` |
| Admin body limit | 64 KiB; overall request limit 256 KiB |
| Event body limit | 8 KiB |
| Login rate limit | 10 attempts per IP per 5 minutes |
| Config rate limit | 240 requests per minute per IP + site key |
| Event rate limit | 600 requests per minute per IP + site key |
| Click/outbound rate limit | 240 requests per minute per IP |
| Learning registration | 8 attempts per IP per 5 minutes |
| Learning data export | 5 requests per user per 5 minutes |
| Learning enrollment / quiz | 30 per user per minute |
| Learning progress / authoring | 120 per user per minute |
| Learning course list | 300 per IP per minute |
| Rate-limit responses | `429` with `Retry-After` |

Rate-limit buckets are in process memory and reset when the API restarts.

## Authentication and roles

### `POST /api/v1/auth/login`

```json
{ "email": "you@example.com", "password": "…" }
```

`200` response:

```json
{
  "token": "64-hex-character opaque session token",
  "expires_at": "2026-11-25T12:00:00Z",
  "user": {
    "id": "…",
    "email": "you@example.com",
    "role": "publisher",
    "publisher_id": "pub_…",
    "created_at": "…",
    "updated_at": "…"
  }
}
```

`401` for bad credentials. Unknown accounts still perform password-hashing work
to reduce account enumeration. The response never contains `password_hash` or
`salt`. A platform administrator has an empty `publisher_id`.

- **Administrator**: full access to all publishers and inventory, affiliate-offer
  administration, verification fallback, read-only settings, and the audit log.
- **Publisher**: scoped to its own `publisher_id`. It can manage its owned sites,
  placements, offers, house/affiliate campaigns, and analytics. It cannot list
  other publishers, use the manual verification fallback, read the audit log, or
  create administrator-only direct campaigns.
- **Analyst**: read-only by API authorization (`writer` endpoints require
  `admin` or `publisher`). The schema recognizes the role, but no analyst
  provisioning endpoint or tenant-assignment workflow is exposed yet.
- **Learner / instructor / editor**: learning roles. They cannot use Phase 1/2
  operations endpoints; the operations helper rejects them. `admin` can use both
  planes. Public self-registration always creates a `learner`; only an
  administrator can create instructors or editors through
  `POST /api/v3/admin/learning/users`.

### `POST /api/v1/auth/logout`

Deletes the bearer token if present. Returns `{"status":"logged_out"}`. Safe to
call with an expired token.

### `GET /api/v1/auth/me`

Returns `{"user": {…}}` for the current token; `401` otherwise.

### `POST /api/v1/auth/change-password`

```json
{ "current_password": "…", "new_password": "at-least-12-characters" }
```

New passwords must be 12–128 characters and differ from the current password.
Success invalidates **all** sessions, including the caller's, and audits
`PASSWORD_CHANGED`:

```json
{ "status": "password_changed", "message": "All sessions were invalidated. Sign in again." }
```

## Settings and sandbox

### `GET /api/v1/settings`

Read-only operational values, never `SECRET_KEY`:

```json
{
  "app_name": "AdControl",
  "environment": "production",
  "timezone": "UTC",
  "sdk_version": "1.3.0",
  "sdk_url": "/sdk/v1/adcontrol.js",
  "config_cache_seconds": 30,
  "max_stale_seconds": 300,
  "event_retention_days": 180,
  "click_token_ttl_seconds": 86400,
  "delivery_rate_limit_per_minute": 240,
  "event_rate_limit_per_minute": 600
}
```

### Email marketing SMTP (`/api/v1/email/...`)

Administrator only. The SMTP password is write-only: it is encrypted with
`SECRET_KEY` at rest, omitted from every response (only `password_set` is
returned), and never written to the audit log.

`GET /api/v1/email/settings` returns the current configuration, or defaults when
nothing has been saved:

```json
{
  "settings": {
    "enabled": false,
    "host": "",
    "port": 587,
    "encryption": "starttls",
    "username": "",
    "password_set": false,
    "from_email": "",
    "from_name": "",
    "reply_to": "",
    "updated_at": "",
    "updated_by": ""
  }
}
```

`PUT /api/v1/email/settings` accepts `enabled`, `host`, `port`, `encryption`
(`starttls`, `ssl`, or `none`), `username`, `from_email`, `from_name`,
`reply_to`, and an optional `password`. Omit `password` to keep the stored value,
send a string to replace it, or send `""`/`null` to clear it. Enabling email
requires a valid host, sender address, and both username and password (or
neither, for an unauthenticated relay). Changes are audited as
`UPDATE_EMAIL_SETTINGS` without secret material.

`POST /api/v1/email/test` with `{ "to": "you@example.com" }` connects to the
saved server (STARTTLS and SSL use certificate-verified TLS), submits one HTML
test message, and returns `{ "status": "sent", "message": "…" }`. Failures return
`502` with the SMTP error. Test sends are limited to five per administrator per
minute and audited as `SEND_TEST_EMAIL` with only the recipient domain.

### `GET /api/v1/sandbox/sites/{site_id}`

Authenticated preview data for the dashboard sandbox. Bypasses the origin
allowlist (there is no host page) but requires a session and returns only
currently selected campaigns with signed click URLs. Affiliate campaigns carry
a `/r/` click URL, their disclosure, and merchant/price metadata:

```json
{
  "site": { "…": "site object" },
  "allowed_domains": ["example.com"],
  "placements": [
    {
      "placement": { "…": "placement object" },
      "campaign": { "…": "campaign object" },
      "click_url": "/r/…"
    }
  ]
}
```

`campaign` and `click_url` are `null` when the placement has no eligible ad. The
sandbox enforces tenant access and applies the placement, campaign, and schedule
selection rules plus a basic destination/offer availability check, but
deliberately skips the public origin allowlist and the site
verification/monetization/publisher gates so an owner can preview a dark site.
It is not a public delivery path, and its signed links still fail at redirect
time if the site is not deliverable.

## Publishers

Publisher object:

```json
{
  "id": "pub_…",
  "name": "Example Media",
  "owner_user_id": "usr_…",
  "owner_email": "owner@example.com",
  "status": "active",
  "contact_email": "owner@example.com",
  "timezone": "Asia/Jakarta",
  "created_at": "…",
  "updated_at": "…"
}
```

| Field | Rules |
| --- | --- |
| `name` | Required, ≤120 characters |
| `owner_email` | Valid unique email for the publisher-owner login |
| `password` | Required on create; 12–128 characters |
| `status` | `pending`, `active`, `suspended`, or `rejected`; defaults to `pending` |
| `contact_email` | Valid email; defaults to `owner_email` |
| `timezone` | IANA-style name, ≤64 characters; defaults to `UTC` |

Publisher provisioning is administrator-only and invite-only. It creates a
publisher-owner login with role `publisher` and a linked `publisher_id`. The
response never contains the password, hash, or salt.

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/publishers` | Admin | List all publishers (`{"publishers":[...]}`) |
| `POST` | `/api/v1/publishers` | Admin | Provision a publisher and owner login (`201`) |
| `GET` | `/api/v1/publishers/profile` | Any linked user | Current publisher and `sites_count` |
| `GET` | `/api/v1/publishers/{id}` | Admin or owner | Publisher plus its sites |
| `PATCH` | `/api/v1/publishers/{id}` | Admin or owner | Update details |

On `PATCH`, publishers may update only contact details (`contact_email`,
`timezone`), not `name`, `status`, or ownership. Administrators may update all
fields. Audit actions: `CREATE_PUBLISHER`, `UPDATE_PUBLISHER`.

## Websites

Site object:

```json
{
  "id": "site_…",
  "workspace_id": "…",
  "publisher_id": "pub_…",
  "name": "Example Publisher",
  "primary_domain": "example.com",
  "allowed_domains": ["example.com", "*.example.org", "localhost:3000"],
  "public_site_key": "site_pub_…",
  "status": "active",
  "config_version": 1,
  "verification_status": "verified",
  "verified_at": "2026-10-20T10:00:00Z",
  "monetization_status": "enabled",
  "default_disclosure_text": "Affiliate disclosure: this site may earn a commission.",
  "allowed_ad_sources": ["house", "direct", "affiliate"],
  "created_at": "…",
  "updated_at": "…"
}
```

| Field | Rules |
| --- | --- |
| `name` | Required, ≤120 characters |
| `primary_domain` | Required hostname, optionally with a port; no wildcard |
| `allowed_domains` | Hostnames, optional ports, `*.example.com`, or `*`; maximum 50; normalized to lowercase without scheme/path; defaults to the primary domain |
| `status` | `active` or `disabled` |
| `publisher_id` | Admin may set it; publisher-created sites always inherit the caller's tenant |
| `verification_status` | `unverified`, `verified`, or `rejected`; set by verification only |
| `monetization_status` | `pending`, `enabled`, or `disabled`; publisher sites start pending and become enabled on verification |
| `default_disclosure_text` | ≤500 characters; used when a campaign/offer has no disclosure |
| `allowed_ad_sources` | Non-empty subset of `house`, `direct`, `affiliate` |

The public site key is safe to expose. The verification token is deliberately
**not** part of the site object; it is returned only by the authenticated
verification endpoint. Publisher-owned sites start unverified at the database
level. Legacy administrator-created sites are verified administratively.

### Endpoints

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/sites?publisher_id=` | Authenticated | List accessible sites; admin may filter by publisher |
| `POST` | `/api/v1/sites` | Admin or publisher | Create (`201`); required `name`, `primary_domain` |
| `GET` | `/api/v1/sites/{id}` | Owner/admin | Get one |
| `PATCH` | `/api/v1/sites/{id}` | Owner/admin | Update name, domain, allowlists, status, disclosure, and sources; admins may also set `monetization_status` |
| `GET` | `/api/v1/sites/{id}/verification` | Owner/admin | Read DNS TXT challenge |
| `POST` | `/api/v1/sites/{id}/verify` | Owner (DNS) / admin (manual) | Verify ownership |
| `POST` | `/api/v1/sites/{id}/rotate-key` | Owner/admin | Generate a new public site key |
| `POST` | `/api/v1/sites/{id}/pause-all` | Owner/admin | Pause every live/scheduled campaign for the site |
| `DELETE` | `/api/v1/sites/{id}` | Owner/admin | Delete only when no analytics history |

Only administrators may set `publisher_id` when creating a site or change
`monetization_status` when updating one. `verification_status`, `verified_at`,
and the verification token are never writable through `PATCH`; they change only
through the verification endpoints. A primary-domain change on a publisher-owned
site resets verification and monetization; the new domain must be proven again.

### Verification

`GET /api/v1/sites/{id}/verification` returns:

```json
{
  "site_id": "site_…",
  "domain": "example.com",
  "verification_status": "unverified",
  "verified_at": "",
  "monetization_status": "pending",
  "methods": ["dns_txt"],
  "method": "dns_txt",
  "record_type": "TXT",
  "record_name": "_adcontrol-challenge.example.com",
  "record_value": "adcontrol-site-verification=<random-token>",
  "instructions": [
    "Create the TXT record at your DNS provider.",
    "Keep the value exactly as shown; DNS changes can take a few minutes to propagate.",
    "Return here and select Verify with DNS TXT."
  ],
  "manual_verification_available": true
}
```

`manual_verification_available` is present only for administrators. Once
verified, the outstanding token is removed and the challenge fields are omitted.
HTML-file verification is intentionally not implemented, so verification does
not create a server-side HTTP-fetch/SSRF surface.

`POST /api/v1/sites/{id}/verify`:

```json
{ "method": "dns_txt" }
```

The server resolves `_adcontrol-challenge.<primary-domain>` and compares the TXT
value exactly. On success:

```json
{
  "status": "verified",
  "method": "dns_txt",
  "site": { "…": "site object" }
}
```

An already verified site returns `{"status":"already_verified","site":{…}}`.
Administrators may pass `{"method":"manual"}` as an out-of-band fallback; a
publisher attempting this receives `403`. Invalid method/token values return
`400`. Audit action: `VERIFY_SITE`.

### Delivery eligibility

A publisher-owned site is dark (`404` from delivery) unless all of the following
are true:

- site `status` is `active`;
- `verification_status` is `verified`;
- `monetization_status` is `enabled`;
- the owning publisher exists and has status `active`.

Disabling a site or key rotation also clears cached delivery for connected pages
on their next refresh. Audit actions: `CREATE_SITE`, `UPDATE_SITE`, `ROTATE_KEY`,
`PAUSE_ALL_CAMPAIGNS`, `VERIFY_SITE`, `DELETE_SITE`.

## Placements

Placement object:

```json
{
  "id": "plc_…",
  "site_id": "site_…",
  "placement_key": "header-banner",
  "name": "Header Banner",
  "description": "",
  "format": "responsive_banner",
  "width": 728,
  "height": 90,
  "fallback_behavior": "blank",
  "allowed_source_types": ["house", "direct", "affiliate"],
  "max_items": 1,
  "layout_config": {},
  "disclosure_mode": "inherited",
  "status": "active",
  "created_at": "…",
  "updated_at": "…"
}
```

| Field | Rules |
| --- | --- |
| `placement_key` | Required on create; `^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$`; unique per site |
| `name` | Required, ≤120 characters |
| `description` | ≤500 characters |
| `format` | `responsive_banner`, `image`, `text_link`, `native_card`, or `product_card`; defaults to `responsive_banner` |
| `width`, `height` | 0–4096 |
| `fallback_behavior` | `hide` (default), `blank`, or `custom` (reserved; SDK treats as hide) |
| `allowed_source_types` | Non-empty subset of `house`, `direct`, `affiliate` |
| `max_items` | 1–20; the current selection engine delivers one campaign per placement |
| `layout_config` | JSON object, ≤4096 serialized characters |
| `disclosure_mode` | `required`, `inherited`, or `disabled_only_if_not_applicable`; affiliate disclosure is mandatory regardless |
| `status` | `active` or `disabled` |

### Endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/placements?site_id=` | List accessible placements, optionally filtered |
| `POST` | `/api/v1/placements` | Create (`201`) |
| `PATCH` | `/api/v1/placements/{id}` | Update name, description, format, dimensions, fallback, sources, max items, layout config, disclosure mode, and status; key and site are immutable |
| `DELETE` | `/api/v1/placements/{id}` | Delete, or archive/disable when analytics history exists |

`DELETE` returns `{"status":"archived"}` with a message when event rows exist.
Audit actions: `CREATE_PLACEMENT`, `UPDATE_PLACEMENT`, `DELETE_PLACEMENT`,
`ARCHIVE_PLACEMENT`.

## Campaigns

Campaign object:

```json
{
  "id": "camp_…",
  "site_id": "site_…",
  "placement_id": "plc_…",
  "name": "Autumn promotion",
  "destination_url": "https://example.com/landing",
  "format": "responsive_banner",
  "title": "Autumn sale",
  "body": "20% off this week.",
  "image_url": "https://img.example.com/autumn.png",
  "alt_text": "Autumn sale artwork",
  "cta_label": "Shop now",
  "status": "active",
  "source_type": "house",
  "affiliate_offer_id": "",
  "disclosure_override": "",
  "starts_at": "2026-10-01T00:00:00Z",
  "ends_at": "2026-10-31T23:59:59Z",
  "priority": 10,
  "weight": 100,
  "created_at": "…",
  "updated_at": "…",
  "published_at": "…"
}
```

| Field | Rules |
| --- | --- |
| `site_id`, `placement_id` | Required; placement must belong to the site |
| `name` | Required, ≤120 characters |
| `destination_url` | For house/direct, a safe `http(s)` URL ≤2048 chars; no userinfo, whitespace, control characters, backslashes, or unsafe schemes. For affiliate, this is derived from the offer and validated as an affiliate URL. |
| `image_url` | Empty or a safe URL; required for `product_card` (the place-offer flow copies it from `product_image_url`) |
| `format` | One of the five formats; defaults to the placement's format |
| `title` | ≤200 characters; required for `product_card` |
| `body` | ≤1000 characters |
| `alt_text` | ≤300 characters |
| `cta_label` | ≤60 characters |
| `status` | `draft`, `scheduled`, `active`, `paused`, `expired`, `archived` |
| `source_type` | `house`, `direct`, `affiliate`, or `external_network`; external-network delivery is disabled pending an approved provider |
| `affiliate_offer_id` | Required when `source_type` is `affiliate`; the offer must belong to the same publisher as the site |
| `disclosure_override` | ≤500 characters; overrides the offer/site disclosure |
| `starts_at`, `ends_at` | Empty or ISO-8601; `ends_at` must be after `starts_at` |
| `priority` | 0–1000; stored default is 1. Higher wins. |
| `weight` | 1–10000; stored default is 100. Deterministic tie-breaker only. |

Creating a campaign defaults `status` to `draft` and `source_type` to `house`.
Creating or updating with `active`/`scheduled` immediately normalizes the status
from the schedule. Publishing sets `active` when inside the window, `scheduled`
when it starts later, and `expired` when it has already ended.

**Source rules**

- The campaign's source must be permitted by both the site's
  `allowed_ad_sources` and the placement's `allowed_source_types`.
- Publishers may create only `house` and `affiliate` campaigns. Administrators
  may also create `direct` campaigns.
- `external_network` is rejected until a provider integration is approved.
- An affiliate campaign must use `product_card`. Its destination is always
  copied server-side from the stored offer; a request cannot substitute a
  destination. The offer must be active before the campaign can publish, and
  its publisher must match the site owner.
- An affiliate campaign cannot publish unless the site is active, verified, and
  monetization-enabled.

### Endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/campaigns?site_id=&placement_id=&status=` | List with optional filters and tenant scoping |
| `POST` | `/api/v1/campaigns` | Create (`201`) |
| `GET` | `/api/v1/campaigns/{id}` | Get one |
| `PATCH` | `/api/v1/campaigns/{id}` | Update; `site_id` is immutable |
| `POST` | `/api/v1/campaigns/{id}/publish` | Publish/resume under the current schedule |
| `POST` | `/api/v1/campaigns/{id}/pause` | Pause immediately |
| `DELETE` | `/api/v1/campaigns/{id}` | Delete, or archive when analytics history exists |

Audit actions: `CREATE_CAMPAIGN`, `UPDATE_CAMPAIGN`, `PUBLISH_CAMPAIGN`,
`PAUSE_CAMPAIGN`, `DELETE_CAMPAIGN`, `ARCHIVE_CAMPAIGN`.

## Affiliate offers

Offers are manually entered destinations for a single publisher. AdControl does
not call Shopee or generate affiliate links. The stored URL is treated as
opaque, byte-exact data: no query parameter is injected, removed, or reordered.

Affiliate offer object:

```json
{
  "id": "off_…",
  "publisher_id": "pub_…",
  "provider": "shopee_affiliate",
  "offer_name": "Shopee coffee grinder",
  "product_title": "Coffee grinder",
  "product_image_url": "https://cdn.example.com/product.jpg",
  "product_description": "Manual grinder with ceramic burrs.",
  "merchant_name": "Example Shop",
  "affiliate_url": "https://shope.ee/abc?smtt=0.123&affiliate_id=XYZ",
  "canonical_product_url": "https://shopee.example.com/product/123",
  "price_text": "IDR 199.000",
  "currency": "IDR",
  "availability_checked_at": "",
  "disclosure_text": "Affiliate disclosure: we may earn a commission.",
  "status": "active",
  "validation_status": "valid",
  "validation_reason": "Structurally valid HTTPS URL; destination was not fetched.",
  "last_link_validation_at": "2026-10-26T12:00:00Z",
  "created_at": "…",
  "updated_at": "…"
}
```

| Field | Rules |
| --- | --- |
| `publisher_id` | Required for admin-created offers; forced to the caller's tenant for publishers |
| `provider` | `shopee_affiliate` or `manual_affiliate` |
| `offer_name` | Required, ≤200 characters |
| `product_title` | Required, ≤300 characters |
| `product_image_url` | Empty or a safe `http(s)` URL; an offer needs one to be placed as a `product_card` campaign |
| `product_description` | ≤2000 characters |
| `merchant_name` | ≤120 characters |
| `affiliate_url` | Required, byte-exact, absolute HTTPS URL, public hostname, no userinfo/whitespace/control characters, ≤2048 characters |
| `canonical_product_url` | Empty or an affiliate-rule HTTPS URL |
| `price_text` | ≤50 characters; free-form display text (normally includes the currency); rendered verbatim |
| `currency` | Optional three-letter uppercase code |
| `availability_checked_at` | Empty or ISO-8601 |
| `disclosure_text` | ≤500 characters |
| `status` | `draft`, `active`, `paused`, `expired`, `rejected`, `archived`; create/edit accepts only draft/active/paused |
| `validation_status` | `unverified` (new), `valid` (structural check passed), `rejected` |

### Endpoints

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/affiliate-offers?publisher_id=&status=` | Admin or publisher | List accessible offers |
| `POST` | `/api/v1/affiliate-offers` | Admin or publisher | Create (`201`) |
| `GET` | `/api/v1/affiliate-offers/{id}` | Owner/admin | Offer plus its campaigns |
| `PATCH` | `/api/v1/affiliate-offers/{id}` | Owner/admin | Update; changing the URL resets validation |
| `POST` | `/api/v1/affiliate-offers/{id}/validate` | Owner/admin | Structural validation only |
| `POST` | `/api/v1/affiliate-offers/{id}/publish` | Owner/admin | Activate (`active`) |
| `POST` | `/api/v1/affiliate-offers/{id}/pause` | Owner/admin | Pause (`paused`) |
| `POST` | `/api/v1/affiliate-offers/{id}/placements` | Owner/admin | Create a product-card campaign (`201`) |
| `DELETE` | `/api/v1/affiliate-offers/{id}` | Owner/admin | Delete only when unused; `409` otherwise |

Validation returns the updated offer object, including:

```json
{
  "validation_status": "valid",
  "validation_reason": "Structurally valid HTTPS URL; destination was not fetched."
}
```

This is deliberately **not** a link checker: no outbound HTTP request is made,
so no offer URL can be used as an SSRF primitive. A valid result proves only
the URL's structure, not that the product exists, the price is current, or a
commission will be paid. A rejected offer cannot be activated.

`POST /api/v1/affiliate-offers/{id}/placements` creates a campaign in one step.
The request supplies no destination or creative:

```json
{
  "placement_id": "plc_…",
  "name": "Product card campaign",
  "status": "draft",
  "starts_at": "",
  "ends_at": "",
  "priority": 1,
  "weight": 100
}
```

The server forces `source_type: "affiliate"`, `format: "product_card"`,
`affiliate_offer_id`, and `destination_url` from the stored offer. The placement
must belong to an accessible site owned by the same publisher. The response is
the created campaign object. Publish the campaign separately.

Offers can be reused across the same publisher's owned sites. Campaigns that
still reference an offer block deletion with `409`; pause or delete those
campaigns first.

Audit actions: `CREATE_AFFILIATE_OFFER`, `UPDATE_AFFILIATE_OFFER`,
`VALIDATE_AFFILIATE_OFFER`, `PUBLISH_AFFILIATE_OFFER`,
`PAUSE_AFFILIATE_OFFER`, `PLACE_AFFILIATE_OFFER`,
`DELETE_AFFILIATE_OFFER`.

## Public delivery

These endpoints are used by the SDK. They take a **public site key**, never an
admin token. They send `Access-Control-Allow-Origin: *`; authorization comes from
the site key plus the origin allowlist, and state changes require the admin API.
The SDK's user-agent is not trusted; it only receives data the API selected.

### `GET /delivery/v1/sites/{public_site_key}/config`

Request headers optionally include `Origin` and `If-None-Match`. Response for a
verified, monetization-enabled publisher site:

```json
{
  "site_key": "site_pub_…",
  "site_name": "Example Publisher",
  "config_version": 7,
  "refresh_interval_seconds": 30,
  "max_stale_seconds": 300,
  "placements": {
    "product-feed": {
      "campaign_id": "camp_…",
      "placement_id": "plc_…",
      "placement_key": "product-feed",
      "format": "product_card",
      "placement_format": "product_card",
      "width": 0,
      "height": 0,
      "fallback_behavior": "hide",
      "title": "Coffee grinder",
      "body": "Manual grinder with ceramic burrs.",
      "image_url": "https://cdn.example.com/product.jpg",
      "alt_text": "Coffee grinder",
      "cta_label": "View offer",
      "source_type": "affiliate",
      "affiliate": true,
      "disclosure_text": "Affiliate disclosure: we may earn a commission.",
      "merchant_name": "Example Shop",
      "price_text": "IDR 199.000",
      "currency": "IDR",
      "click_url": "/r/…"
    },
    "in-article": {
      "campaign_id": "",
      "placement_id": "plc_…",
      "placement_key": "in-article",
      "format": "native_card",
      "placement_format": "native_card",
      "width": 0,
      "height": 0,
      "fallback_behavior": "hide",
      "title": "",
      "body": "",
      "image_url": "",
      "alt_text": "",
      "cta_label": "",
      "source_type": "",
      "affiliate": false,
      "disclosure_text": "",
      "merchant_name": "",
      "price_text": "",
      "currency": ""
    }
  }
}
```

- Destination URLs are **not** included. The browser only receives a signed
  click URL; the API resolves the destination server-side.
- Every **active** placement is present, even with no eligible campaign.
  `campaign_id: ""` means "no ad"; `click_url` is absent. The SDK applies the
  placement's fallback.
- Only active, verified, monetization-enabled sites with an active publisher are
  served. Unverified publisher sites return `404` and are `no-store` so a bad
  result is not cached.
- Affiliate entries always include non-empty `disclosure_text` and a `/r/`
  click URL. Non-affiliate entries use `/click/`.
- Product-card rendering uses `title`, `body`, `image_url`, `alt_text`,
  `cta_label`, `merchant_name`, and `price_text`. `price_text` is rendered
  verbatim as the operator entered it.
- `ETag` is weak and derived from the stable config, an hourly issuance window,
  and a random per-process nonce. Send it back as `If-None-Match`; a match
  returns `304` with no body. Restarting the API changes the ETag so clients
  fetch fresh, valid click links.
- `Cache-Control: public, max-age=30, stale-while-revalidate=30` on a `200`;
  a `304` keeps the shorter `public, max-age=30`.
- Rate limit: 240/min per IP + site key.
- `403` when `Origin`/`Referer` is outside the allowlist; `404` when the site is
  missing, disabled, unverified, or not monetization-enabled. Both are `no-store`.

Requests without `Origin`/`Referer` (server-side checks, curl) are allowed if the
site key is valid. Browser requests always carry one of these headers.

### `POST /delivery/v1/events`

Records an **impression only**. Requests should use
`Content-Type: text/plain;charset=UTF-8` so `navigator.sendBeacon` can send them
without a CORS preflight; the body is parsed as JSON.

```json
{
  "site_key": "site_pub_…",
  "event_type": "impression",
  "event_id": "imp-0123456789abcdef",
  "placement_key": "header-banner",
  "campaign_id": "camp_…",
  "page_origin": "https://www.example.com",
  "referrer": "https://search.example/"
}
```

| Field | Rules |
| --- | --- |
| `site_key` | Required, active and eligible site |
| `event_type` | Required, must be `impression` |
| `event_id` | Required, `^[A-Za-z0-9._:-]{8,128}$`, globally unique dedup key |
| `placement_key` | Required, must exist for the site |
| `campaign_id` | Required, must belong to the site and placement |
| `page_origin` | Checked only when no `Origin`/`Referer` header is present |
| `referrer` | Optional; minimized to a query/fragment-free URL ≤300 chars |

Response:

```json
{ "status": "recorded", "event_id": "imp-0123456789abcdef" }
```

`status` is `deduplicated` when the `event_id` already exists. `400` is returned
for any click-type event; clicks are recorded only by the signed redirect.
`403` for a disallowed origin, `404` for an ineligible site, `413` for bodies over
8 KiB, `429` when rate-limited.

The API stores the event time, site/placement/campaign ids, source type,
affiliate-offer id when applicable, origin, minimized referrer, a coarse UA
category (`bot`/`mobile`/`desktop`), and a truncated HMAC-SHA256 of the client IP.
It does not store the raw IP or set cookies.

### `GET /click/{token}` and `GET /r/{token}`

Both routes verify the same HMAC-signed, 24-hour token, revalidate delivery
state and origin, record the click, and return `302 Location: <server-selected
destination>` with `Cache-Control: no-store` and
`Referrer-Policy: no-referrer`.

- `/click/` is the Phase 1 house/direct click path.
- `/r/` is the Phase 2 first-party affiliate outbound path. It additionally
  records an `outbound_redirect` event alongside the click, and is used only for
  campaigns with a valid active affiliate offer. The redirect target is the
  stored affiliate URL, byte-for-byte, including its query string and parameter
  order.

| Status | Meaning |
| --- | --- |
| `302` | Click recorded; redirect follows |
| `400` | Malformed/forged token |
| `403` | Click origin not allowed |
| `410` | Token expired, or campaign/site/placement/offer no longer deliverable |
| `429` | Rate limited |

There is no query-string destination and no user-controlled redirect target.
`?next=https://evil.example` is ignored; the server still resolves the configured
destination. Invalid `/r/` tokens return a plain-text `400`, not a redirect.

## Analytics

All analytics endpoints require a bearer token, accept optional `site_id`,
`placement_id`, and `campaign_id` filters, and return `no-store`. Administrators
may also pass `publisher_id`; non-admin accounts are always pinned to their own
publisher and receive `403` if they have no publisher link. Requesting a site,
placement, or campaign outside the caller's tenant returns `404`.

`start_date`/`end_date` accept `YYYY-MM-DD` (inclusive days) or an ISO-8601
timestamp. `ctr` is a percentage (0–100), computed as `clicks / impressions *
100`, or `0` when there are no impressions.

### `GET /api/v1/analytics/summary`

```json
{
  "impressions": 1234,
  "clicks": 56,
  "outbound_redirects": 12,
  "affiliate_clicks": 12,
  "ctr": 4.54,
  "confirmed_conversions": 0,
  "confirmed_commission": 0,
  "provenance": {
    "impressions": "adcontrol_observed",
    "clicks": "adcontrol_observed",
    "outbound_redirects": "adcontrol_observed",
    "conversions": "not_connected",
    "commission": "not_connected"
  }
}
```

`impressions`, `clicks`, `outbound_redirects`, and `affiliate_clicks` are
AdControl-observed counts. `clicks` counts signed click redirects; an affiliate
click also produces one `outbound_redirect`. `confirmed_conversions` and
`confirmed_commission` are always zero because no external conversion source is
connected. They must never be inferred from redirect events.

### `GET /api/v1/analytics/timeseries?days=7`

`days` is 1–365 (default 7) and is used only when no explicit dates are given.

```json
[
  {
    "date": "2026-10-24",
    "impressions": 100,
    "clicks": 4,
    "outbound_redirects": 1,
    "ctr": 4.0
  }
]
```

### `GET /api/v1/analytics/breakdown?dimension=campaign`

`dimension` is `site`, `placement`, `campaign`, or `source` (default
`campaign`). At most 200 rows, ordered by impressions then clicks:

```json
{
  "rows": [
    {
      "key": "affiliate",
      "name": "affiliate",
      "impressions": 100,
      "clicks": 4,
      "outbound_redirects": 4,
      "ctr": 4.0
    }
  ]
}
```

The `source` dimension uses the event's stored `source_type` (`house`, `direct`,
`affiliate`, or `legacy` for pre-migration rows).

### `GET /api/v1/analytics/export.csv`

Same filters and date range as summary. Returns
`text/csv; charset=utf-8` with
`Content-Disposition: attachment; filename="adcontrol_events.csv"`.
Columns:

```
occurred_at,event_type,event_id,site_id,placement_id,campaign_id,source_type,affiliate_offer_id,page_origin,referrer,user_agent_category
```

The export can be large; it is generated in memory and is not paginated. Tokens
are accepted only in the `Authorization` header, not in the query string. Every
query enforces the caller's tenant scope.

## Audit log

### `GET /api/v1/audit-log?limit=50&offset=0`

Administrator-only. `limit` is 1–200 (default 50), `offset` ≥ 0, newest first.

```json
{
  "audit_logs": [
    {
      "id": "…",
      "actor": "you@example.com",
      "action": "PAUSE_CAMPAIGN",
      "entity_type": "campaign",
      "entity_id": "camp_…",
      "details": {},
      "created_at": "2026-10-26T12:00:00Z"
    }
  ]
}
```

Actions include `LOGIN`, `PASSWORD_CHANGED`, `CREATE_PUBLISHER`,
`UPDATE_PUBLISHER`, site/placement/campaign create/update/delete/archive/publish/
pause, `ROTATE_KEY`, `PAUSE_ALL_CAMPAIGNS`, `VERIFY_SITE`, affiliate-offer
create/update/validate/publish/pause/place/delete, and partner-inquiry receipt
and status changes. `details` is a JSON value, not a string.

## Partner inquiries

The public pages `/become-a-publisher` and `/become-a-client` submit applications
to these endpoints. A submission is stored as a lead for administrator review; it
never creates a user, publisher, site, campaign, or session.

### `POST /api/v3/partners/inquiries`

Public. Rate limit: 20 per origin bucket per 10 minutes. Request body is capped at 16 KiB.

```json
{
  "kind": "publisher",
  "name": "Ada Publisher",
  "email": "ada@example.com",
  "company": "Ada's Blog",
  "website": "https://ada.example.com",
  "message": "I would like to place one article unit on my site.",
  "fax": ""
}
```

- `kind` is `publisher` or `client`.
- `name` is 2–120 characters; `email` must be a valid address.
- `company` is optional (≤160 characters); `website` is optional for a client
  and required for a publisher, and must be an `http`/`https` URL.
- `message` is 20–4000 characters.
- `fax` is a hidden anti-bot field. If it is non-empty, the request receives the
  normal success response but nothing is stored.

`201` returns `{"status": "received"}`. `400` reports a validation error, `413`
an oversized body, and `429` a rate limit (`Retry-After: 600`). No account is
created and no session token is returned.

### `GET /api/v1/partner-inquiries`

Administrator-only. Optional filters: `kind` (`publisher`/`client`), `status`
(`new`/`reviewing`/`closed`), `limit` (1–200, default 100), and `offset` (≥0).
Newest first.

```json
{
  "inquiries": [
    {
      "id": "…",
      "kind": "publisher",
      "name": "Ada Publisher",
      "email": "ada@example.com",
      "company": "Ada's Blog",
      "website": "https://ada.example.com",
      "message": "I would like to place one article unit on my site.",
      "status": "new",
      "created_at": "2026-10-26T12:00:00Z",
      "updated_at": "2026-10-26T12:00:00Z"
    }
  ]
}
```

### `PATCH /api/v1/partner-inquiries/{id}`

Administrator-only. Body: `{"status": "reviewing"}` where status is `new`,
`reviewing`, or `closed`. Returns `{"inquiry": {…}}`. A missing inquiry is `404`.
The change writes a `PARTNER_INQUIRY_STATUS_UPDATED` audit row; receipt writes
`PARTNER_INQUIRY_RECEIVED`.

## Learning API

Phase 3 routes live under `/api/v3`, plus public `/sitemap.xml` and `/robots.txt`.
They are additive to Phase 1/2. All admin/learner JSON endpoints set
`Cache-Control: no-store`; public catalog endpoints are cacheable for 2 minutes.
Unauthenticated public catalog reads are rate-limited on the course-list route.
See [learning.md](learning.md) for the end-to-end behavior and role model.

### SEO endpoints

#### `GET /sitemap.xml`

Returns `application/xml; charset=utf-8`, `Cache-Control: public, max-age=1800`.
The origin is `PUBLIC_BASE_URL`, which must be an HTTPS origin with no path or
trailing slash. An invalid or `http://` value falls back to
`https://marketing.nevatal.id`.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url><loc>https://marketing.nevatal.id/</loc><priority>1.0</priority></url>
  <url><loc>https://marketing.nevatal.id/courses</loc><priority>0.8</priority></url>
  <url><loc>https://marketing.nevatal.id/topics</loc><priority>0.7</priority></url>
  <url><loc>https://marketing.nevatal.id/learning-paths</loc><priority>0.7</priority></url>
  <!-- published topics, courses, and paths only -->
</urlset>
```

A course is included only when both the course and its topic are published.
Drafts, archived courses, authoring/account routes, and the operations dashboard
are excluded. Course and path `<lastmod>` values use the row's `updated_at`.

#### `GET /robots.txt`

Returns `text/plain; charset=utf-8`, `Cache-Control: public, max-age=1800`:

```text
User-agent: *
Allow: /
Allow: /courses
Allow: /topics
Allow: /learning-paths
Disallow: /api/
Disallow: /author
Disallow: /my-learning
Disallow: /dashboard

Sitemap: https://marketing.nevatal.id/sitemap.xml
```

The disallow rules are search-result hygiene for private app shells. They do not
authorize requests; every private API still requires a valid session and role.

### Accounts and privacy

#### `POST /api/v3/auth/register`

Public. Rate limit: 8 attempts per IP per 5 minutes.

```json
{ "email": "learner@example.com", "password": "at-least-12-characters" }
```

The email is normalized to lowercase. Passwords are 12–128 characters. The role
is always `learner`; a `role` property in the body is ignored. `201` returns the
same session shape as login:

```json
{
  "token": "64-hex-character opaque session token",
  "expires_at": "2026-11-25T12:00:00Z",
  "user": {
    "id": "…",
    "email": "learner@example.com",
    "role": "learner",
    "created_at": "…",
    "updated_at": "…"
  }
}
```

`400` for invalid fields, `409` for an existing email, `429` when rate-limited.
No password hash, salt, or automatic enrollment is returned.

#### `GET /api/v3/learning/me/export`

Authenticated learning account. Rate limit: 5 per user per 5 minutes. Returns a
JSON attachment `adcontrol-learning-export.json`:

```json
{
  "exported_at": "2026-10-26T12:00:00Z",
  "user": { "id": "…", "email": "…", "role": "learner" },
  "enrollments": [
    {
      "id": "…",
      "course_id": "…",
      "status": "active",
      "progress_percent": 50,
      "completed_lesson_ids": ["…"]
    }
  ],
  "quiz_attempts": [
    {
      "id": "…",
      "lesson_id": "…",
      "score": 100,
      "passed": true,
      "attempted_at": "…"
    }
  ],
  "note": "This export contains your learning account data. It does not include other learners' data."
}
```

#### `DELETE /api/v3/learning/me`

Authenticated learning account. Only a `learner` may self-delete; an instructor,
editor, or administrator receives `403`. On success, all sessions for the account
are removed, the account is deleted with its enrollments/progress/attempts
(cascade), and the response is `204`. The token is unusable immediately (`401`).

### Public catalog

All catalog resources below are public and require no token.

| Method | Path | Query | Content |
| --- | --- | --- | --- |
| `GET` | `/api/v3/catalog/courses` | `topic` (slug), `q` (≤100 chars), `limit` 1–100, `offset` ≥0 | `{"courses": [...]}` |
| `GET` | `/api/v3/catalog/courses/{slug}` | — | `{"course", "curriculum", "has_preview", "enrollment_required"}` |
| `GET` | `/api/v3/catalog/topics` | — | `{"topics": [...]}`, each with `course_count` |
| `GET` | `/api/v3/catalog/topics/{slug}` | — | `{"topic", "courses"}` |
| `GET` | `/api/v3/catalog/learning-paths` | — | `{"learning_paths": [...]}`, each with public `courses` |
| `GET` | `/api/v3/catalog/learning-paths/{slug}` | — | `{"learning_path", "courses"}` |

Public course objects contain `id`, `slug`, topic fields, `title`, `summary`,
`description_md`, `level`, `duration_minutes`, `outcomes`, `version`,
`published_at`, `last_reviewed_at`, `next_review_due_at`, and `updated_at`. They do
not contain author or reviewer identity. A course is visible only when it and its
topic are `published`.

For anonymous course detail:

- Curriculum modules and lessons are in order.
- A lesson marked `is_preview: true` has readable `content_md` and video fields.
- Other lessons have `is_locked: true`, empty content, and empty video fields.
- Quiz objects are omitted entirely; no answer key is public.

`404` is returned for an unknown, draft, archived, or unpublished-topic course/
topic/path. `400` for a malformed topic filter, query, `limit`, or `offset`.
Public list/detail responses are `Cache-Control: public, max-age=120`.

### Enrollment, learner course, progress, and quizzes

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| `POST` | `/api/v3/learning/courses/{slug}/enroll` | Learning account | Create an enrollment (`201`) or return the existing one (`200`) |
| `GET` | `/api/v3/learning/my-learning` | Learning account | Enrollments plus availability and last review date |
| `GET` | `/api/v3/learning/courses/{slug}` | Enrolled learner | Full curriculum, resources, enrollment, completed IDs, attempts |
| `GET` | `/api/v3/learning/lessons/{lesson_id}` | Enrolled learner | One lesson, quiz without answers, resources, attempts |
| `POST` | `/api/v3/learning/lessons/{lesson_id}/complete` | Enrolled learner | Mark the lesson complete and update progress |
| `POST` | `/api/v3/learning/lessons/{lesson_id}/quiz/attempt` | Enrolled learner | Grade a quiz and record the attempt |

Enrollment returns:

```json
{
  "created": true,
  "enrollment": {
    "id": "…",
    "course_id": "…",
    "status": "active",
    "last_lesson_id": "",
    "enrolled_at": "…",
    "completed_at": "",
    "updated_at": "…",
    "completed_lessons": 0,
    "total_lessons": 4,
    "progress_percent": 0
  },
  "course": { "…": "public course object" }
}
```

The course and lesson endpoints return `403` when the caller is signed in but not
enrolled, and `404` when the course/lesson is not published or active. The
learner course payload includes full lesson content and quizzes; quiz options have
`id`, `text`, and `sort_order` only. `is_correct` and question `explanation` are
never included before grading.

`complete` returns:

```json
{
  "completed": true,
  "enrollment": { "…": "enrollment with updated counts", "status": "completed" }
}
```

Progress is over active lessons. Completing every active lesson sets
`status: "completed"` and `completed_at`. My Learning marks an enrollment
`available: false` when its course is unpublished but retains the enrollment and
history.

Quiz attempt body:

```json
{
  "answers": [
    { "question_id": "…", "option_id": "…" }
  ]
}
```

At most 100 answers. Every answer must reference a question and option belonging
to this lesson's quiz; duplicate question IDs are rejected (`400`). One answer
per question is expected, but omitted questions score as incorrect. The server
grades against the stored key:

```json
{
  "attempt": {
    "id": "…",
    "lesson_id": "…",
    "score": 100,
    "passed": true,
    "correct_count": 1,
    "total_questions": 1,
    "attempted_at": "…"
  },
  "score": 100,
  "passed": true,
  "passing_score": 70,
  "correct_count": 1,
  "total_questions": 1,
  "results": [
    {
      "question_id": "…",
      "selected_option_id": "…",
      "correct_option_id": "…",
      "is_correct": true,
      "explanation": "…"
    }
  ],
  "note": "Explanations and the answer key are returned only after a graded attempt."
}
```

The score is integer `floor(correct / total * 100)`; `passed` means
`score >= passing_score`. Attempts are appended, not overwritten. A lesson without
a quiz returns `404`. Taking a quiz does not automatically mark the lesson
complete.

### Authoring, review, and revisions

Authoring requires `instructor`, `editor`, or `admin`. An instructor may manage
only courses with their own `author_id`; editors and admins may manage all
courses. A non-owner receives `403`. A course that is not `draft` returns `409`
for content mutations.

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v3/author/topics` | List all topics, including unpublished |
| `GET` | `/api/v3/author/courses?status=&q=&limit=&offset=` | List manageable courses |
| `POST` | `/api/v3/author/courses` | Create a draft course (`201`) |
| `GET` | `/api/v3/author/courses/{id}` | Full authoring payload, revisions, answer keys |
| `PATCH` | `/api/v3/author/courses/{id}` | Update draft metadata |
| `POST` | `/api/v3/author/courses/{id}/status` | Transition workflow status |
| `POST` | `/api/v3/author/courses/{id}/modules` | Create a module (`201`) |
| `PATCH` | `/api/v3/author/modules/{id}` | Update a module |
| `DELETE` | `/api/v3/author/modules/{id}` | Archive a module (`204`) |
| `POST` | `/api/v3/author/courses/{id}/lessons` | Create a lesson (`201`) |
| `PATCH` | `/api/v3/author/lessons/{id}` | Update a lesson |
| `DELETE` | `/api/v3/author/lessons/{id}` | Archive a lesson (`204`) |
| `POST` | `/api/v3/author/courses/{id}/resources` | Create a resource (`201`) |
| `DELETE` | `/api/v3/author/resources/{id}` | Delete a resource (`204`) |
| `GET` | `/api/v3/author/lessons/{id}/quiz` | Read a quiz with answer key |
| `PUT` | `/api/v3/author/lessons/{id}/quiz` | Replace the lesson's quiz |
| `DELETE` | `/api/v3/author/lessons/{id}/quiz` | Delete the quiz (`204`) |
| `GET` | `/api/v3/author/courses/{id}/revisions` | List version snapshots |
| `GET` | `/api/v3/author/courses/{id}/revisions/{revision_id}` | Read one snapshot |
| `GET` | `/api/v3/author/courses/{id}/analytics` | Aggregate course analytics |

Course creation fields:

```json
{
  "slug": "my-course",
  "title": "My course title",
  "summary": "A short summary of at least ten characters.",
  "description_md": "## Overview\n\nSafe Markdown only.",
  "level": "beginner",
  "duration_minutes": 60,
  "topic_id": "…",
  "outcomes": ["Explain one outcome"]
}
```

`topic_slug` may be sent instead of `topic_id`. Slugs are 2–120 lowercase
letters/digits with single hyphens. Title is 3–160; summary 10–500; description
is required and at most 60,000 bytes; level is `beginner`, `intermediate`, or
`advanced`; duration is 1–100,000 minutes; at most 20 outcomes of 1–300 plain-text
characters each. The response is the authoring payload. New courses start
`draft` at version `0` with the caller as author.

Module body: `title` (3–160), `summary` (≤500), `sort_order` (0–100,000).
Lesson body: `module_id`, `slug`, `title` (3–160), `content_md`, `estimated_minutes`
(1–600), `sort_order`, `is_preview` boolean, `video_provider` (`youtube` or
`vimeo`), and `video_id`. Content is at most 120,000 bytes. Video IDs are 6–64
characters of `[A-Za-z0-9_-]`; both video fields must be empty or both set.
Resources require `title` (3–200), optional `description` (≤1000), an HTTPS safe
`url` (≤2048), `kind` (`link`, `download`, or `tool`), optional `lesson_id`, and
`sort_order`.

Status body:

```json
{ "status": "in_review", "note": "Ready for editorial review" }
```

`note` is plain text ≤500 characters. Allowed transitions and actor:

| From | To | Actor |
| --- | --- | --- |
| `draft` | `in_review` | Owner instructor, editor, admin |
| `in_review` | `draft` | Owner instructor, editor, admin |
| `in_review` | `approved` | Editor, admin |
| `approved` | `published` | Editor, admin |
| `approved`, `published`, `archived` | `draft` | Editor, admin |
| Any non-archived | `archived` | Editor, admin |
| `draft` | `archived` | Owner instructor |

An unauthorized transition is `403`; the same status is `409`. The readiness
check runs before both `approved` and `published`: at least one active module,
at least one active lesson, at least one active lesson in every active module,
and non-empty content in every active lesson. Failure is `400` with the reason.

Publishing increments the version and writes an immutable revision snapshot.
`published_at` is set on first publication; `last_reviewed_at` is refreshed on
every publication and `next_review_due_at` is 180 days after that review. The
note becomes the revision note, or a default is used. The response
is `{"course": {...}, "status": "published"}`. Approving does not publish.

Revision list entries contain `id`, `version`, `created_by`, `created_by_email`,
`note`, and `created_at`; the detail adds a `snapshot` object containing the
course/curriculum payload at publication time. Course-level hard deletion is not
exposed; use `archived`.

Author aggregate analytics:

```json
{
  "course_id": "…",
  "enrollments": 10,
  "active_enrollments": 7,
  "completions": 3,
  "average_progress_percent": 42,
  "quiz_attempts": 12,
  "average_quiz_score": 76,
  "note": "Aggregate AdControl learning metrics only; no named learner data is returned."
}
```

### Learning administration

Both endpoints require the back-office `admin` role.

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v3/admin/learning/users` | List learning accounts (`learner`, `instructor`, `editor`) |
| `POST` | `/api/v3/admin/learning/users` | Create an instructor or editor (`201`) |
| `GET` | `/api/v3/admin/learning/analytics` | Aggregate totals and course-level metrics |

Create body:

```json
{ "email": "instructor@example.com", "password": "at-least-12-characters", "role": "instructor" }
```

`role` must be `instructor` or `editor`; learner accounts self-register. Passwords
are 12–128 characters. The response contains the user object and never a hash or
salt. There is no staff-deletion endpoint in this release.

The analytics response contains `users_by_role`, `totals` (`enrollments`,
`completions`, `quiz_attempts`), course rows with `course_id`, `slug`, `title`,
`status`, `author_email`, and the same aggregate analytics object, plus a note
that no named learner activity is returned. It never returns a learner's email,
individual progress, or attempts.

### Learning content validation

Markdown and plain-text fields reject `<script`, `<iframe>`, `<object>`, `<embed`,
`<style>`, `<link`, `<meta`, `javascript:`, `vbscript:`, `data:text/html`,
`data:application/`, `file://`, `onerror=`, `onload=`, and `onclick=` without
regard to case. Control characters other than tab/CR/LF are rejected. Quiz
validation requires 2–10 options per question and exactly one correct option.
Video frames are restricted by CSP to `www.youtube-nocookie.com` and
`player.vimeo.com`. Resource URLs are validated structurally and never fetched.

## Health

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/healthz` | Process liveness; `200` with status and timestamp |
| `GET` | `/readyz` | Database readiness; `200` or `503` |

Both are `no-store` and are used by container health checks.
