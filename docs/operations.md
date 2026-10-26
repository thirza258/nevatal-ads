# Operations guide

This is the runbook for the production deployment at **https://ads.nevatal.id**.
Public HTTPS is provided by **Cloudflare Tunnel**: a host-installed `cloudflared`
connector dials out to Cloudflare and forwards requests to the loopback-only
Compose web port. No inbound web port or origin TLS certificate is required.

Backup and restore procedures are in [backup-restore.md](backup-restore.md); the
security model is in [security.md](security.md).

## Topology

```
Internet :443
      │
      ▼
Cloudflare edge (TLS, DNS, WAF/rate limits, HSTS)
      │ encrypted outbound-only tunnel
      ▼
cloudflared on the Docker host
      │ http://127.0.0.1:18080
      ▼
web container (nginx)
  ├── /                      React public learning app
  ├── /dashboard             React operations app
  ├── /assets/, /index.html  shared static assets
  ├── /demo/                 static demo
  ├── /sdk/v1/               SDK JS + CSS
  ├── /sitemap.xml, /robots.txt
  └── /api/, /delivery/, /click/ ──► api:8088
                                          │
                                     ./data SQLite
```

The compose stack publishes only `127.0.0.1:${HTTP_PORT:-18080}`. The API is on an
internal Docker network and is not reachable from the host. Cloudflare terminates
TLS at its edge; `cloudflared` speaks plain HTTP to the container over loopback.

## Host requirements

- Linux host with Docker Engine 24+ and Docker Compose v2.
- The `ads.nevatal.id` zone managed by Cloudflare, plus a Cloudflare Tunnel.
- `cloudflared` installed on the Docker host.
- Outbound HTTPS (TCP 443) and, preferably, UDP/TCP 7844 for the tunnel. No
  inbound 80/443 port forwarding is required.
- Enough disk for SQLite growth plus backups. Image hotlinking means AdControl
  stores no binary assets. Plan for 2–5× the database size in `./data/backups`.
- Local SSD/NVMe for `./data`. Do not put SQLite on NFS/SMB.
- `openssl` for secret generation and SRI hashing.

## First deployment

### 1. Cloudflare Tunnel and DNS

Use either a dashboard-managed tunnel or a locally managed tunnel.

#### Dashboard-managed

1. In Cloudflare Zero Trust, open **Networks → Tunnels → Create a tunnel** and
   choose **Cloudflared**.
2. Name it (for example `adcontrol`) and install the connector on this host using
   the displayed command:

   ```bash
   sudo cloudflared service install <TUNNEL_TOKEN>
   ```

   The tunnel token is a secret. Avoid leaving it in shell history or copying it
   into the repository.
3. Add a **Public hostname**:
   - Hostname: `ads.nevatal.id`
   - Service: `http://127.0.0.1:18080`

#### CLI-managed

```bash
cloudflared tunnel login
cloudflared tunnel create adcontrol
cloudflared tunnel route dns adcontrol ads.nevatal.id
cloudflared tunnel list
```

The create command prints a tunnel UUID and writes
`~/.cloudflared/<UUID>.json`. Install the config and credentials on the host:

```bash
sudo install -d -m 700 /etc/cloudflared
sudo install -m 600 ~/.cloudflared/<UUID>.json /etc/cloudflared/<UUID>.json
sudo install -m 644 deploy/cloudflared/config.yml.example /etc/cloudflared/config.yml
sudo sed -i "s/TUNNEL_UUID/<UUID>/g" /etc/cloudflared/config.yml
sudo cloudflared service install
sudo systemctl enable --now cloudflared
```

The template routes `ads.nevatal.id` to `http://127.0.0.1:18080`. If you change
`HTTP_PORT`, update the ingress service and reload the connector.

#### Verify the connector

```bash
sudo systemctl status cloudflared --no-pager
sudo journalctl -u cloudflared -n 50 --no-pager
cloudflared tunnel info adcontrol
```

Look for a registered connector and no repeated reconnect errors. The public
hostname can be tested after the stack is running.

### 2. Environment

```bash
cd /path/to/nevatal-ads
cp deploy/.env.example deploy/.env
chmod 600 deploy/.env
```

Edit `deploy/.env`:

