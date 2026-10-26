# AdControl

AdControl is a self-hosted, centralized ad-link and placement management platform
with an embedded marketing learning area. Publishers install one small JavaScript
snippet; after that, placements, creatives, destinations, schedules, and pause
controls are managed from a React dashboard and propagate to live sites without a
redeploy. The same React deployment serves a public course catalog at `/` and the
operations dashboard at `/dashboard`.

The production origin for this deployment is **https://marketing.nevatal.id**.

## What it does

- **Multiple owned websites** with independent public site keys and server-side
  Origin/Referer domain allowlists.
- **Publisher accounts and tenant isolation** (Phase 2). Administrators provision
  invite-only publisher accounts; each publisher sees and manages only its own
  sites, placements, campaigns, offers, and analytics.
- **Domain ownership verification** for publisher-owned sites through a DNS TXT
  challenge at `_adcontrol-challenge.<domain>`, with an administrator-only manual
  fallback. Unverified sites cannot deliver ads.
- **Placements** addressed by stable keys such as `header-banner` or `in-article`.
- **Campaigns/creatives** with title, body, image, alt text, CTA, destination,
  priority, weight, and optional start/end schedules.
- **Multiple monetization sources** for house ads, administrator-managed direct
  ads, and manually entered Shopee affiliate offers. External ad networks are not
  enabled; the reserved `external_network` source is rejected until an approved
  adapter exists.
- **First-party affiliate links**: a Shopee affiliate URL is stored exactly as
  entered and served through a signed `/r/<token>` redirect after server-side
  site, placement, campaign, offer, schedule, and origin revalidation. No
  affiliate API credentials are required and no affiliate links are generated or
  rewritten.
- **Product-card creatives** with merchant, price, image, CTA, and a mandatory
  visible affiliate disclosure. Destinations are never rendered into the page.
- **Public delivery API** with ETag/304 support, short cache lifetimes, CORS, and
  no-store responses for admin data.
- **A dependency-free browser SDK** that renders accessible, clearly labeled ads,
  supports dynamically inserted placements in SPAs, and counts an impression only
  after at least 50% of the placement is visible for one second.
- **Safe click handling** through short-lived HMAC-signed redirects. Destinations
  are revalidated on every click; the browser never submits an arbitrary URL.
- **Analytics** for impressions, clicks, outbound redirects, affiliate clicks,
  CTR, time series, breakdowns by site, placement, campaign, or ad source, date
  filtering, and authenticated CSV export. AdControl-observed metrics are kept
  explicitly separate from externally confirmed conversions and commissions,
  which are not connected in this release.
- **Role-aware operations** including publisher-scoped management, pause
  campaign, pause all campaigns for a site, rotate site key, disable a site,
  audit log, retention, and backups.
- **Fail-open delivery**: if AdControl is unavailable, the host page continues to
  work and placements stay empty (or reserve blank space when configured).
- **Marketing learning platform** (Phase 3): a public catalog of free courses,
  topics, and learning paths; learner registration, enrollment, My Learning,
  progress, resume, and graded quizzes with protected answer keys; and a
  structured authoring/review/publication studio with version and freshness
  tracking. Public SEO pages, canonical URLs, Schema.org course metadata,
  `/sitemap.xml`, and `/robots.txt` are included.
- **Partner intake** (Phase 4): public `/become-a-publisher` and
  `/become-a-client` pages with validated, rate-limited application forms. Leads
  are stored in SQLite and reviewed in an administrator-only **Inquiries** view;
  nothing is provisioned automatically.

## Stack

| Layer | Technology |
| --- | --- |
| API | C++20, cpp-httplib (vendored), nlohmann/json (vendored), SQLite, OpenSSL |
| Web apps | React 18, TypeScript, Vite, Tailwind CSS — public learning at `/`, operations dashboard at `/dashboard` |
| SDK | TypeScript compiled to a small IIFE with esbuild |
| Edge/static | nginx, serving the React apps, demo, SDK assets, and reverse proxy |
| Deployment | Docker Compose, read-only containers, dropped capabilities |
| Storage | SQLite in WAL mode, online backup API |

## Repository layout

```
apps/dashboard/        React public learning app and operations dashboard
apps/sdk/              Browser SDK source and built dist assets
deploy/                Compose stack, nginx config, demo host, .env template
services/api/          C++ API, database layer, learning migrations/seeds, unit tests
scripts/               Backup, restore, API and stack test scripts
tests/browser/         Playwright SDK, strict-CSP, and learning smoke suites
docs/                  Architecture, integration, API, security, learning, operations
```

