# Security model

AdControl makes ad destinations remotely controllable, so the security boundary
is the admin plane and the integrity of delivery. This document describes what is
enforced, what is deliberately not stored, and where the remaining risks are.
It does not replace a deployment-specific review.

## Trust model

| Party | Trusted for |
| --- | --- |
| Administrator | Provisioning publishers and learning staff, configuring all inventory, verification fallback, destinations, pauses, learning administration |
| Publisher account | Managing only its own sites, placements, offers, campaigns, and analytics |
| Instructor | Authoring only its own learning courses; submitting/withdrawing from review |
| Editor | Reviewing, approving, publishing, and managing all learning courses |
| Learner | Managing only its own enrollment, progress, quiz attempts, and account export/deletion |
| Publisher page | Hosting the public site key and placement markup |
| Affiliate offer data | Operator-entered Shopee URL and product metadata; not fetched or independently verified |
| Learning content | Staff-authored Markdown, quiz data, and HTTPS resources; structurally validated and safely rendered, not assumed benign |
| Visitor browser | Reporting qualified impressions; following signed click links |
| Cloudflare edge + Tunnel | Terminating HTTPS, DNS, WAF/rate limiting, outbound-only origin connection |
| Docker host | Running containers, `cloudflared`, and protecting the data directory |
| AdControl API | All authorization decisions, destination validation, click recording, quiz grading, learning access checks |

The **public site key is not a secret**. It identifies a site in public HTML.
The security controls that matter are the admin authentication, the origin
allowlist, and server-side revalidation on every click.

## Secrets and credentials

| Secret | Where it lives | Rules |
| --- | --- | --- |
| `SECRET_KEY` | `deploy/.env` → API environment | ≥32 characters in production; generate with `openssl rand -hex 32`; never in git, HTML, logs, or API responses |
| Admin password | `deploy/.env` only for first seed; dashboard Settings afterwards | 12–128 characters; stored as PBKDF2-HMAC-SHA256 with a per-user salt |
| User session token (all roles) | Browser `localStorage` + `sessions` table | 32 random bytes; 30-day expiry; sent only in the `Authorization` header |
| Public site key | Public HTML / dashboard | Not a secret; rotate if abused |
| Publisher passwords and sessions | Provisioned by an administrator; stored as PBKDF2 hashes/salted; sessions server-side | Same rules as admin credentials; never returned in API responses |
| Learner and learning-staff passwords | Self-registration (learner) or administrator provisioning (instructor/editor); PBKDF2 hash + per-user salt | 12–128 characters; never returned, logged, or included in exports |
| Learning quiz answer keys | Quiz question/option rows | Never sent to a learner before grading or exposed on public course pages; returned only to authorized authors and after a graded attempt |
| Domain verification token | Site row + authenticated verification endpoint | Random per-site token; omitted from the normal site object; cleared on verification |
| Affiliate URLs | Offer row, authenticated dashboard responses, and the outbound redirect only | Not a secret, but never included in public delivery config or visitor-page DOM |
| Provider credentials | Not used in Phase 2 | No Shopee API or ad-network credentials are stored. Future credentials must be write-only, encrypted at rest, and never returned to clients. |
| Click-token signing key | Derived from `SECRET_KEY` | HMAC-SHA256 over campaign id, issue time, and nonce |

`deploy/.env` is gitignored and should be `chmod 600`. The API never logs request
bodies, passwords, tokens, or `SECRET_KEY`. Changing `SECRET_KEY` invalidates
all outstanding click tokens and breaks IP-hash continuity. It does **not**
invalidate admin sessions, which are opaque database rows independent of the
signing key; change the admin password to invalidate sessions.

The dashboard API does not return `password_hash`, `salt`, or `SECRET_KEY`.
Settings expose only operational values.

## Authentication and sessions

- Passwords use PBKDF2-HMAC-SHA256, 100,000 iterations, 16-byte random salt,
  constant-time comparison.