```dotenv
ADCONTROL_ENV=production
SECRET_KEY=<openssl rand -hex 32>
ADMIN_EMAIL=you@example.com
ADMIN_PASSWORD=<at least 12 characters>
HTTP_BIND=127.0.0.1
HTTP_PORT=18080
ADCONTROL_UID=1000
ADCONTROL_GID=1000
EVENT_RETENTION_DAYS=180
BACKUP_INTERVAL_HOURS=24
BACKUP_RETENTION=14
PUBLIC_BASE_URL=https://ads.nevatal.id
```

`PUBLIC_BASE_URL` is the canonical origin for learning SEO (`sitemap.xml`,
`robots.txt`, and canonical metadata). Use an absolute HTTPS origin with no path
or trailing slash. An HTTP or malformed value falls back to
`https://ads.nevatal.id`. It does not change the tunnel ingress; that remains the
loopback `HTTP_PORT`.

Generate values without putting them in shell history if that matters:

```bash
openssl rand -hex 32
openssl rand -base64 24
```

`ADMIN_PASSWORD` is used only when the database has no administrator with that
email. In production, startup fails if seeding is needed and the password is
missing or shorter than 12 characters.

### 3. Data directory

The API runs as `ADCONTROL_UID:ADCONTROL_GID`. The bind mount must be writable by
that user and private to it:

```bash
mkdir -p data
sudo chown "$(id -u):$(id -g)" data
chmod 700 data
```

If you use different UID/GID values, set them in `deploy/.env` and own `data`
accordingly. The container's database and backups are created `0600`/`0700`.

### 4. Start

```bash
docker compose -f deploy/docker-compose.yml up -d --build
docker compose -f deploy/docker-compose.yml ps
curl -fsS http://127.0.0.1:18080/readyz
```

The first build compiles the C++ API, runs the unit tests, and builds the
dashboard and SDK. If the API exits, check:

```bash
docker compose -f deploy/docker-compose.yml logs --tail=100 api
```

Typical first-start failures are a short/missing production password, a missing
`SECRET_KEY`, or a `./data` ownership mismatch.

### 5. Verify public HTTPS

```bash
curl -fsS https://ads.nevatal.id/readyz
curl -fsSI https://ads.nevatal.id/ | head
curl -fsS https://ads.nevatal.id/api/v3/catalog/courses | jq '.courses | length'
curl -fsS https://ads.nevatal.id/robots.txt
curl -fsS https://ads.nevatal.id/sitemap.xml | head -30

# Learning app at /; operations app at /dashboard.
curl -fsS https://ads.nevatal.id/ | grep -q 'AdControl Learning'
curl -fsS https://ads.nevatal.id/dashboard | grep -q 'AdControl'
```

If the sitemap points at the wrong host or an `http://` URL, correct
`PUBLIC_BASE_URL`, recreate the API, and check `/sitemap.xml` again. Do not
"fix" canonical URLs at the Cloudflare cache layer.

If Cloudflare returns **1033**, the tunnel has no connected connector. If it
returns **502**, the tunnel is connected but `127.0.0.1:18080` is not reachable
(check the stack and `HTTP_PORT`). A **404** usually means the public hostname or
ingress rule is missing.

In the Cloudflare dashboard:

- Confirm the DNS record for `ads.nevatal.id` is proxied and routed to the tunnel.
- Enable **Always Use HTTPS**.
- Enable HSTS only after HTTPS works end to end. A sensible start is
  `max-age=31536000` without `includeSubDomains` or `preload` until you have
  verified every subdomain.
- Leave Cloudflare's default caching for API paths alone. Origin responses are
  already marked `no-store`, `max-age=30` (delivery), `max-age=300` (SDK),
  `max-age=120` (public learning catalog), or `max-age=1800` (sitemap/robots).
  Public catalog responses are safe to cache briefly; learner, authoring, and
  admin responses must remain `no-store`.
- Do not enable features that rewrite or defer the SDK script unless you have
  tested publisher pages with them. Do not HTML-rewrite learning routes if it
  changes canonical metadata.

### 6. Cloudflare WAF rate limits

The container nginx overwrites `X-Forwarded-For` with its direct peer so clients
cannot choose their rate-limit bucket. Behind Cloudflare Tunnel, that direct peer
is the local connector, which means **all visitors share the API's in-process
buckets**. The built-in limits still bound total load, but they are not
per-internet-client.