## Quick start (localhost)

Prerequisites: Docker Engine 24+, Docker Compose v2, and `openssl`.

```bash
cp deploy/.env.example deploy/.env
chmod 600 deploy/.env

# Replace SECRET_KEY, ADMIN_EMAIL, and ADMIN_PASSWORD.
SECRET=$(openssl rand -hex 32)
EMAIL=admin@example.com
PASS=$(openssl rand -base64 24)
sed -i "s|CHANGE_ME_openssl_rand_hex_32|$SECRET|; s|CHANGE_ME_admin_email|$EMAIL|; s|CHANGE_ME_at_least_12_characters|$PASS|" deploy/.env

mkdir -p data
sudo chown "$(id -u):$(id -g)" data
chmod 700 data

docker compose -f deploy/docker-compose.yml up -d --build
curl -fsS http://127.0.0.1:18080/readyz
```

Open the public learning catalog at `http://127.0.0.1:18080/`. The operations
dashboard is at `http://127.0.0.1:18080/dashboard`; sign in with the
`ADMIN_EMAIL` and `ADMIN_PASSWORD` from `deploy/.env`. Those values seed the first
administrator only when the database has no administrator. Change the password
in Settings after the first login, and keep `deploy/.env` out of source control.

The bundled demo publisher is at `http://127.0.0.1:18080/demo/`.

To stop the stack:

```bash
docker compose -f deploy/docker-compose.yml down
```

## First campaign

1. **Websites → Add website.** Set the publisher origin (for example
   `https://www.example.com`) and every origin allowed to load the site. The site
   key is public; the allowlist is the security boundary.
2. **Placements → Add placement.** Choose a stable key, format, dimensions, and
   fallback behavior. Put the key in the publisher markup:
   `<div data-adcontrol-placement="header-banner"></div>`.
3. **Campaigns → Create.** Assign the placement, creative text, optional image,
   HTTPS destination, priority/weight, and optional schedule. Publish it.
4. **Websites → Integration snippet.** Copy the generated snippet, or use:
   ```html
   <script async
           src="https://marketing.nevatal.id/sdk/v1/adcontrol.js"
           data-site-key="site_pub_..."></script>
   ```
5. Load the page. Delivery configuration refreshes at least every 30 seconds, so
   changes appear without touching the site. A browser or CDN may hold a delivery
   response for up to 30 seconds; the goal is propagation within roughly one
   minute.

## Publisher monetization and Shopee offers (Phase 2)

Phase 2 extends the same dashboard and API; existing Phase 1 sites and campaigns
continue to work. New publisher-owned sites must be verified before delivery.

### Onboard a publisher

1. Sign in as an administrator and open **Publishers → Add publisher**. Provide
   the publisher's name, owner email, an initial password (12–128 characters),
   and contact/timezone details. Publisher accounts are invite-only; there is no
   public sign-up.
2. The owner signs in with that email/password. The dashboard is scoped to that
   publisher's tenant: it cannot list or mutate another publisher's inventory.
3. The publisher creates a site in **Websites**. The site starts
   `verification_status: unverified` and `monetization_status: pending`; delivery
   for it returns `404` until verification succeeds.
4. Open the site's verification panel and create the displayed DNS record:
   - type: `TXT`
   - name: `_adcontrol-challenge.<primary-domain>`
   - value: `adcontrol-site-verification=<per-site token>`
   Choose **Verify with DNS TXT**. DNS propagation can take a few minutes.
5. An administrator may use **manual verification** if DNS cannot be changed.
   Publishers cannot self-approve manual verification. Changing the primary
   domain resets verification, so the new domain must be proven again.

### Add and place a Shopee offer

1. Open **Affiliate Offers → Add offer** as the publisher. Enter the Shopee
   affiliate/product URL exactly as provided by Shopee, including all tracking
   and campaign parameters. AdControl does not call the Shopee API, generate
   affiliate links, or alter the query string.
2. The URL must be an absolute `https://` URL with a public hostname, no
   userinfo, no whitespace, and at most 2048 characters. Validation is
   **structural only**: the destination is not fetched. Selecting **Validate**
   records that fact; it does not verify the product or commission.
3. Publish the offer. Offers can be reused by product-card campaigns on any site
   owned by the same publisher, and they can be paused independently.
