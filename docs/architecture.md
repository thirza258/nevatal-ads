# Architecture and delivery flow

AdControl is a single-host, self-hosted service that centralizes ad destinations,
schedules, creatives, and placement assignments for websites you own. Host pages
install one script tag; everything after that is managed from the dashboard and
reaches live pages without a redeploy.

The same deployment also serves AdControl Learning: a public marketing course
catalog at `/`, a learner area, and an authoring/review workflow in the back
office. The operations dashboard is at `/dashboard`. Learning has separate roles
and tables but shares the SQLite database, API process, and React build.

The production origin in this repository is **https://marketing.nevatal.id**.

## Components

```
  Editors / Visitors
          │ HTTPS
          ▼
  Cloudflare edge (TLS, DNS, WAF)
          │ outbound-only tunnel
          ▼
  cloudflared on the Docker host
          │ http://127.0.0.1:18080
          ▼
  ┌──────────────────────────────────────────────┐
  │                Docker Compose                │
  │                                              │
  │  web (nginx)                                 │
  │   ├── /                React learning app    │
  │   ├── /dashboard       React operations app  │
  │   ├── /demo/           static demo host      │
  │   ├── /sdk/v1/*        SDK JS + CSS           │
  │   ├── /sitemap.xml, /robots.txt              │
  │   └── /api/, /delivery/, /click/, /r/        │
  │              │ proxy_pass                    │
  │              ▼                               │
  │  api (C++20, cpp-httplib)                    │
  │   ├── admin + publisher API + sessions       │
  │   ├── domain verification (DNS TXT)          │
  │   ├── public delivery config                 │
  │   ├── impression ingestion                   │
  │   ├── signed /click/ and /r/ redirects       │
  │   ├── learning catalog, enrollment, progress │
  │   ├── authoring, review, revisions, SEO      │
  │   └── maintenance + backup thread            │
  │              │                               │
  │              ▼                               │
  │  SQLite (WAL) /data/adcontrol.db             │
  └──────────────────────────────────────────────┘

  host page ── script ──► delivery config ──► render
                                  ├── impression beacon
                                  ├── /click/<token> ──► house/direct destination
                                  └── /r/<token> ──────► exact stored affiliate URL
```

| Component | Responsibility |
| --- | --- |
| `web` | HTTP origin behind Cloudflare Tunnel, static learning/dashboard/demo/SDK assets, SEO proxy routes, reverse proxy, security headers, cache policy |
| `api` | Authentication, role/tenant authorization, domain verification, affiliate-offer validation, delivery selection, event recording, advertising and learning analytics, learning catalog/authoring, audit log, retention, backups |
| `apps/sdk` | Fail-open browser renderer, visibility-qualified impressions, SPA rescans, product-card disclosure, signed `/click/` and `/r/` navigation |
| `apps/dashboard` | One React build with separate lazy entry points for the public learning app (`/`) and operations dashboard (`/dashboard`) |
| SQLite | All state; one database file in WAL mode |
| Cloudflare edge + Tunnel | Public HTTPS listener, certificate management, HSTS, WAF/rate limiting, outbound-only connection to the host |

The API is not published directly. Compose binds the web container to
`127.0.0.1:${HTTP_PORT:-18080}` by default; only the local Cloudflare Tunnel
connector should reach it. See [operations.md](operations.md) for tunnel setup.

## Request flows

### 1. Admin and publisher dashboard

1. The browser loads the operations React entry from `/dashboard`. Visiting `/`
   loads the public learning entry instead; legacy root hashes such as
   `/#campaigns` still select the dashboard entry.
2. `POST /api/v1/auth/login` verifies the password with PBKDF2-HMAC-SHA256
   (100,000 iterations, per-user salt) and returns an opaque 32-byte bearer token
   valid for 30 days. The token is a random value stored server-side; it is not
   a JWT and carries no claims.
3. The dashboard sends `Authorization: Bearer <token>` on every request and keeps
   the token in `localStorage`. Logout deletes the server-side session. The
   session's role and `publisher_id` drive navigation and read-only states; the
   API enforces the same scope independently.
4. Mutating endpoints require the `admin` or `publisher` role. Publishers are
   pinned to their own tenant and may create only house or affiliate campaigns.
   Administrators provision publishers, manage direct campaigns, can use the
   manual verification fallback, and read the audit log.
5. Every create, update, delete, publish, pause, verification, offer action, key
   rotation, login, and password change writes an audit row.

The API never returns password hashes or salts, and settings never expose
`SECRET_KEY`. Publisher-owner accounts are created only by an administrator.