Add edge rate limiting at Cloudflare, at minimum for sign-in and registration:

- Path: `/api/v1/auth/login`
- Rate: 10 requests per minute per IP (tune to your needs)
- Action: block or managed challenge, with a short mitigation timeout

Consider a similar rule for `/api/v3/auth/register` (public account creation).
Also consider `/delivery/v1/events`, `/delivery/v1/sites/*/config`,
`/click/*`, and public `/api/v3/catalog/courses` if abuse is observed. Do not set
`HTTP_BIND=0.0.0.0` and do not make nginx trust arbitrary client-supplied
forwarding headers.

### 7. Firewall

No inbound web port should be open:

```bash
sudo ss -ltnp | grep -E ':(80|443|18080)\b'
# 127.0.0.1:18080 is correct; 0.0.0.0:18080 or a public 80/443 is not.
```

Allow the host to make outbound connections to Cloudflare on TCP 443 and
UDP/TCP 7844. If you use a host firewall with a default-deny outbound policy,
add those rules; otherwise the tunnel cannot establish.

### 8. First login and hardening

1. Open `https://ads.nevatal.id/` and sign in with `ADMIN_EMAIL` /
   `ADMIN_PASSWORD`.
2. Change the password in **Settings**. This invalidates all sessions.
3. Remove or blank `ADMIN_PASSWORD` in `deploy/.env` after the password change if
   you do not want the seed value on disk. Keep `ADMIN_EMAIL`; seeding is skipped
   once the account exists.
4. Add your publisher websites and restrict each allowlist to origins you own.
5. Create placements and campaigns; publish only when ready.
6. Verify backups and copy one off-host.

### 9. Publisher and affiliate onboarding

This step applies when external publisher partners will use the platform.

1. As an administrator, open **Publishers → Add publisher**. Supply the owner
   email, a strong initial password, and the correct publisher status. Share the
   credentials out of band. Publisher accounts are invite-only.
2. The publisher signs in and creates its owned site. The site is dark in
   delivery until verification succeeds and monetization is enabled.
3. Open the site's verification challenge. At the publisher's DNS provider,
   create the shown record (the name and value must match exactly):

   ```
   _adcontrol-challenge.example.com. 300 IN TXT "adcontrol-site-verification=<token>"
   ```

   Confirm propagation from the AdControl host with:

   ```bash
   dig +short TXT _adcontrol-challenge.example.com
   ```

   Then choose **Verify with DNS TXT**. The API checks DNS only; it does not make
   an HTTP request to the publisher site. If DNS cannot be changed, an
   administrator can use **manual verification** as an explicit out-of-band
   attestation.
4. A primary-domain change resets verification and monetization. Repeat the DNS
   step for the new domain.
5. Affiliate offers are entered manually. Paste the exact Shopee affiliate URL
   and product metadata, validate it structurally, publish it, and place it in a
   `product_card` placement. Validation does **not** fetch the URL, check price
   or stock, or confirm commissions.
6. Before approving a publisher, inspect its site allowlist, disclosure text,
   offer destinations, and publisher status. Disable the publisher or the site
   immediately if its content or links are unsafe.
7. Confirm product cards show a visible affiliate disclosure and that outbound
   traffic goes through `/r/`. `confirmed_conversions` and
   `confirmed_commission` remain zero because no Shopee conversion integration
   exists; do not present observed clicks as sales.

The new publisher, verification, offer, campaign source, and event provenance
tables are part of the same SQLite database and are covered by the existing
backup/restore process. Backups still contain sensitive site keys and publisher
account hashes; protect them accordingly.

### 10. Learning operations

The public learning app is at `/`; the advertising dashboard remains at
`/dashboard`. The launch catalog (eight topics, eight courses, 32 lessons, four
paths) is seeded once on startup. The seed state prevents duplicate inserts and
does not overwrite operator edits after the first run. Existing users are not
enrolled automatically.

1. **Provision staff.** Sign in as an administrator and open `/author/team` (or
   `POST /api/v3/admin/learning/users`) to create `instructor` or `editor`
   accounts. Share initial passwords out of band. Instructors can manage only
   their own courses; editors can review and manage all courses. There is no
   public staff signup. The MVP has no staff-removal endpoint, so define and
   record an explicit offboarding procedure before creating accounts.