- Login is rate-limited to 10 attempts per IP per five minutes. Unknown accounts
  perform equivalent hashing work to reduce timing-based user enumeration.
- Session tokens are opaque random values stored server-side. Logout deletes the
  row; password change deletes every session for the user.
- Advertising mutating endpoints require the `admin` or `publisher` role.
  Publishers are additionally constrained to their own `publisher_id` on every
  site, placement, campaign, offer, analytics, and verification query.
  Administrator-only actions include publisher provisioning, manual verification
  fallback, direct campaigns, the audit log, and provider administration.
- Advertising read endpoints require a valid session. An account with no
  publisher link cannot use tenant-scoped inventory or analytics endpoints;
  administrators are the exception.
- Learning access uses separate authorization helpers. `authenticate_learning`
  accepts `learner`, `instructor`, `editor`, or `admin`;
  `authenticate_learning_staff` accepts the three staff roles;
  `authenticate_learning_editor` accepts `editor` or `admin`. The advertising
  helper rejects learning roles, and the learning helpers reject
  `publisher`/`analyst`. `admin` is the only dual-plane role.
- Public registration always forces role `learner`; a caller-supplied `role` is
  ignored. Instructors and editors are created only by an administrator.
  Instructors can read and mutate only courses whose `author_id` matches their
  own user ID; editors and admins can manage all courses. The server enforces
  this on every course, module, lesson, resource, quiz, revision, and analytics
  path.
- The schema recognizes a read-only `analyst` role, but this release does not
  provision analyst accounts or assign them to a tenant. Do not create analyst
  users manually without reviewing every endpoint's tenant filter.
- The initial administrator is seeded from `ADMIN_EMAIL`/`ADMIN_PASSWORD` only
  when no user with that email exists. In `ADCONTROL_ENV=production`, startup
  fails if the password is missing or shorter than 12 characters when seeding is
  needed. Change the seeded password after the first login.
- Publisher-owner credentials are created by an administrator and must be
  communicated out of band. There is no email invitation, password reset, or
  public signup. Rotate or disable a compromised publisher through its owner
  account/publisher status.

### Residual risks

- **No MFA.** Protect the account with a strong unique password and restrict the
  dashboard at the proxy if your threat model requires it.
- **Bearer token in `localStorage`.** This is XSS-readable. The shipped CSP
  (`script-src` limited to `'self'` and the SRI-pinned Cloudflare Insights
  beacon, no inline scripts) and React's escaping materially reduce XSS risk,
  but an httpOnly cookie with CSRF protection would be stronger. The
  dashboard and API share an origin, so the current design avoids CSRF while
  accepting the XSS trade-off.
- **No per-account lockout or IP allowlist** beyond the rate limit. Add these in
  Cloudflare (WAF, Access) or in front of the dashboard if needed.
- **Sessions are stored as raw random tokens.** An attacker who can read the
  database also has live sessions. Protect the database file and backups.

## Destination and creative validation

All destinations and image URLs pass server-side validation in
`services/api/include/utils.hpp`:

- only `http://` and `https://` schemes; `javascript:`, `data:`, `file:`,
  `vbscript:`, and other schemes are rejected;
- no whitespace, control characters, or backslashes;
- no userinfo (`user:pass@host`) to reduce credential-phishing destinations;
- a host must be present; scheme-only and protocol-relative forms are rejected;
- maximum length 2048.

Validation runs on create, update, publish, delivery selection, and click
redirect. A destination tampered with directly in the database is skipped during
delivery and rejected at click time. HTTPS is strongly recommended for
destinations and required for images on HTTPS publisher pages (mixed content).

Affiliate URLs use a stricter policy than ordinary destinations:

- HTTPS is mandatory; plain HTTP is rejected by default.
- The authority must contain a syntactically valid public hostname with at least
  one dot; `localhost` and attribute-like bare hosts are rejected.
- Userinfo (`user:pass@host`), whitespace, control characters, backslashes, and
  malformed ports are rejected.
- The URL is not fetched, resolved, or checked with an HTTP client. Validation is
  structural only, eliminating an SSRF primitive.