### 1a. Publisher site verification and affiliate offers

1. A publisher-owner account creates a site with `publisher_id` forced from its
   session. The site begins unverified with monetization pending.
2. `GET /api/v1/sites/{id}/verification` returns a per-site DNS TXT challenge.
   `POST /api/v1/sites/{id}/verify` resolves the TXT record server-side and
   compares the exact token. No HTTP fetch is performed, avoiding an SSRF
   surface. An administrator can verify manually out of band.
3. Verification marks the site verified and monetization-enabled. Delivery and
   impression ingestion still also require an active site and active publisher.
4. A publisher enters a Shopee affiliate URL manually. AdControl stores it
   byte-for-byte and validates only its structure (HTTPS, public hostname, no
   userinfo/whitespace, bounded length). It does not call Shopee, fetch the
   destination, or generate a link.
5. Placing an offer creates a `product_card` campaign server-side with the
   destination copied from the offer. Client input cannot substitute a URL.
6. Delivery sends affiliate metadata and disclosure but never the destination.
   A signed `/r/` URL is the only browser-facing outbound handle.

### 1b. Public learning and learner flow

1. The browser loads the learning entry from `/`. Course, topic, path, and SEO
   requests go to `/api/v3/catalog/*`, `/sitemap.xml`, and `/robots.txt`.
2. The API filters on **published** status for both the course and its topic.
   Public detail returns locked, content-free entries for non-preview lessons
   and omits quizzes and author identity entirely.
3. A visitor registers at `/register`. The server always creates a `learner`,
   hashes the password, creates an opaque session, and performs no enrollment.
4. Enrollment re-checks that the course and topic are published. Once enrolled,
   the learner course endpoint returns full lesson content and quizzes without
   answer keys or explanations.
5. Explicit lesson completion updates enrollment progress. A graded quiz attempt
   is scored server-side and only then returns the correct option and
   explanation. A learner cannot read another learner's enrollment or attempts.
6. My Learning exposes the learner's own export and account deletion. Staff
   roles cannot use the learner self-delete path.
7. The sitemap is generated from the same published-status rules at request
   time and cached for 30 minutes; robots excludes private app/API shells from
   indexing without replacing server-side authorization.

### 1c. Learning authoring and publication

1. An administrator creates `instructor` or `editor` accounts through
   `/author/team`. There is no public staff signup.
2. `instructor` callers can only load and mutate courses whose `author_id`
   matches their user ID. `editor` and `admin` can manage all courses. The
   ownership check runs before course, module, lesson, resource, quiz, revision,
   or analytics access.
3. A draft is edited through structured modules, lessons, resources, and
   quizzes. Publication readiness requires a module/lesson tree with content in
   every active lesson. Non-draft content mutation returns `409`.
4. Instructors submit to `in_review`; only editors/admins approve, publish, or
   unpublish. Publishing increments the version, refreshes review dates, and
   stores an immutable revision snapshot. Unpublishing returns the course to
   draft and removes it from the public catalog and sitemap while retaining
   learner history.
5. Course analytics are aggregated by SQL. No named learner row is returned to
   an author or admin report.

### 2. Delivery configuration

1. The host page loads
   `https://marketing.nevatal.id/sdk/v1/adcontrol.js` with a public site key.
2. The SDK calls
   `GET /delivery/v1/sites/<public_site_key>/config` with `credentials: omit` and
   no cookies.
3. The API validates the site key, active site status, verification status,
   monetization status, active publisher status, and the request
   `Origin`/`Referer` against the site allowlist. It then selects at most one
   eligible campaign per active placement.
4. The response lists every **active placement** for the site. A placement with
   an eligible campaign has a campaign id, creative fields, source type,
   disclosure, affiliate metadata when applicable, and a signed `click_url`
   (`/click/` or `/r/`). A placement with no eligible campaign has
   `campaign_id: ""` and no `click_url`. The empty entry is what lets the SDK
   apply the configured fallback consistently. Destination URLs are never
   included.
5. The SDK renders each placement element, observes it, and counts an impression
   only after at least 50% of the element has been visible for one continuous
   second. Product cards render the disclosure outside the clickable anchor.

The response is cacheable for 30 seconds and carries a weak ETag derived from the
stable configuration, an hourly token-issuance window, and a random per-process
nonce. Conditional requests that match return `304`, so connected pages do not
re-download unchanged configuration every poll. Signed click tokens are
deliberately excluded from the ETag hash; an API restart (including one that
rotates `SECRET_KEY`) changes the nonce so old cached click tokens are not
served.

### 3. Impression event