2. **Author and review.** Instructors create drafts, add modules/lessons/
   resources/quizzes, and submit for review. Editors approve and publish. Both
   approval and publication require every active module to contain an active
   lesson with non-empty content. Published courses are versioned with an
   immutable snapshot.
3. **Review freshness.** `next_review_due_at` is 180 days after the latest
   publication. Track overdue courses and unpublish rather than leaving
   inaccurate or unsafe material live. Re-publishing after review refreshes the
   date and creates the next version.
4. **Check content safety.** Course Markdown must be non-executable. Do not
   attempt to embed raw HTML, arbitrary iframes, scripts, or unsafe URLs.
   Video is limited to YouTube/Vimeo IDs; resources must be HTTPS links that
   AdControl does not fetch. Exercises may reference the sandbox but must never
   instruct automatic production changes.
5. **Verify SEO.** Set `PUBLIC_BASE_URL=https://ads.nevatal.id` in `deploy/.env`,
   recreate the API, and confirm that `/sitemap.xml` and `/robots.txt` use that
   origin and that only published topics/courses/paths are listed. The sitemap
   and public catalog APIs are cached for 30 and 2 minutes respectively.
6. **Protect learner data.** Learners can export their own data and self-delete
   learner accounts from `/my-learning`; staff cannot self-delete. Learning
   accounts, enrollments, progress, quiz attempts, revisions, and audit rows are
   **not** removed by `EVENT_RETENTION_DAYS`, which applies to advertising
   events only. Backups include all of it, so apply your privacy retention
   policy to backups as well.
7. **Report honestly.** Author and admin learning analytics are aggregate-only.
   Do not add named-learner reporting without a privacy review. There are no
   paid courses, certificates, cohorts, or commissions in Phase 3.

For the full behavior and route list, see [learning.md](learning.md).

## Monitoring

### Health

```bash
curl -fsS http://127.0.0.1:18080/healthz   # process alive
curl -fsS http://127.0.0.1:18080/readyz    # database reachable
curl -fsS https://ads.nevatal.id/readyz    # public path through Cloudflare
docker compose -f deploy/docker-compose.yml ps
sudo systemctl status cloudflared --no-pager
```

Both containers have Docker health checks. The web check fetches `/`; the API
check fetches `/readyz` inside the container.

### Logs

```bash
docker compose -f deploy/docker-compose.yml logs -f --tail=100 api web
sudo journalctl -u cloudflared -f
```

The API logs startup configuration (without secrets), backup/retention events,
and unhandled exceptions. Container logs are capped at 10 MB × 5 files per
service. Ship them to your log system if you need long retention; audit data is
in the database, not in logs. Cloudflare dashboard analytics cover edge traffic
and WAF actions.

### What to alert on

- `/readyz` non-200 locally or publicly for more than a minute.
- API container restart loops or `[Fatal]` lines.
- `cloudflared` inactive, reconnecting, or reporting no registered connectors.
- Cloudflare 1033/502/522 responses for `ads.nevatal.id`.
- Disk usage above 80% on the `./data` filesystem.
- Backup directory has no file newer than ~2× `BACKUP_INTERVAL_HOURS`.
- A sustained `429` rate at Cloudflare or in access logs.
- Login failures or audit anomalies in the dashboard.
- Public learning catalog or authoring routes returning sustained 5xx errors.
- A sitemap origin that does not match `PUBLIC_BASE_URL`.
- Database integrity check failure.

### Backup freshness

```bash
ls -lt data/backups | head
docker compose -f deploy/docker-compose.yml exec -T api \
  sqlite3 /data/adcontrol.db 'PRAGMA integrity_check;'
```

See [backup-restore.md](backup-restore.md) for off-host copies, retention, and
quarterly restore drills.

## Upgrades

The database schema is created or extended with idempotent `CREATE TABLE/INDEX
IF NOT EXISTS` statements at startup. There is no versioned down-migration and
no automatic schema rollback. Back up before every upgrade.

1. **Back up and tag the current images.**

   ```bash
   ./scripts/backup.sh
   docker tag adcontrol-api:latest adcontrol-api:pre-$(date -u +%Y%m%d)
   docker tag adcontrol-web:latest adcontrol-web:pre-$(date -u +%Y%m%d)
   ```