- The stored string is preserved byte-for-byte. AdControl does not add, remove,
  decode, or reorder query parameters, and it does not generate affiliate links.
- Public delivery config contains only a signed `/r/` token, never the
  destination. The API resolves the stored URL at redirect time.

An affiliate URL's structural validity says nothing about product availability,
price accuracy, merchant legitimacy, or commission. Treat offers as
operator-attested content and review them operationally.

Creative text is rendered with `textContent`, never `innerHTML`. The SDK creates
elements with DOM APIs and never calls `eval` or `new Function`. Image URLs are
validated in the browser as well. User-supplied URLs in admin views are rendered
through normal React escaping. Affiliate disclosure is appended outside the
clickable anchor and cannot be suppressed by the SDK for an affiliate card.

## Learning content, answer keys, and learner privacy

Learning is a second application plane, not a trusted extension of the
advertising dashboard. Its authorization, content, and data-minimization rules
are enforced independently:

- **Draft and restricted content.** A course is publicly readable only when both
  the course and its topic are `published`. Public detail hides non-preview
  lesson content, video IDs, author identity, and every quiz. Enrollment,
  lesson access, progress, and quiz attempts re-check published status and
  enrollment server-side. Hiding a course from search or the sitemap is not an
  access control.
- **Answer-key protection.** Learner course/lesson payloads omit `is_correct`
  and question explanations. The server grades against stored keys and returns
  the selected option, correct option, and explanation only in the graded
  attempt response. A learner cannot submit a score or choose a question outside
  the lesson's quiz; unknown options and duplicate answers are rejected.
- **Authoring ownership.** A caller who is not the owning instructor and not an
  editor/admin receives `403` before any course child resource is read or
  mutated. Non-draft courses reject content mutation with `409`. Approval,
  publication, unpublishing, and archiving require an editor/admin, and readiness
  is checked before approval and publication.
- **Safe content.** Course descriptions and lessons use a restricted Markdown
  renderer that returns React nodes. It never calls `dangerouslySetInnerHTML`,
  `innerHTML`, `eval`, or `new Function`. The API rejects raw HTML tags,
  `javascript:`/`vbscript:`/dangerous `data:` URIs, `file://`, inline event
  handlers, and control characters. Titles, summaries, quiz fields, resource
  descriptions, and review notes are plain text.
- **Video allowlist.** Lesson video is limited to YouTube or Vimeo provider
  fields with a 6–64 character ID. The player constructs only the corresponding
  privacy-enhanced embed. nginx permits frames only from
  `https://www.youtube-nocookie.com` and `https://player.vimeo.com`; all other
  iframe origins are blocked by CSP.
- **Resource URLs.** Resources require an absolute HTTPS URL that passes the
  shared URL validator and is ≤2048 characters. The API never fetches resource
  URLs, so authoring is not an SSRF primitive. Files are linked, not proxied or
  executed by AdControl.
- **No production side effects.** Course exercises may reference the AdControl
  sandbox, but the learning plane has no write path into advertising inventory.
  Do not author exercises that automatically change live campaigns, DNS,
  credentials, or other production systems.
- **Learner data minimization.** Learning stores the account email, PBKDF2
  password hash/salt, role, enrollments, completed lesson IDs, last completed
  lesson, quiz scores/counts/timestamps, and timestamps. It does not create a
  separate visitor profile or store learner IP addresses in learning tables.
  Rate-limit buckets key on IP or user ID in process memory and reset on restart.
- **Learner control.** A signed-in learner can export their own account,
  enrollments, completed lesson IDs, and quiz attempts, and can self-delete a
  `learner` account. Deletion removes sessions and cascades enrollments,
  progress, and attempts. Staff accounts cannot self-delete; the MVP has no
  staff-removal endpoint, so offboarding is an explicit operator procedure.
  Backups may retain deleted learner data until their retention expires.