`POST /delivery/v1/events` accepts **impressions only**. The request is
CORS-safelisted (`Content-Type: text/plain`), sent with `sendBeacon` where
available and `fetch(..., keepalive)` otherwise. The API:

- checks the per-IP/per-site event rate limit and the 8 KiB payload cap,
- validates the site, origin, placement, and that the campaign belongs to that
  placement,
- generates its own event row and uses the SDK-supplied `event_id` as a unique
  deduplication key,
- stores a minimized referrer (origin/path without query or fragment) and a
  coarse user-agent category.

The database stores a truncated HMAC of the IP address keyed by `SECRET_KEY`; it
does not store the raw IP. No cookies or persistent visitor ids are created.

Client-submitted click beacons are rejected with `400`; clicks are recorded only
by the server-side redirect.

### 4. Click redirects and affiliate outbound

1. The SDK points the creative's anchor at a relative `/click/<token>` or
   `/r/<token>` URL issued in the delivery config. The SDK verifies that the URL
   resolves to the configured API origin and starts with the expected path.
2. The API verifies the HMAC-SHA256 signature, embedded campaign id, and 24-hour
   TTL using constant-time comparison.
3. The API re-checks, at click time: site status/verification/monetization,
   publisher status, placement status, campaign status, schedule, destination
   safety, offer state for affiliate campaigns, and the request origin allowlist.
4. For an affiliate campaign, the redirect target is the exact stored affiliate
   URL, after confirming the offer is active, belongs to the site's publisher,
   and is not rejected. No tracking parameters are injected, removed, or
   reordered. The browser cannot supply a destination.
5. If everything is valid, it records one click event (plus an
   `outbound_redirect` event for `/r/`) and returns a `302` with
   `Referrer-Policy: no-referrer`. Otherwise it returns `400`, `403`, `410`, or
   `429`, never a redirect to an unverified URL.

There are no arbitrary destination URLs in query strings, so the endpoint cannot
be used as an open redirect. Pausing a campaign, expiring a schedule, disabling a
site/publisher, or deactivating an offer invalidates existing click links
immediately. A click or outbound redirect is an observed event, not a confirmed
conversion or commission.

## Data model

```
Advertising plane
users ──< sessions
publishers ──< sites ──< placements ──< campaigns
     │            │                         │
     │            └────────< ad_events >────┘
     └──< affiliate_offers ──< campaigns (affiliate)

Learning plane
learning_topics ──< learning_courses ──< learning_modules ──< learning_lessons
                            │                                      │
                            │                                      └──< learning_quizzes
                            │                                             └──< questions ──< options
                            ├──< learning_resources
                            ├──< learning_course_revisions
                            └──< learning_enrollments ──< learning_lesson_progress
                                                         └──< learning_quiz_attempts
learning_paths ──< learning_path_courses >── learning_courses
users ──< learning_enrollments
users ──< learning_course_revisions (created_by)

audit_logs (append-only from the API's perspective)

partner_inquiries (public applications; no account or tenant link)
```

The advertising and learning tables share the `users` table and session store but
use distinct roles and authorization helpers. Learner accounts have no
`publisher_id`; publisher/analyst accounts cannot use learning APIs.