4. Create a placement whose allowed source types include `affiliate` and whose
   format is `product_card`. Use **Place offer** to create the product-card
   campaign; the server copies the destination from the stored offer, so a
   request cannot substitute another URL. Publish the campaign.
5. The card shows the merchant, product title, price text, CTA, and an affiliate
   disclosure. Clicks go to the first-party `/r/<signed-token>` endpoint, which
   revalidates state and returns a `302` to the exact stored affiliate URL.

The disclosure precedence is: campaign disclosure override, then offer
disclosure, then the site default, then a built-in affiliate disclosure. The SDK
renders the effective disclosure outside the clickable link and never renders an
affiliate card without one. Publisher pages must not hide or remove it.

### Reporting boundary

AdControl observes impressions, signed click redirects, and outbound affiliate
redirects. `affiliate_clicks` and `outbound_redirects` are distinct counters.
`confirmed_conversions` and `confirmed_commission` are always `0` in this release
with provenance `not_connected`. **Do not infer a sale or commission from a
click.** Externally confirmed Shopee orders and commissions require a separately
approved integration and are intentionally out of scope for Phase 2.

### Phase 2 boundaries

- Publisher accounts are provisioned by an administrator only.
- Only publisher-owned sites are supported; there is no open inventory
  marketplace or third-party publisher signup.
- Shopee links are entered manually. No Shopee API credentials are used.
- External ad-network serving is disabled; the `external_network` source type is
  reserved but rejected until terms, credentials, and a reviewed adapter exist.
- HTML-file domain verification is intentionally omitted to avoid adding a
  server-side HTTP fetch surface. DNS TXT and the admin fallback are the
  supported methods.
- The default analytics retention remains **180 days**.

## Marketing learning platform (Phase 3)

Phase 3 adds a public learning area at `/` while keeping the operations dashboard
at `/dashboard`. Existing Phase 1/2 data and routes are unchanged, and no existing
account is enrolled automatically. See [docs/learning.md](docs/learning.md) for
the full behavior, security boundaries, and operational checks.

### Browse and learn

- `/courses`, `/topics`, `/learning-paths`, and their slug detail pages are
  public. The launch catalog is seeded with eight topics, eight courses, 32
  lessons, and four learning paths; operators can edit or archive it through the
  authoring API.
- Register at `/register`. Registration always creates a `learner`; learning
  staff accounts must be created by an administrator.
- Enroll from a published course, then use `/my-learning` and the lesson player.
  Progress is based on lessons explicitly marked complete. A course is complete
  when every active lesson is complete.
- Quizzes are graded server-side. Answer keys and explanations are withheld until
  a graded attempt; public course pages preview only lessons marked as preview.
- Download account data or delete a learner account from `/my-learning`. Staff
  accounts cannot self-delete.

### Author and publish

1. As an administrator, sign in and open `/author/team`. Create an `instructor`
   or `editor` account and share the initial password out of band.
2. An instructor or editor opens `/author` and creates a draft course. Add
   metadata, modules, lessons, resources, and optional quizzes. Content is safe
   Markdown only; raw HTML, scripts, unsafe URLs, and arbitrary iframes are
   rejected. Video embeds are restricted to YouTube/Vimeo IDs.
3. Submit the course for review. An editor or administrator approves and
   publishes it. Approval and publication require at least one module, every
   module to have an active lesson, and every active lesson to have content.
4. Publishing increments the course version and stores an immutable revision
   snapshot. `last_reviewed_at` and `next_review_due_at` (180 days) support
   content freshness review. To change published content, move it back to draft,
   edit, review, and publish the next version.
5. Course analytics are aggregate-only. No named learner progress or activity is
   returned to authors or administrators.

### SEO

`PUBLIC_BASE_URL` sets the canonical origin used by `/sitemap.xml`, `/robots.txt`,
and client-side canonical metadata. It must be an HTTPS origin with no path or
trailing slash; the default is `https://marketing.nevatal.id`. The sitemap includes
only published topics, courses, and paths. Robots allows public learning pages
for all crawlers and excludes private app/API shells from indexing, while every
private endpoint still enforces authentication and authorization server-side.

### Phase 3 boundaries

- Courses are free at launch. There is no checkout, payment, entitlement,
  certificate, cohort, or instructor-marketplace implementation.
- The seed catalog covers digital marketing foundations, advertising/campaign
  optimization, SEO, GEO, SEM/PPC, affiliate/Shopee basics, content/social, and
  analytics/CRO. Topics and learning paths are seeded and read-only in this MVP.