- **Aggregate reporting.** Author analytics return per-course counts and
  averages only. Admin learning analytics return totals, role counts, and
  course-level metrics with author email. Neither returns named learners,
  individual progress, or individual attempts. Course content and support notes
  must not be used to store unnecessary personal data.

The 180-day `next_review_due_at` is a content-governance reminder, not a security
control. Publishing again refreshes `last_reviewed_at` and the due date.

## Domain allowlisting

Every site has `allowed_domains` (or falls back to `primary_domain`). The API
checks the request `Origin`, falling back to `Referer`, against that list for:

- delivery config,
- impression events,
- click redirects.

Matching is host-based and case-insensitive; schemes and paths are ignored.
Entries may include a port, a wildcard subdomain (`*.example.com`), or `*`
(explicitly opt out; not recommended). A wildcard without a port matches any
port.

Limitations to understand:

- A non-browser client can forge `Origin`/`Referer`. The allowlist prevents other
  websites from embedding a stolen site key in a browser; it is not a
  cryptographic access control for server-to-server traffic. The site key plus
  the rate limits are the practical abuse controls.
- A malicious page on an **allowed** domain can render ads and generate events.
  Only list domains you control.
- When the browser strips both headers (privacy settings), the API allows the
  request if the site key is valid. Events still require a valid placement and
  campaign; they are limited per IP. This is a compatibility trade-off.
- CORS for public endpoints is `*` by design so any allowed publisher origin can
  fetch. Responses contain no admin secrets.

## Domain ownership verification

Sites created by publisher accounts start unverified and cannot deliver ads or
record impressions until ownership is proven. Verification uses a random token
stored on the site and a DNS TXT record at
`_adcontrol-challenge.<primary-domain>`:

```
adcontrol-site-verification=<token>
```

The API performs a DNS TXT lookup and compares the value exactly. It does not
make an HTTP request to the publisher's site, so the verification flow cannot be
used for SSRF against internal services. The token is not included in the normal
site JSON, only in the authenticated challenge response. A primary-domain change
resets verification so an old domain cannot vouch for a new one.

An administrator can use the manual fallback to mark a site verified without DNS.
That is an explicit out-of-band attestation and bypasses the proof above. Use it
only when DNS cannot be changed and you have verified ownership through another
channel. Publishers cannot self-approve manual verification. Disabling a
publisher makes its sites dark even if their DNS records still validate.

## Click integrity

Click URLs contain only an HMAC-signed token: campaign id, issue timestamp, and
random nonce, signed with a key derived from `SECRET_KEY` and compared in
constant time. The browser never supplies a destination.

On every click the API re-checks signature, TTL (24 hours), site status,
placement status, campaign status, schedule, destination safety, and origin. A
paused, expired, deleted, or reconfigured campaign therefore cannot be clicked
through an old link; it returns `410`. There is no open-redirect surface.

For affiliate campaigns, `/r/` additionally re-checks that the offer still
exists, belongs to the site's publisher, is active, is not rejected, and still
passes affiliate URL validation. The redirect is the exact stored affiliate URL.
Query parameters such as `?next=https://evil.example` are ignored. Affiliate
clicks record both a `click` and an `outbound_redirect` event; these are observed
redirect counts, not proof of a sale or commission.

**Replay:** a signed click link can be followed more than once and each
successful redirect counts a click. Rate limits (240/min/IP) bound abuse but do
not deduplicate clicks. If you need unique-click accounting, add a server-side
dedup key or treat click counts as redirect counts; do not trust them as unique
users.

Click responses set `Referrer-Policy: no-referrer` so the publisher URL is not
sent to the advertiser or affiliate merchant. The API records the page origin and
a minimized referrer for analytics.

## Event data minimization

Impression events store:

- event type, site/placement/campaign ids;
- ad source type and affiliate offer id when applicable;
- server-generated timestamp;
- page origin (scheme + host + port, no path);
- referrer with query and fragment removed, truncated to 300 characters;
- coarse user-agent category (`bot`, `mobile`, `desktop`);
- `HMAC-SHA256(SECRET_KEY, IP)` truncated to 16 hex characters (64 bits).

