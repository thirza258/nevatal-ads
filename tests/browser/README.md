# Browser suites

These Playwright suites exercise the shipped SDK, dashboard delivery path, and
public learning app in a real Chromium browser. They complement the API and stack
suites, which cannot verify IntersectionObserver visibility rules, the SPA
mutation scanner, or browser CSP and layout behavior.

| Suite | Checks |
| --- | --- |
| `sdk-e2e.js` | Demo rendering, signed click URLs, safe link attributes, Ad label, no cookies or storage, 50%/1 s impression qualification, offscreen suppression, SPA-inserted placements, refresh deduplication, pause propagation, blank/hide fallbacks, no CSP violations |
| `csp-strict.js` | A host page served with `default-src 'self'; script-src 'self'; style-src 'self'; ...` still renders the ad, loads the SDK stylesheet, applies CSSOM dimensions, keeps the Ad label, and produces no CSP violations or page errors |
| `learning-smoke.js` | The public learning app lazy-loads and renders, catalog and course routes get titles and canonical URLs, phone-width layouts do not overflow, and the dashboard entry still loads without console or CSP errors. |

## Safety

`sdk-e2e.js` and `csp-strict.js` authenticate as the administrator, create or
modify placements and campaigns, and write analytics events.
`learning-smoke.js` is read-only: it browses only public learning routes and does
not sign in or mutate data. Because the first two suites change live data, run
all three against a local or staging stack only. Never point them at
**https://ads.nevatal.id** production.

## Run

1. Start the stack and confirm it is healthy:

   ```bash
   docker compose -f deploy/docker-compose.yml up -d
   curl -fsS http://127.0.0.1:18080/readyz
   ```

2. Seed the test site, placement, and campaign. This writes site/campaign IDs to
   `/tmp/adcontrol_stack_ids` (mode `0600`):

   ```bash
   bash scripts/test-stack.sh
   ```

3. Restart the API. The stack suite deliberately exhausts the login rate-limit
   bucket at the end; restarting clears that in-memory state. It also changes the
   delivery ETag nonce, so the browser suites fetch fresh click links:

   ```bash
   docker compose -f deploy/docker-compose.yml restart api
   ```

4. Run all browser suites:

   ```bash
   bash tests/browser/run.sh
   ```

The runner sources `deploy/.env`, verifies `/readyz` and the IDs file, then runs
all three suites inside the pinned Playwright image with host networking. It mounts
only the test directory and the IDs file read-only. The container receives the
admin email and password it needs, but not `SECRET_KEY`; the rest of the
repository, the database, and `deploy/.env` are not mounted. The expected result:

```text
=== SDK end-to-end suite ===
...
RESULT: 40 passed, 0 failed

=== Strict-CSP suite ===
...
RESULT: 8 passed, 0 failed

=== Public learning smoke suite ===
...
RESULT: 11 passed, 0 failed

Browser suites: sdk-e2e=0 csp-strict=0 learning=0 (0 = pass)
```

`run.sh` runs every suite even if an earlier one fails, so one failure does not
hide the others. The process exits non-zero if any suite fails.

## Test data changes

- `sdk-e2e.js` creates uniquely named placements and a campaign on the seeded
  test site, then **pauses** the seeded `header-banner` campaign to prove that a
  paused creative disappears from an open page.
- `csp-strict.js` **republishes** that header campaign before checking rendering
  under a strict policy. After a full `run.sh` pass, the seeded campaign is
  active again.
- Impressions and clicks from the SDK and strict-CSP suites remain in the test
  site's analytics.
- `learning-smoke.js` leaves no test data behind; it is public and read-only.

## Configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `ADCONTROL_TEST_PORT` | `HTTP_PORT` from `deploy/.env` (then `18080`) | Web port |
| `ADCONTROL_BASE` | `http://127.0.0.1:<port>` | Full base URL override |
| `ADCONTROL_TEST_IDS` | `/tmp/adcontrol_stack_ids` | IDs file from `scripts/test-stack.sh` |
| `ADCONTROL_PLAYWRIGHT_IMAGE` | `mcr.microsoft.com/playwright:v1.64.1-noble` | Browser image |
| `ADCONTROL_ENV_FILE` | `<repo>/deploy/.env` | Admin credentials for direct `node` runs |
| `ADMIN_EMAIL`, `ADMIN_PASSWORD` | from `deploy/.env` | Override the env file; the Docker runner passes these |

The image tag matches the pinned `playwright` dependency in `package.json`.
Upgrade both together. Host networking is required on Linux so Chromium reaches
the loopback-only web port and uses an origin that is in the site allowlist.

To run a suite directly with a local Node install instead of Docker:

```bash
cd tests/browser
npm ci
ADCONTROL_ROOT="$(cd ../.. && pwd)" node sdk-e2e.js
ADCONTROL_ROOT="$(cd ../.. && pwd)" node csp-strict.js
ADCONTROL_ROOT="$(cd ../.. && pwd)" node learning-smoke.js
```

Direct runs use the same `deploy/.env` and IDs file. They require Playwright's
browser binaries (`npx playwright install chromium`) and a reachable stack.