| Entity | Key fields | Notes |
| --- | --- | --- |
| `users` | email, password_hash, salt, role, publisher_id | `publisher` owners link to a tenant; admins have no publisher |
| `publishers` | name, owner_user_id, status, contact_email, timezone (`owner_email` is derived from the owner login) | Invite-only; provisioning creates the owner login |
| `sites` | publisher_id, name, primary_domain, allowed_domains, public_site_key, verification_status, monetization_status, default_disclosure_text, allowed_ad_sources, config_version | Publisher sites start unverified; allowlist plus verification gate delivery |
| `placements` | site_id, placement_key, format, width, height, fallback_behavior, allowed_source_types, max_items, disclosure_mode, status | `UNIQUE(site_id, placement_key)` |
| `campaigns` | site_id, placement_id, source_type, affiliate_offer_id, destination_url, disclosure_override, creative fields, status, schedule, priority, weight | Drafts are invisible to delivery until published; affiliate destinations are server-derived |
| `affiliate_offers` | publisher_id, provider, product metadata, exact affiliate_url, canonical_product_url, disclosure_text, status, validation_status | Manually entered; structural validation only; publisher-scoped |
| `ad_events` | site_id, placement_id, campaign_id, source_type, affiliate_offer_id, event_type, event_id, occurred_at, page_origin, referrer, ua_category, ip_hash | `event_id` is globally unique; impressions, clicks, and outbound redirects share the table |
| `learning_topics` | slug, title, description, status, sort_order | Seeded; public only when published |
| `learning_courses` | slug, topic_id, metadata, status, author_id, reviewer_id, version, published_at, last_reviewed_at, next_review_due_at | Draft → in_review → approved → published → archived; version/revision on publish |
| `learning_modules`, `learning_lessons` | course_id, titles, ordering, content_md, video, active/archive status, preview flag | Soft-archive so learner history stays valid |
| `learning_resources` | course_id, optional lesson_id, title, HTTPS url, kind | Links only; never fetched server-side |
| `learning_quizzes`, questions, options | lesson_id, passing_score, prompts/explanations, one correct option | Answer key never sent to enrolled learners before grading |
| `learning_paths`, `learning_path_courses` | slug, metadata, ordered course links | Seeded and read-only in the MVP |
| `learning_enrollments` | user_id, course_id, status, last_lesson_id, completed_at | Unique `(user_id, course_id)`; not created automatically |
| `learning_lesson_progress` | enrollment_id, lesson_id, completed_at | Unique per enrollment/lesson |
| `learning_quiz_attempts` | enrollment_id, user_id, course_id, lesson_id, quiz_id, score, passed, counts | Appended attempts; used for aggregate reporting |
| `learning_course_revisions` | course_id, version, created_by, note, snapshot, created_at | Immutable snapshot at each publication |
| `partner_inquiries` | kind (`publisher`/`client`), name, email, company, website, message, status, timestamps | Public marketing-site applications; admin-only review; never provisions an account |
| `audit_logs` | actor, action, entity_type, entity_id, details, created_at | Sensitive-change trail; admin-only read |
| `sessions` | token, user_id, expires_at | No secrets are returned to clients |

Foreign keys are enabled. Deleting an entity with analytics history is refused
or converted into an archive/disable so event rows never dangle.

## Campaign selection and scheduling

Delivery applies these rules in order:

1. Site status must be `active`.
2. The site must be `verified` with monetization `enabled`. Publisher-owned
   sites additionally require an `active` publisher; the publisher is looked up
   from the site, never from the request. Platform-owned sites have no publisher
   and are seeded verified/enabled.
3. The request `Origin` (or `Referer`) must pass the site allowlist.
4. Placement status must be `active`.
5. Campaign status must be `active` or `scheduled`.
6. The current UTC time must fall inside `[starts_at, ends_at]` when either bound
   is set.
7. The campaign's `source_type` must be allowed by the placement's
   `allowed_source_types`.
8. Ordinary destinations must still pass URL safety validation when read from
   the database.
9. Affiliate campaigns require a non-empty `affiliate_offer_id`; the referenced
   offer must exist, be `active`, not be `rejected`, belong to the same
   `publisher_id` as the site, and still pass strict affiliate URL validation.
10. For each placement, campaigns are ordered by `priority DESC`, then
    `weight DESC`; the first eligible campaign is delivered.

The current selection rule is **one deliverable campaign per placement**.
`priority` picks the campaign; `weight` is a deterministic tie-breaker, not
weighted rotation. The status of a scheduled campaign is advanced to
`active`/`expired` by hourly maintenance and opportunistically when delivery or
clicks are evaluated.

Site eligibility and origin checks are re-applied independently on impression
ingestion, which also verifies that the placement exists and the campaign belongs
to that site and placement. `/click/` and `/r/` apply the full delivery gate plus
destination or offer validation. Delivery-time eligibility is not cached as
authorization: disabling a publisher, unverifying a site, pausing an offer, or
editing a URL invalidates the next click even when the browser still holds a
previously delivered config.

## Propagation and caching budget

There are four independent delays:

| Layer | Budget |
| --- | --- |
| Admin writes to SQLite | Immediate |
| SDK polling interval | 30 seconds (`refresh_interval_seconds`) |
| Delivery `Cache-Control` / browser HTTP cache | 30 seconds (`max-age=30`, `stale-while-revalidate=30`) |
| Stale-config tolerance in the SDK | 300 seconds (`max_stale_seconds`) |

Normal propagation is therefore roughly **30–60 seconds** after an edit. A page
that remains open still refreshes on its own timer; a page reload gets the new
config immediately (subject to the browser cache). The `304` path avoids
re-downloading unchanged bodies but does not extend the propagation budget.

A `403` (domain no longer allowed) or `404` (site disabled/key rotated) response
makes the SDK clear every rendered ad immediately. A network failure keeps the
last good config while it is younger than `max_stale_seconds`, then clears
rendered ads on the next failed refresh. This is the fail-open/fail-safe
boundary: an outage removes ads rather than leaving stale, potentially paused
content clickable.