2. **Fetch and build.**

   ```bash
   git pull                     # or copy the new release into place
   docker compose -f deploy/docker-compose.yml build --pull
   ```

   The API build runs the C++ unit tests; a failed build leaves the running
   containers untouched.

3. **Recreate.**

   ```bash
   docker compose -f deploy/docker-compose.yml up -d
   curl -fsS http://127.0.0.1:18080/readyz
   curl -fsS https://ads.nevatal.id/readyz
   ```

4. **Smoke test** the dashboard login, a delivery config, a qualified
   impression, and a click. Also load `/`, `/courses`, a course detail page,
   `/my-learning` (signed out), `/dashboard`, `/sitemap.xml`, and `/robots.txt`.
   Run `bash scripts/test-stack.sh` against a staging stack if you want the full
   suite (it creates a test site and exhausts the login rate-limit bucket;
   restart the API afterwards).

5. **Invalidate stale browser state if the SDK changed.** The SDK JS is cached
   for five minutes. Reload publisher pages after that window, or give the
   release notes to publishers. If you use SRI, update and deploy hashes in the
   same window.

`cloudflared` and DNS do not need to change for an application upgrade.

### Rollback

If the new version fails and the database is unchanged:

```bash
docker tag adcontrol-api:pre-YYYYMMDD adcontrol-api:latest
docker tag adcontrol-web:pre-YYYYMMDD adcontrol-web:latest
docker compose -f deploy/docker-compose.yml up -d --force-recreate
```

Because the compose services build with the `:latest` image names, retagging the
previous images restores them. If a schema migration or data change is
incompatible, restore the pre-upgrade backup with `scripts/restore.sh`; that
replaces writes made since the backup.

No blue/green deployment is provided. Expect a few seconds of downtime while the
API container restarts; the web health check waits for API health before
starting. SDK clients keep their last good configuration for up to five minutes,
then clear ads during an outage.

## Routine maintenance

| Interval | Task |
| --- | --- |
| Daily | Check health, tunnel status, disk, and backup freshness; review unexpected admin/audit activity |
| Weekly | Review container and cloudflared logs; check Cloudflare WAF/analytics for anomalies; review new publishers, unverified sites, paused offers, and learning courses due for review |
| Monthly | Apply OS/container/cloudflared updates on a maintenance window; verify off-host backups; sample affiliate URLs and disclosures; sample courses, quizzes, video embeds, and resource links |
| Quarterly | Restore a backup into staging; review allowlists, publisher statuses, offers, paused/expired campaigns, learning staff access, content freshness, learner-data retention, and sitemap contents |
| On staff change | Change the admin password (ends sessions); rotate `SECRET_KEY` if click-token integrity is in doubt; review publisher access |

### Certificate and tunnel maintenance

Cloudflare manages the public certificate and renews it automatically. `cloudflared`
updates through the OS package manager or its self-update service, depending on
how it was installed. After updating, confirm the tunnel remains connected and
`https://ads.nevatal.id/readyz` still works.

If tunnel credentials or a connector token may have leaked, rotate them in the
Cloudflare dashboard, reinstall the connector, and remove the old credentials
from the host. TLS certificates for the public hostname are never stored in this
repository.

### Rotating the site key

**Websites → Rotate key** invalidates the old public key immediately. Update the
publisher snippet, then expect existing pages to clear ads until they reload.
There is no overlap window; rotate during low traffic or accept a short gap.

### Rotating `SECRET_KEY`

Stopping and restarting with a new `SECRET_KEY` invalidates all outstanding click
tokens and breaks IP-hash correlation. It does **not** log out admins. Do it if
the key may have leaked:

```bash
NEW_SECRET=$(openssl rand -hex 32)
sed -i "s|^SECRET_KEY=.*|SECRET_KEY=$NEW_SECRET|" deploy/.env
docker compose -f deploy/docker-compose.yml up -d --force-recreate api
```

Existing pages recover by fetching new click links within about 30 seconds; click
links in tabs older than the rotation return `400`. Rotate the admin password as
well if you suspect account compromise.

### Changing the public port

The container always listens on port 80; `HTTP_PORT` changes the host-side
loopback port. If `18080` is already in use, pick another unprivileged port (for
example `18081`):