No raw IP address, cookie, device fingerprint, or persistent visitor id is
stored. `event_id` is a per-impression dedup key, not a user identifier. The IP
HMAC is only useful for coarse abuse analysis while `SECRET_KEY` is stable; it
is not reversible without the key, but it is linkable across events from the
same IP. Document this in your privacy notice if required.

Retention defaults to 180 days (`EVENT_RETENTION_DAYS`, range 1–3650). The
maintenance thread purges older events hourly. Reducing retention does not
rewrite existing backups; expire off-host backups on a matching schedule.

Deleting entities with analytics history is blocked or converted to an archive
so reports remain consistent. Full deletion means stopping the service and
removing the database and backups (see
[backup-restore.md](backup-restore.md#retention-and-deletion)).

### Partner inquiry data

The public publisher/client application form stores the name, email, company,
website, message, status, and timestamps supplied by the applicant. This is
personal data and is deliberately kept separate from advertising and learning
account data:

- The endpoint is unauthenticated but rate-limited, body-capped, and validated.
  A hidden `fax` field silently discards bot submissions.
- A submission never creates a user, publisher, site, campaign, or session.
- Only administrators can list or change the status. The applicant has no
  account and cannot read or edit the record.
- Applications are not removed by `EVENT_RETENTION_DAYS`; delete reviewed or
  rejected applications according to the deployment’s privacy policy, and apply
  the same retention to backups.
- Do not export inquiries to third-party marketing tools without a lawful basis.
  The message field is rendered as text in the dashboard, never as HTML.

### Reporting provenance and tenant isolation

`impressions`, `clicks`, `outbound_redirects`, and `affiliate_clicks` are
AdControl-observed metrics. `confirmed_conversions` and `confirmed_commission`
are always zero with provenance `not_connected`; the product does not connect to
Shopee or any other conversion source. Do not infer a sale, revenue, or commission
from an outbound redirect.

Every analytics query, list endpoint, and CSV export accepts a publisher scope
when the caller is not an administrator. The scope is derived from the session,
not from a query parameter. A non-admin request for another tenant's site,
placement, or campaign returns `404`. Administrators may filter by publisher.
Offer reads, placement, and campaign mutations apply the same ownership checks.
Review these paths after adding any endpoint that joins campaign or event data.

## Abuse resistance

| Control | Value |
| --- | --- |
| Overall request body limit | 256 KiB (API server), 64 KiB advertising admin JSON |
| Partner inquiry submission | 20 per 10 min per origin bucket; 16 KiB body |
| Advertising event payload | 8 KiB |
| Login | 10 per 5 min per IP |
| Advertising config | 240 per min per IP + site key |
| Advertising events | 600 per min per IP + site key |
| Advertising clicks | 240 per min per IP |
| Learning registration | 8 per 5 min per IP |
| Learning data export | 5 per 5 min per user |
| Learning enrollment / quiz attempt | 30 per min per user |
| Learning progress / authoring | 120 per min per user |
| Learning course-list reads | 300 per min per IP |
| Learning course/quiz authoring body | Up to 256 KiB parsed body; content byte caps enforced per field |
| SQL | prepared statements throughout; no string interpolation of user values |
| JSON parsing | caught; type-checked field reads |
| Unknown routes | JSON error handler; no stack traces |

Rate buckets live in process memory and reset on restart. An attacker using many
source IPs can exceed them. The API trusts `X-Forwarded-For` only when
`TRUST_PROXY=true`; the bundled nginx **overwrites** the header with
`$remote_addr` so clients cannot choose their bucket. With Cloudflare Tunnel,
that direct peer is the local connector, so the origin buckets are shared by all
visitors and are not per-internet-client. Enforce per-client limits in Cloudflare
WAF, especially for `/api/v1/auth/login`. Never expose the API port directly with
`TRUST_PROXY=true`, and never bind the web port publicly.

Login rate limiting is per source IP and shared by failures and successes. A
successful test suite or repeated automation can fill the bucket; restart the API
or wait five minutes. This is availability, not a bypass.

## Transport and browser headers

TLS is terminated at the Cloudflare edge. A host-installed `cloudflared`
connector maintains an outbound-only encrypted tunnel and forwards plain HTTP to
the loopback-bound nginx port. No inbound 80/443 listener or origin certificate
is required. Do not expose the dashboard or admin API over plain HTTP; bearer
tokens and passwords would cross the network in cleartext. See
[operations.md](operations.md).

nginx adds to dashboard, demo, SDK, and proxied API responses:

```http
Content-Security-Policy: default-src 'self'; script-src 'self' https://static.cloudflareinsights.com; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:; font-src 'self' data:; connect-src 'self' https://cloudflareinsights.com; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'; frame-src https://www.youtube-nocookie.com https://player.vimeo.com
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Referrer-Policy: strict-origin-when-cross-origin
Permissions-Policy: camera=(), microphone=(), geolocation=(), payment=(), usb=()
```

- Inline **scripts** are not allowed. Inline **styles** are allowed for the
  React apps' styling; the public SDK does not need `'unsafe-inline'` and
  publisher pages can use a stricter policy (see
  [integration.md](integration.md#content-security-policy)).
- The only third-party script origin is
  `https://static.cloudflareinsights.com`, because Cloudflare Web Analytics
  injects its beacon into HTML at the edge; `https://cloudflareinsights.com` is
  allowed in `connect-src` for that beacon's RUM `POST`. The beacon is loaded
  with a pinned SRI hash. This is a deliberate, narrow exception to `'self'`.
  Remove both sources from `deploy/nginx/default.conf` if Web Analytics is
  disabled, and re-add them before enabling it again.
- `frame-src` is restricted to the approved privacy-enhanced YouTube and Vimeo
  players. No arbitrary iframes are permitted on learning pages.
- `frame-ancestors 'none'` plus `X-Frame-Options: DENY` prevent clickjacking.
- `/click/` and `/r/` responses override the referrer policy to `no-referrer`
  and set `X-Robots-Tag: noindex, nofollow`.
- `server_tokens off` and header inheritance rules are handled in
  `deploy/nginx/default.conf`; do not add location-level `add_header` directives
  without re-adding the required headers.

HSTS belongs at Cloudflare, not in the container. Enable
`Strict-Transport-Security` with `max-age=31536000` only after HTTPS works for
every hostname it covers. Add `includeSubDomains` only when every subdomain is
HTTPS-only, and avoid `preload` unless you have verified the entire zone. Also
enable **Always Use HTTPS** at Cloudflare.

## CORS

- `/delivery/v1/*` and `/sdk/v1/*`: `Access-Control-Allow-Origin: *`, no
  credentials, preflight cached for 24 hours. Data is public by design.
- Admin `/api/v1/*` responses carry no `Access-Control-Allow-Origin`, so
  cross-origin JavaScript cannot read them even with a stolen token; the shared
  preflight handler does not allow the `Authorization` header. Keep the dashboard
  on the same origin as the API (the bundled nginx does this).

## Audit logging

The following actions write an append-only audit row with actor, entity, timestamp,
and a JSON details payload: login, password change, publisher create/update,
site create/update/delete, verification, key rotation, pause-all, placement
create/update/delete/archive, campaign create/update/publish/pause/delete/
archive, affiliate-offer create/update/validate/publish/pause/place/delete,
learning staff creation, learner registration, learner enrollment, course
create/update/status transitions, module/lesson create/archive, resource
create/delete, quiz replace/delete, learner self-deletion, and partner-inquiry
receipt/status changes. In-place module
and lesson field edits are not separately audited in this MVP; published
curriculum snapshots provide a version record. Impressions, clicks, and outbound
redirects are analytical events, not audit events.

The audit log is administrator-only in the advertising dashboard. It is not
signed or replicated; protect it through database backups and restrict dashboard
access.

## Host and container hardening

- The API runs as `${ADCONTROL_UID}:${ADCONTROL_GID}` (default 1000:1000), not
  root, with `cap_drop: [ALL]`, `no-new-privileges`, and a read-only root
  filesystem. `/tmp`, nginx cache/run paths, and `/data` are writable mounts.
- The web container is read-only with `no-new-privileges` and tmpfs scratch
  paths.
- Compose binds the web port to `127.0.0.1` by default. Only the local
  `cloudflared` connector should reach it; no inbound firewall port is needed.
- `./data` is owner-only (`chmod 700`); databases and backups are `0600`. The
  service sets `umask(0077)`.
- Containers have log rotation limits (`10m` × 5).

See [operations.md](operations.md) for the Cloudflare Tunnel setup.

## Supply chain

- cpp-httplib and nlohmann/json are vendored headers in the API image.
- Dashboard production dependencies are installed from a lockfile; the SDK has
  only build-time dev dependencies.
- Images build locally from pinned base images (`debian:bookworm`,
  `node:22-alpine`, `nginx:1.27-alpine`); rebuild to pick up OS security updates.
- The SDK asset is served from the same origin as your AdControl deployment.
  Optional SRI is documented in
  [integration.md](integration.md#subresource-integrity-optional).

No automated dependency update or vulnerability scanning is built in. Run
`docker scout`, Trivy, or your usual scanner against the images as part of
maintenance.

## Data at rest and backups

The SQLite database contains password hashes, session tokens, public site keys,
campaigns, event analytics, learning accounts, enrollments, progress, quiz
attempts, authoring revisions, partner-inquiry applications, and audit logs. It
is not encrypted at rest by the application. Use full-disk encryption or an encrypted volume, restrict filesystem
access, and encrypt off-host backup copies. Backup guidance, integrity
verification, and restore drills are in
[backup-restore.md](backup-restore.md). Deleting a learner removes current rows,
but backups and WAL history may retain the data until their retention expires.

## Deployment checklist

- [ ] Cloudflare Tunnel is healthy; `marketing.nevatal.id` routes to it over HTTPS.
- [ ] Cloudflare Web Analytics matches the CSP: enabled with the Insights
      allowlist present, or disabled with the allowlist removed.
- [ ] `deploy/.env` is mode `0600` and excluded from git.
- [ ] `SECRET_KEY` is random, ≥32 characters, unique to this deployment.
- [ ] Seeded admin password is ≥12 characters and changed after first login.
- [ ] `HTTP_BIND=127.0.0.1` (default); the API port is not exposed.
- [ ] Site allowlists contain only origins you control.
- [ ] Publisher-owned sites are verified; manual fallbacks are documented.
- [ ] Publisher accounts are active only for approved partners.
- [ ] Affiliate offers are reviewed operationally; disclosure is visible on
      every product card.
- [ ] Learning staff are approved and provisioned only by an administrator;
      former staff are offboarded with an explicit procedure.
- [ ] Published courses have been editorially reviewed; lessons and resources
      contain no unsafe HTML, executable downloads, or live production changes.
- [ ] Video embeds use only the allowlisted YouTube/Vimeo providers, and the
      sitemap/robots checks show only intended public content.
- [ ] `PUBLIC_BASE_URL` is an HTTPS origin with no path or trailing slash.
- [ ] Learning analytics are treated as aggregate; no named-learner reporting is
      added outside the reviewed API.
- [ ] Partner inquiries are reviewed only in `/dashboard#inquiries`, and the
      deployment has a documented retention/deletion practice for them.
- [ ] Dashboard access is restricted at the proxy if required.
- [ ] HSTS enabled only after HTTPS is confirmed.
- [ ] Automatic backups enabled and copied off-host.
- [ ] Retention matches your privacy policy.
- [ ] Proxy/CDN rate limits configured for internet-facing traffic.
- [ ] Restore drill completed and documented.

## Reporting a problem

This is a self-hosted project without a formal security contact. If you deploy it
publicly, add a security contact to your site and keep dependencies,
certificates, and backups current.