### Learning caching and SEO

| Response | Cache policy |
| --- | --- |
| Public catalog lists and detail | `public, max-age=120` |
| `/sitemap.xml`, `/robots.txt` | `public, max-age=1800` |
| Learner enrollment/progress/quiz and export | `no-store` |
| Authoring, revisions, analytics, admin learning | `no-store` |
| Hashed JS/CSS assets | `public, max-age=31536000, immutable` |

nginx defaults document/SPA routes to `no-store` and passes API cache headers
through for the sitemap, robots, delivery, and SDK paths. The sitemap uses
`PUBLIC_BASE_URL` (HTTPS only) and is generated from published rows at request
time. Learning metadata is applied client-side by the React app; the sitemap
and canonical URL are available without JavaScript, but full server-side
rendering is not implemented. Approved video frames are the only cross-origin
frames permitted by CSP.

## Storage and maintenance

One background thread in the API:

- runs every hour: purges events older than `EVENT_RETENTION_DAYS` (default 180)
  and advances campaign states;
- uses SQLite's online backup API on startup and every
  `BACKUP_INTERVAL_HOURS` (default 24), writes a temp file, verifies integrity,
  atomically renames it, and prunes to `BACKUP_RETENTION` files.

Phase 3 tables are created with additive `CREATE TABLE/INDEX IF NOT EXISTS`
migrations. The launch catalog is inserted once under the
`learning_seed_state` key `launch-catalog-v1`; later restarts do not recreate or
overwrite edited/archived content. Existing users are never auto-enrolled.

`EVENT_RETENTION_DAYS` purges advertising events only. Learning accounts,
enrollments, progress, attempts, authoring revisions, and audit rows are not
purged by that job; learner self-deletion or an explicit operator procedure is
required. Include all of these tables when estimating database growth and backup
scope.

SQLite is configured with WAL, foreign keys, `synchronous=NORMAL`, and a
5-second busy timeout. This is appropriate for one API process on one host.
Running multiple API replicas against the same database file is unsupported.

## Deployment topology

```
DNS: marketing.nevatal.id ──► Cloudflare edge ──► tunnel ──► cloudflared
     ──► 127.0.0.1:18080 nginx ──► api:8088
```

- TLS terminates at Cloudflare; the tunnel is outbound-only and the compose
  stack speaks plain HTTP on loopback by default.
- nginx keeps a single set of security headers, overwrites `X-Forwarded-For`
  with the direct peer, and applies cache policy per path.
- Containers run read-only with dropped capabilities and
  `no-new-privileges`.
- The database and backups live in `./data`, mounted into the API container.

## Known boundaries

- **Owned inventory only.** Publishers manage sites they own; there is no
  marketplace, bidding, or third-party inventory discovery, and publisher
  accounts are invite-only with no public signup.
- **Two role planes.** Advertising uses `admin`/`publisher` (with an
  unprovisioned read-only `analyst` role); learning uses
  `learner`/`instructor`/`editor` plus learning administration by `admin`.
  Learning accounts cannot access advertising APIs, and publisher/analyst
  accounts cannot access learning APIs. There is no public staff signup and no
  staff-removal endpoint in the learning MVP.
- **Free learning only.** There is no payment, entitlement, certificate, cohort,
  marketplace, or owned-video hosting. Learning topics and paths are seeded and
  read-only; only courses have an authoring workflow. Route metadata is
  client-rendered, not server-rendered.
- **No conversion tracking.** Affiliate URLs are manually entered and
  structurally validated only. AdControl does not call Shopee, generate
  affiliate links, ingest commissions, or confirm sales. `confirmed_conversions`
  and `confirmed_commission` remain zero with provenance `not_connected`.
- **No external ad network.** Provider integration is represented by the
  `external_network` source type, but it is rejected at validation until terms
  and credentials are approved. No provider adapter is implemented, so serving
  makes no third-party network calls.
- **No creative image hosting or proxying;** images are hotlinked from HTTPS
  URLs. No weighted rotation, frequency capping, or geo targeting.
- Rate-limit counters are in process memory and reset on restart. Behind
  Cloudflare Tunnel all visitors share the origin peer, so use Cloudflare WAF
  rate limiting for internet-facing abuse controls.
- Analytics are first-party, server-side deduplicated counts. Ad blockers,
  browser tracking protections, and client-side failures can reduce measured
  impressions; there is no cross-device identity. Clicks measure observed
  redirects, not unique people or purchases.