```bash
# deploy/.env
HTTP_PORT=18081
```

Then update the tunnel ingress to `http://127.0.0.1:18081`, recreate the web
service, and restart cloudflared:

```bash
docker compose -f deploy/docker-compose.yml up -d --force-recreate web
sudo cloudflared tunnel --config /etc/cloudflared/config.yml ingress validate
sudo systemctl restart cloudflared
curl -fsS https://ads.nevatal.id/readyz
```

Do not change the mapping to a public interface.

## Scaling and capacity

- **Single host, single API process.** SQLite WAL allows concurrent readers and
  one writer. Do not scale the API service to multiple replicas against the same
  `./data` volume.
- The event table is the main advertising growth driver. `EVENT_RETENTION_DAYS`
  bounds it; lower the value and allow an hourly purge if disk grows
  unexpectedly. Purges do not shrink the file immediately; `VACUUM` is not
  automated. Learning accounts, enrollments, progress, quiz attempts, revisions,
  and audit rows are not purged by that setting and should be included in growth
  and retention planning.
- The delivery config is computed per request and cached by the browser for 30
  seconds. Static dashboard assets are immutable. For very high traffic, use
  Cloudflare caching for `/sdk/` and `/delivery/` while respecting `max-age=30`.
- Analytics queries aggregate the full requested range. CSV export loads all
  matching events into memory; keep date ranges bounded on large datasets.
- Rate-limit state is in memory and per process; there is no shared store.

If you outgrow one host, the correct next step is a managed database with a
different storage layer, not multiple SQLite writers.

## Disaster recovery

### Host lost, backups available

1. Provision a new host with Docker and `cloudflared`, and install the tunnel
   connector with the same tunnel credentials (or create a new tunnel and update
   the `ads.nevatal.id` DNS route).
2. Copy the repository and the latest verified backup (plus `deploy/.env` from a
   secure secret store) to the host.
3. Create `./data`, set ownership/mode, and restore:
   `./scripts/restore.sh <backup.db.gz> --yes`.
4. Rebuild and start the stack and verify `/readyz` locally and publicly.
5. Rotate `SECRET_KEY` and the admin password if the old host's security is in
   doubt. Reissue site keys only if they may have leaked.

Because DNS points at the tunnel rather than a fixed origin IP, no DNS change is
needed when reusing the tunnel on a new host. Stop the old connector first or
ensure only one connector is serving this production tunnel.

### Database corrupted

1. Stop writes (`docker compose ... stop api`).
2. Preserve the damaged database and WAL files for forensics.
3. Restore the newest backup that passes `PRAGMA integrity_check`.
4. Verify events after the backup timestamp are lost; communicate if analytics
   accuracy matters.
5. Rebuild from a fresh database only as a last resort; this loses all sites and
   campaigns.

### Tunnel or Cloudflare failure