- Practical exercises may reference the AdControl sandbox but never make
  automatic production changes.
- Staff-account removal is an operator procedure; the MVP has no admin
  staff-deletion endpoint.
- Route metadata is client-rendered. The sitemap and canonical URLs are
  server-generated, but full server-side rendering is a later enhancement.

## Partner applications (Phase 4)

The public navbar links to **For publishers** (`/become-a-publisher`) and **For
clients** (`/become-a-client`). Each page explains the offering and submits a
short application: name, email, company, website, and a message. Applications are
validated, capped at 16 KiB, limited to 20 per origin bucket per 10 minutes, and
protected by a hidden bot-trap field.

A submission is stored in `partner_inquiries` and shown to administrators in the
operations dashboard at `/dashboard#inquiries`. Administrators can search and
filter by type and status, reply by email, and mark an inquiry `new`,
`reviewing`, or `closed`. Receipt and status changes are audited. Applications
never create a user, publisher, site, campaign, or session; onboarding remains the
manual, reviewed procedure described above. See
[docs/api.md](docs/api.md#partner-inquiries) and
[docs/security.md](docs/security.md#partner-inquiry-data) for the data-handling
details.

## Production on marketing.nevatal.id with Cloudflare Tunnel

The compose stack serves plain HTTP on `127.0.0.1:${HTTP_PORT}` only by default
(`18080` unless overridden). A host-installed `cloudflared` connector reaches
Cloudflare over an outbound-only tunnel and forwards `https://marketing.nevatal.id` to
that loopback port. No inbound 80/443 ports and no origin certificate are needed
for the tunnel.

The short version:

1. Create a named tunnel in Cloudflare Zero Trust or with `cloudflared tunnel
   create`, and route `marketing.nevatal.id` to it.
2. Configure the public hostname to forward to `http://127.0.0.1:18080`. A
   template is at
   [deploy/cloudflared/config.yml.example](deploy/cloudflared/config.yml.example).
3. Start the stack with a strong `SECRET_KEY`, `ADMIN_EMAIL`, and
   `ADMIN_PASSWORD` in `deploy/.env`. Set `PUBLIC_BASE_URL=https://marketing.nevatal.id`
   so learning canonicals, the sitemap, and robots use the public HTTPS origin.
4. Run the tunnel connector on the host, then verify
   `curl -fsS https://marketing.nevatal.id/readyz`. Confirm `https://marketing.nevatal.id/`
   serves the learning catalog, `https://marketing.nevatal.id/dashboard` serves the
   operations app, and `/sitemap.xml` and `/robots.txt` return `200`.
5. Enable **Always Use HTTPS** and HSTS at Cloudflare only after verification.
   Add a Cloudflare WAF rate limit for `/api/v1/auth/login` and
   `/api/v3/auth/register`; the tunnel connector is a shared origin peer, so
   origin-side per-IP buckets are not per-visitor.
6. Add the publisher origins — **not** `marketing.nevatal.id` — to the allowed domains
   for each website.

Step-by-step commands, systemd setup, firewall rules, and troubleshooting are in
[docs/operations.md](docs/operations.md). Do not expose `127.0.0.1:18080`
publicly or switch `HTTP_BIND` to `0.0.0.0`.

## Testing

The unit tests run during the API Docker build and can also be run directly:

```bash
docker build -t adcontrol-api:latest services/api
```

The build runs `adcontrol-tests`, including URL safety, domain matching, schedule
eligibility, password hashing, campaign selection, event deduplication,
Phase 2 tenant isolation, site verification, affiliate URL preservation, and
online-backup integrity checks.

The API integration suite starts an isolated API container and never touches the
stack database. It covers Phase 1 and Phase 2 API behavior end to end (publisher
provisioning, verification and dark-site delivery, tenancy separation, unsafe
affiliate URL rejection, byte-exact affiliate preservation, `/r/` redirects,
no-open-redirect behavior, product-card delivery, disclosure, and provenance
reporting), plus Phase 3 catalog visibility, SEO output, role boundaries,
registration, enrollment, progress, quiz answer-key protection, authoring
workflow, revisions, unpublish/republish, export, deletion, and aggregate
learning analytics. The current suite result is **194 passed, 0 failed**:

```bash
bash scripts/test-api.sh
```

With the Compose stack running:

```bash
bash scripts/test-stack.sh
```

The stack suite also checks the public learning shell at `/`, the operations app
at `/dashboard`, CSP frame allowlisting for approved video providers,
sitemap/robots proxying and caching, and catalog access through nginx. The current
suite result is **79 passed, 0 failed**.

The stack suite creates a fresh test website and may fill the login rate-limit
bucket at the end. Restart the API container to clear rate-limit state:

```bash
docker compose -f deploy/docker-compose.yml restart api
```

Browser-level checks run in a pinned Playwright container. The SDK suite covers
rendering, signed click links, visibility-qualified impressions, the SPA scanner,
fallback behavior, a real Phase 2 affiliate product card (visible disclosure,
merchant/price rendering, and a `/r/` link with no affiliate destination in the
DOM), and console/CSP health. The strict-CSP suite verifies rendering under
`style-src 'self'`. The learning smoke suite checks the lazy-loaded public app,
route titles/canonicals, phone-width overflow, the dashboard entry, and browser
errors. Current results are **40 SDK, 8 strict-CSP, and 11 learning checks
passed**:

```bash
bash tests/browser/run.sh
```

Run them after the stack suite and API restart described above. The SDK and
strict-CSP suites mutate the seeded test site and must never target production;
the learning smoke suite is read-only and public. See
[tests/browser/README.md](tests/browser/README.md) for prerequisites and the
exact pass/fail output.

## Backup and restore

Automatic online backups run every `BACKUP_INTERVAL_HOURS` (default 24) into
`./data/backups` and keep `BACKUP_RETENTION` snapshots. Manual, verified, gzipped
backups and a guarded restore command are provided:

```bash
./scripts/backup.sh
./scripts/restore.sh data/backups/adcontrol-manual-YYYYMMDD-HHMMSS.db.gz --yes
```

The database and backup files are created with owner-only permissions. RAID is
not a backup. See [docs/backup-restore.md](docs/backup-restore.md) for integrity
checks, off-host copies, retention, and recovery testing.

## Documentation

- [Architecture and delivery flow](docs/architecture.md)
- [Marketing learning platform](docs/learning.md)
- [Publisher integration and CSP/SRI](docs/integration.md)
- [API reference](docs/api.md)
- [Security model](docs/security.md)
- [Operations, TLS, upgrades, and rollback](docs/operations.md)
- [Backups and restores](docs/backup-restore.md)

## Important limitations

- SQLite is used in WAL mode. This is appropriate for a single API instance on a
  single host; do not run multiple API replicas against the same database file.
- Rate limits are kept in process memory and reset when the API restarts.
- The current selection rule is one deliverable campaign per placement. Priority
  chooses the campaign; weight is a deterministic tie-breaker, not weighted
  rotation.
- Authentication uses a bearer token in browser localStorage. The shipped CSP
  mitigates script injection, but an httpOnly-cookie session would be a stronger
  design if the dashboard and API are split across origins.
- Cloudflare Tunnel terminates TLS at Cloudflare and connects to the loopback
  web port. The API sees the connector as its direct peer, so its in-process
  per-IP buckets are shared by all visitors. Add Cloudflare WAF rate limits,
  especially for login, for internet-facing abuse controls.
- Images are linked from HTTPS origins; AdControl does not host or proxy binary
  creative assets.
- Publisher-owned inventory is supported, but the product does not operate an
  inventory marketplace, allow public publisher signup, integrate external ad
  networks, or connect to Shopee's API for conversion/commission data.
- Affiliate URLs are manually entered and structurally validated only. A valid
  URL does not prove that the destination, product, price, or commission is live;
  verify offers operationally. Click and outbound counts are AdControl-observed,
  not confirmed sales.
- The schema carries a read-only `analyst` role, but this release has no
  account-provisioning or tenant-scoping workflow for analysts. Only `admin` and
  publisher-owner accounts are provisioned through the product.
- Domain verification uses DNS TXT or an administrator-only manual fallback. An
  administrator who uses the fallback attests ownership out of band.
- Learning courses are free at launch. There is no payment, paid entitlement,
  certificate, cohort, marketplace, or owned-video hosting. Learning route
  metadata is applied client-side; the sitemap and canonical URLs are available
  without JavaScript, but server-side rendering is not implemented.
- Topics and learning paths are seeded and read-only through the MVP authoring
  UI/API. Staff account creation exists, but staff removal is not exposed through
  an endpoint; offboarding is an operator procedure.
- Learning reports are aggregate only. Per-course author analytics and admin
  totals do not expose named learner activity.

## License

AdControl is released under the [MIT License](LICENSE).