The stack and database can remain running. If Cloudflare or the tunnel is
unavailable, visitors see an edge error rather than stale ads, and host pages
that already loaded keep working. Publisher pages that cannot load the SDK fail
open (no ads). Check `systemctl status cloudflared`, the Cloudflare tunnel
dashboard, and outbound connectivity before restarting application containers.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Cloudflare **1033** | Tunnel has no connected connector: `systemctl status cloudflared`, `journalctl -u cloudflared` |
| Cloudflare **502** | Tunnel is up but origin is unreachable: stack health and `HTTP_PORT`/ingress port match |
| Public hostname **404** | Tunnel public hostname or DNS route is missing for `ads.nevatal.id` |
| Public DNS **NXDOMAIN** | No DNS record exists. Add the tunnel **Public hostname** in Zero Trust (dashboard-managed), or run `cloudflared tunnel route dns <tunnel> ads.nevatal.id` (CLI-managed); confirm the zone uses Cloudflare nameservers |
| Local `readyz` fails | `docker compose ps`; API logs; data permissions |
| `/` shows learning, not the ad dashboard | Intended: the operations dashboard is `/dashboard`. Legacy hashes such as `/#campaigns` still open it. |
| Dashboard loads but login fails | API logs; correct `ADMIN_EMAIL`; rate limit (429 → wait/restart API) |
| Login returns `429` | Cloudflare WAF rule or shared origin bucket; adjust edge limits or wait |
| Registration returns `429` | Public learning registration limit (8 per IP/5 min) or edge rule; wait or adjust WAF |
| Sitemap uses the wrong origin or `http://` | Set `PUBLIC_BASE_URL=https://ads.nevatal.id` with no path/trailing slash; recreate the API |
| Published course is missing from catalog/sitemap | Course or its topic is not `published`; check approval/publication, readiness, and the 30-minute sitemap cache |
| Course detail shows locked lessons | For anonymous visitors, only `is_preview` lessons are public. Enrollment is required for the rest. |
| Authoring returns `403` | Caller is an instructor who does not own the course, or lacks the editor/admin role |
| Authoring edit returns `409` | Content is only editable in `draft`; the owner can withdraw `in_review`, while `approved`/`published`/`archived` requires an editor/admin |
| Publish returns `400` | Readiness check failed: every active module needs an active lesson and every active lesson needs content |
| Video embed is blocked | Use only `youtube`/`vimeo` with a valid ID; CSP permits only those two frame origins |
| Delivery returns `403` | Page origin missing from the site allowlist; include port if non-default |
| Delivery returns `404` | Site disabled or key rotated |
| Ad renders but no impressions | Below 50% visible for 1 s; `IntersectionObserver` unavailable; background tab |
| Click returns `410` | Campaign paused/expired, schedule ended, site/placement disabled, or token >24 h old |
| Clicks all return `403` | Click request origin is not in the allowlist; check that the page's `Origin` is listed |
| Ads disappear after an upgrade | SDK JS cached/stale, SRI hash mismatch, or CSP blocked the new asset; check browser console |
| Browser console shows a CSP error for styles | `style-src` must allow `https://ads.nevatal.id` (publisher CSP) |
| `SQLITE_BUSY` / API 500s | Another process opened the database; ensure only one API writer and local disk |
| Disk filling | Event retention, learning data, backup count, container logs; inspect `du -sh data/*` |
| Need to remove a learning staff account | No API/UI endpoint exists in this MVP. Use a reviewed database operation against the staff user and its sessions; never delete the shared `admin` account casually |
| `[Fatal] SECRET_KEY...` | Production requires a ≥32-character key; set it in `deploy/.env` |
| `[Fatal] ... ADMIN_PASSWORD` | Fresh production database needs a ≥12-character seed password |
| Permission denied on `/data` | `./data` owner must match `ADCONTROL_UID:GID`; check `id` and `ls -ln data` |

## Deployment checklist

- [ ] Cloudflare Tunnel is healthy and `ads.nevatal.id` is routed to it.
- [ ] Tunnel ingress targets `http://127.0.0.1:18080` (or the configured `HTTP_PORT`).
- [ ] No inbound 80/443/18080 is open; only loopback binds the web port.
- [ ] `https://ads.nevatal.id/readyz` returns ready.
- [ ] Always Use HTTPS and HSTS are enabled at Cloudflare.
- [ ] A Cloudflare WAF rate limit protects `/api/v1/auth/login`.
- [ ] A Cloudflare WAF rate limit protects `/api/v3/auth/register`.
- [ ] `deploy/.env` mode `0600`; `SECRET_KEY` unique and ≥32 chars.
- [ ] `HTTP_BIND=127.0.0.1`; `ss -ltnp` confirms no public web port.
- [ ] Admin password changed; first backup copied off-host.
- [ ] Site allowlists contain only owned origins.
- [ ] Publisher accounts are approved and active; owned sites are verified and
      monetization-enabled.
- [ ] Affiliate offers are reviewed, active, and paired with visible
      product-card disclosures.
- [ ] `PUBLIC_BASE_URL=https://ads.nevatal.id`; `/sitemap.xml` and
      `/robots.txt` return the public origin, not `http://` or a container host.
- [ ] `/` loads the public learning app and `/dashboard` loads operations; the
      launch catalog has published courses.
- [ ] Learning staff accounts are limited to approved people with distinct
      credentials; initial passwords were delivered out of band.
- [ ] Courses due for 180-day review are assigned an owner, and sample lessons,
      quizzes, videos, and resources were checked for safety and accuracy.
- [ ] Retention and backup settings match policy, including learning data.
- [ ] Monitoring covers `/readyz`, disk, backups, tunnel status, and WAF blocks.
- [ ] Restore drill completed.
