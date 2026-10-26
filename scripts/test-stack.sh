#!/usr/bin/env bash
#
# Integration suite for the running Compose stack (nginx + API + dashboard).
# Creates a fresh site/campaign each run and does not require a clean database.
#
# Usage: bash scripts/test-stack.sh
# Requires: a running stack, curl, jq, and deploy/.env.
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
COMPOSE=(docker compose -f "$ROOT/deploy/docker-compose.yml")

set -a
# shellcheck disable=SC1091
source "$ROOT/deploy/.env"
set +a

PORT="${ADCONTROL_TEST_PORT:-${HTTP_PORT:-18080}}"
BASE="http://127.0.0.1:${PORT}"
ORIGIN="127.0.0.1:${PORT}"
ALT_ORIGIN="localhost:${PORT}"
RUN_ID="$(date -u +%Y%m%d%H%M%S)-$$"
IDS_FILE="${ADCONTROL_TEST_IDS:-/tmp/adcontrol_stack_ids}"
PASS=0
FAIL=0

check() {
  local label="$1" condition="$2"
  if [ "$condition" = "true" ] || [ "$condition" = "1" ]; then
    PASS=$((PASS + 1)); echo "PASS  $label"
  else
    FAIL=$((FAIL + 1)); echo "FAIL  $label"
  fi
}

headers() { curl -sS -D - -o /dev/null "$@"; }
code() { curl -sS -o /dev/null -w '%{http_code}' "$@"; }
header_value() { tr -d '\r' | awk -F': ' -v key="$1" 'tolower($1)==tolower(key){print $2; exit}'; }
header_count() { tr -d '\r' | grep -ci "^$1:" || true; }

wait_web=0
for _ in $(seq 1 40); do
  if curl -fsS "$BASE/healthz" >/dev/null 2>&1; then wait_web=1; break; fi
  sleep 0.5
done
check "Stack is reachable through nginx" "$wait_web"

# ---- Public learning app, dashboard, and static host ---------------------
PUBLIC_ORIGIN="${PUBLIC_BASE_URL:-https://ads.nevatal.id}"
PUBLIC_ORIGIN="${PUBLIC_ORIGIN%/}"

APP_HEADERS="$(mktemp)"
curl -fsS -D "$APP_HEADERS" -o /tmp/adcontrol_dashboard.html "$BASE/"
check "Public learning HTML served at the root" "$([ -s /tmp/adcontrol_dashboard.html ] && echo true || echo false)"
check "Public learning CSP present" "$(tr -d '\r' < "$APP_HEADERS" | grep -qi '^Content-Security-Policy: default-src' && echo true || echo false)"
check "Public learning denies framing" "$(tr -d '\r' < "$APP_HEADERS" | grep -qi '^X-Frame-Options: DENY' && echo true || echo false)"
check "Public learning is no-store" "$([ "$(header_value cache-control < "$APP_HEADERS")" = "no-store" ] && echo true || echo false)"
check "Public learning has one nosniff header" "$([ "$(header_count x-content-type-options < "$APP_HEADERS")" = "1" ] && echo true || echo false)"
check "CSP allows only approved video embed hosts"   "$(tr -d '\r' < "$APP_HEADERS" | grep -qi "frame-src https://www.youtube-nocookie.com https://player.vimeo.com" && echo true || echo false)"
check "CSP keeps scripts self-only and blocks eval"   "$(tr -d '\r' < "$APP_HEADERS" | grep -qi "script-src 'self';" && echo true || echo false)"

DASHBOARD_HEADERS="$(curl -sS -D - -o /dev/null "$BASE/dashboard")"
check "Dashboard route is served by the SPA" "$(echo "$DASHBOARD_HEADERS" | grep -qi '^Content-Security-Policy:' && echo true || echo false)"
check "Dashboard route is no-store" "$([ "$(header_value cache-control <<< "$DASHBOARD_HEADERS")" = "no-store" ] && echo true || echo false)"

SPA_HEADERS="$(curl -sS -D - -o /dev/null "$BASE/some/client/route")"
check "SPA fallback keeps the security policy" "$(echo "$SPA_HEADERS" | grep -qi '^Content-Security-Policy:' && echo true || echo false)"
check "SPA fallback is no-store" "$([ "$(header_value cache-control <<< "$SPA_HEADERS")" = "no-store" ] && echo true || echo false)"

ASSET="$(grep -oE '/assets/[^"]+\.js' /tmp/adcontrol_dashboard.html | head -1)"
ASSET_HEADERS="$(curl -sS -D - -o /dev/null "$BASE$ASSET")"
check "Hashed asset is immutable for one year" "$([ "$(header_value cache-control <<< "$ASSET_HEADERS")" = "public, max-age=31536000, immutable" ] && echo true || echo false)"
check "Hashed asset keeps CSP" "$(echo "$ASSET_HEADERS" | grep -qi '^Content-Security-Policy:' && echo true || echo false)"

# ---- Learning SEO documents and public catalog ---------------------------
SITEMAP_HEADERS="$(mktemp)"
SITEMAP="$(curl -fsS -D "$SITEMAP_HEADERS" "$BASE/sitemap.xml")"
check "Sitemap is proxied through nginx" "$([ "$(header_value content-type < "$SITEMAP_HEADERS")" = "application/xml; charset=utf-8" ] && echo true || echo false)"
check "Sitemap keeps the API cache policy" "$([ "$(header_value cache-control < "$SITEMAP_HEADERS")" = "public, max-age=1800" ] && echo true || echo false)"
check "Sitemap has one nosniff header" "$([ "$(header_count x-content-type-options < "$SITEMAP_HEADERS")" = "1" ] && echo true || echo false)"
check "Sitemap uses the configured public origin" "$(echo "$SITEMAP" | grep -Fq "<loc>${PUBLIC_ORIGIN}/courses/" && echo true || echo false)"
check "Sitemap includes the public topics index" "$(echo "$SITEMAP" | grep -Fq "<loc>${PUBLIC_ORIGIN}/topics</loc>" && echo true || echo false)"
check "Sitemap excludes authoring routes" "$(echo "$SITEMAP" | grep -qE '/(author|my-learning)' && echo false || echo true)"

ROBOTS_HEADERS="$(mktemp)"
ROBOTS="$(curl -fsS -D "$ROBOTS_HEADERS" "$BASE/robots.txt")"
check "Robots is proxied through nginx" "$([ "$(header_value content-type < "$ROBOTS_HEADERS")" = "text/plain; charset=utf-8" ] && echo true || echo false)"
check "Robots keeps the API cache policy" "$([ "$(header_value cache-control < "$ROBOTS_HEADERS")" = "public, max-age=1800" ] && echo true || echo false)"
check "Robots disallows authoring and My Learning"   "$(echo "$ROBOTS" | grep -q '^Disallow: /author$' && echo "$ROBOTS" | grep -q '^Disallow: /my-learning$' && echo true || echo false)"
check "Robots disallows the back-office dashboard" "$(echo "$ROBOTS" | grep -q '^Disallow: /dashboard$' && echo true || echo false)"
check "Robots allows all crawlers on public learning pages" \
  "$(echo "$ROBOTS" | grep -q '^User-agent: \*$' && echo "$ROBOTS" | grep -q '^Allow: /$' && echo "$ROBOTS" | grep -q '^Allow: /courses$' && echo "$ROBOTS" | grep -q '^Allow: /topics$' && echo "$ROBOTS" | grep -q '^Allow: /learning-paths$' && echo true || echo false)"
check "Robots advertises the sitemap" "$(echo "$ROBOTS" | grep -Fq "Sitemap: ${PUBLIC_ORIGIN}/sitemap.xml" && echo true || echo false)"

CATALOG="$(curl -fsS "$BASE/api/v3/catalog/courses")"
check "Public catalog is served through nginx" "$(echo "$CATALOG" | jq -e '.courses | length >= 1' >/dev/null && echo true || echo false)"
COURSE_SLUG="$(echo "$CATALOG" | jq -r '.courses[0].slug')"
COURSE_PAGE_HEADERS="$(curl -sS -D - -o /dev/null "$BASE/courses/$COURSE_SLUG")"
check "Course page route is served by the SPA" "$(echo "$COURSE_PAGE_HEADERS" | grep -qi '^Content-Security-Policy:' && echo true || echo false)"
check "Course page route is no-store" "$([ "$(header_value cache-control <<< "$COURSE_PAGE_HEADERS")" = "no-store" ] && echo true || echo false)"

DEMO_HEADERS="$(curl -sS -D - -o /dev/null "$BASE/demo/?site_key=site_pub_aaaaaaaaaaaa")"
check "Demo page served" "$(echo "$DEMO_HEADERS" | grep -q '200 OK' && echo true || echo false)"
check "Demo page uses self-only scripts" "$(echo "$DEMO_HEADERS" | grep -q "script-src 'self'" && echo true || echo false)"
DEMO_BODY="$(curl -fsS "$BASE/demo/")"
check "Demo HTML has no inline scripts" "$(echo "$DEMO_BODY" | grep -q '<script src="/demo/demo.js"></script>' && ! echo "$DEMO_BODY" | grep -qE '<script>[^<]' && echo true || echo false)"

# ---- Public SDK assets ----------------------------------------------------
SDK_HEADERS="$(curl -sS -D - -o /dev/null -H "Origin: http://$ORIGIN" "$BASE/sdk/v1/adcontrol.js")"
check "SDK JS served by nginx" "$(echo "$SDK_HEADERS" | grep -qi '^Content-Type: application/javascript' && echo true || echo false)"
check "SDK JS allows cross-origin hosts" "$(echo "$SDK_HEADERS" | grep -qi '^Access-Control-Allow-Origin: \*' && echo true || echo false)"
check "SDK JS is cached briefly" "$(echo "$SDK_HEADERS" | grep -qi '^Cache-Control: public, max-age=300' && echo true || echo false)"
CSS_HEADERS="$(curl -sS -D - -o /dev/null "$BASE/sdk/v1/adcontrol.css")"
check "SDK CSS served by nginx" "$(echo "$CSS_HEADERS" | grep -qi '^Content-Type: text/css' && echo true || echo false)"
check "SDK CSS is cacheable" "$(echo "$CSS_HEADERS" | grep -qi '^Cache-Control: public, max-age=300' && echo true || echo false)"

# ---- Admin auth and API ---------------------------------------------------
LOGIN_HEADERS="$(mktemp)"
LOGIN="$(curl -fsS -D "$LOGIN_HEADERS" "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$ADMIN_EMAIL" --arg password "$ADMIN_PASSWORD" '{email:$email,password:$password}')")"
TOKEN="$(echo "$LOGIN" | jq -r '.token')"
AUTH=(-H "Authorization: Bearer $TOKEN")
check "Login through nginx" "$([ -n "$TOKEN" ] && [ "$TOKEN" != "null" ] && echo true || echo false)"
check "Login response is no-store" "$([ "$(header_value cache-control < "$LOGIN_HEADERS")" = "no-store" ] && echo true || echo false)"
check "Login response hides password hash" "$(echo "$LOGIN" | jq -e '.user | has("password_hash")|not' >/dev/null && echo true || echo false)"
check "Login response hides salt" "$(echo "$LOGIN" | jq -e '.user | has("salt")|not' >/dev/null && echo true || echo false)"

SETTINGS_HEADERS="$(mktemp)"
SETTINGS="$(curl -fsS -D "$SETTINGS_HEADERS" "$BASE/api/v1/settings" "${AUTH[@]}")"
check "Settings via nginx" "$([ "$(echo "$SETTINGS" | jq -r '.app_name')" = "AdControl" ] && echo true || echo false)"
check "Settings do not expose SECRET_KEY" "$(echo "$SETTINGS" | jq -e 'has("secret_key")|not' >/dev/null && echo true || echo false)"
check "Settings response is no-store" "$([ "$(header_value cache-control < "$SETTINGS_HEADERS")" = "no-store" ] && echo true || echo false)"
check "API has one no-store Cache-Control" "$([ "$(header_count cache-control < "$SETTINGS_HEADERS")" = "1" ] && echo true || echo false)"

SITE="$(curl -fsS "$BASE/api/v1/sites" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg domain "$ORIGIN" --arg alt "$ALT_ORIGIN" \
    '{name:"Stack Publisher",primary_domain:$domain,allowed_domains:[$domain,$alt],status:"active"}')")"
SITE_ID="$(echo "$SITE" | jq -r '.id')"
SITE_KEY="$(echo "$SITE" | jq -r '.public_site_key')"
check "Create site through nginx" "$([ -n "$SITE_ID" ] && [ "$SITE_ID" != "null" ] && echo true || echo false)"

PLACEMENT="$(curl -fsS "$BASE/api/v1/placements" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$SITE_ID" '{site_id:$sid,placement_key:"header-banner",name:"Header Banner",format:"responsive_banner",width:728,height:90,fallback_behavior:"blank",status:"active"}')")"
PLACEMENT_ID="$(echo "$PLACEMENT" | jq -r '.id')"
check "Create placement through nginx" "$([ -n "$PLACEMENT_ID" ] && [ "$PLACEMENT_ID" != "null" ] && echo true || echo false)"

CAMPAIGN="$(curl -fsS "$BASE/api/v1/campaigns" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$SITE_ID" --arg pid "$PLACEMENT_ID" \
    '{site_id:$sid,placement_id:$pid,name:"Stack Campaign",destination_url:"https://example.com/landing?stack=1",format:"responsive_banner",title:"Stack test advertisement",body:"A harmless creative served through nginx.",image_url:"https://images.example.com/ad.png",alt_text:"Stack test advertisement",cta_label:"Learn more",status:"draft",priority:1,weight:100}')")"
CAMPAIGN_ID="$(echo "$CAMPAIGN" | jq -r '.id')"
curl -fsS -X POST "$BASE/api/v1/campaigns/$CAMPAIGN_ID/publish" "${AUTH[@]}" >/dev/null
check "Create and publish campaign through nginx" "$([ -n "$CAMPAIGN_ID" ] && [ "$CAMPAIGN_ID" != "null" ] && echo true || echo false)"

# ---- Delivery config, ETag, CORS, caching ---------------------------------
CONFIG_HEADERS="$(mktemp)"
CONFIG="$(curl -fsS -D "$CONFIG_HEADERS" -H "Origin: http://$ORIGIN" "$BASE/delivery/v1/sites/$SITE_KEY/config")"
ETAG="$(header_value etag < "$CONFIG_HEADERS")"
CLICK_URL="$(echo "$CONFIG" | jq -r '.placements["header-banner"].click_url // empty')"
check "Delivery config includes creative" "$([ "$(echo "$CONFIG" | jq -r '.placements["header-banner"].campaign_id')" = "$CAMPAIGN_ID" ] && echo true || echo false)"
check "Delivery config includes signed click URL" "$(echo "$CLICK_URL" | grep -q '^/click/' && echo true || echo false)"
check "Delivery config has ETag" "$([ -n "$ETAG" ] && echo true || echo false)"
check "Delivery config exposes ETag cross-origin" "$(tr -d '\r' < "$CONFIG_HEADERS" | grep -qi '^Access-Control-Expose-Headers: ETag' && echo true || echo false)"
check "Delivery config allows all origins" "$(tr -d '\r' < "$CONFIG_HEADERS" | grep -qi '^Access-Control-Allow-Origin: \*' && echo true || echo false)"
check "Delivery config keeps 30s caching" "$([ "$(header_value cache-control < "$CONFIG_HEADERS")" = "public, max-age=30, stale-while-revalidate=30" ] && echo true || echo false)"
check "Delivery config has no duplicated Cache-Control" "$([ "$(header_count cache-control < "$CONFIG_HEADERS")" = "1" ] && echo true || echo false)"
check "Conditional config request returns 304" "$([ "$(code -H "Origin: http://$ORIGIN" -H "If-None-Match: $ETAG" "$BASE/delivery/v1/sites/$SITE_KEY/config")" = "304" ] && echo true || echo false)"
BAD_DELIVERY_HEADERS="$(mktemp)"
BAD_DELIVERY="$(curl -sS -D "$BAD_DELIVERY_HEADERS" -o /dev/null -w '%{http_code}' -H 'Origin: https://evil.example' "$BASE/delivery/v1/sites/$SITE_KEY/config")"
check "Disallowed origin rejected through nginx" "$([ "$BAD_DELIVERY" = "403" ] && echo true || echo false)"
check "Rejected delivery is no-store" "$([ "$(header_value cache-control < "$BAD_DELIVERY_HEADERS")" = "no-store" ] && echo true || echo false)"
OPTIONS_CODE="$(code -X OPTIONS -H "Origin: http://$ORIGIN" -H 'Access-Control-Request-Method: POST' -H 'Access-Control-Request-Headers: content-type' "$BASE/delivery/v1/events")"
check "Delivery CORS preflight" "$([ "$OPTIONS_CODE" = "204" ] && echo true || echo false)"

# ---- Public events and click tracking -------------------------------------
EVENT_BODY="$(jq -nc --arg sk "$SITE_KEY" --arg cid "$CAMPAIGN_ID" \
  --arg eid "stack-impression-$RUN_ID" --arg origin "http://$ORIGIN" \
  '{site_key:$sk,event_type:"impression",event_id:$eid,placement_key:"header-banner",campaign_id:$cid,page_origin:$origin,referrer:"https://search.example/"}')"
EVENT="$(curl -fsS "$BASE/delivery/v1/events" -H 'Content-Type: text/plain;charset=UTF-8' -H "Origin: http://$ORIGIN" --data "$EVENT_BODY")"
check "Impression recorded through nginx" "$([ "$(echo "$EVENT" | jq -r '.status')" = "recorded" ] && echo true || echo false)"
EVENT_DUP="$(curl -fsS "$BASE/delivery/v1/events" -H 'Content-Type: text/plain;charset=UTF-8' -H "Origin: http://$ORIGIN" --data "$EVENT_BODY")"
check "Duplicate impression deduplicated through nginx" "$([ "$(echo "$EVENT_DUP" | jq -r '.status')" = "deduplicated" ] && echo true || echo false)"
CLICK_BEACON_CODE="$(code "$BASE/delivery/v1/events" -H 'Content-Type: text/plain;charset=UTF-8' -H "Origin: http://$ORIGIN" \
  --data "$(jq -nc --arg sk "$SITE_KEY" --arg cid "$CAMPAIGN_ID" \
    --arg eid "stack-click-beacon-$RUN_ID" --arg origin "http://$ORIGIN" \
    '{site_key:$sk,event_type:"click",event_id:$eid,placement_key:"header-banner",campaign_id:$cid,page_origin:$origin}')")"
check "Client-click beacons rejected through nginx" "$([ "$CLICK_BEACON_CODE" = "400" ] && echo true || echo false)"
PRE_CLICK_SUMMARY="$(curl -fsS "$BASE/api/v1/analytics/summary?site_id=$SITE_ID" "${AUTH[@]}")"
check "Rejected click beacon is not counted" "$([ "$(echo "$PRE_CLICK_SUMMARY" | jq -r '.clicks')" = "0" ] && echo true || echo false)"

CLICK_HEADERS="$(mktemp)"
CLICK_STATUS="$(curl -sS -o /dev/null -D "$CLICK_HEADERS" -w '%{http_code}' -H "Origin: http://$ORIGIN" "$BASE$CLICK_URL")"
check "Click redirect through nginx" "$([ "$CLICK_STATUS" = "302" ] && echo true || echo false)"
check "Click redirects to the configured destination" "$([ "$(header_value location < "$CLICK_HEADERS")" = "https://example.com/landing?stack=1" ] && echo true || echo false)"
check "Click redirect suppresses referrers" "$([ "$(header_value referrer-policy < "$CLICK_HEADERS")" = "no-referrer" ] && echo true || echo false)"
check "Click response has one referrer policy" "$([ "$(header_count referrer-policy < "$CLICK_HEADERS")" = "1" ] && echo true || echo false)"
check "Click response is no-store" "$([ "$(header_value cache-control < "$CLICK_HEADERS")" = "no-store" ] && echo true || echo false)"

# /r/ is the first-party affiliate outbound route. A full signed affiliate
# flow is exercised by scripts/test-api.sh; this checks that nginx proxies the
# route (instead of serving the SPA fallback) and preserves its safety headers.
R_HEADERS="$(mktemp)"
R_STATUS="$(curl -sS -o /dev/null -D "$R_HEADERS" -w '%{http_code}' -H "Origin: http://$ORIGIN" "$BASE/r/not-a-real-token")"
check "Affiliate redirect route is proxied by nginx" "$([ "$R_STATUS" = "400" ] && echo true || echo false)"
check "Invalid affiliate token is rejected as plain text" "$([ "$(header_value content-type < "$R_HEADERS")" = "text/plain; charset=utf-8" ] && echo true || echo false)"
check "Affiliate redirect suppresses referrers" "$([ "$(header_value referrer-policy < "$R_HEADERS")" = "no-referrer" ] && echo true || echo false)"
check "Affiliate redirect has one referrer policy" "$([ "$(header_count referrer-policy < "$R_HEADERS")" = "1" ] && echo true || echo false)"
check "Affiliate redirect is no-store" "$([ "$(header_value cache-control < "$R_HEADERS")" = "no-store" ] && echo true || echo false)"
check "Affiliate redirect is not indexable" "$([ "$(header_value x-robots-tag < "$R_HEADERS")" = "noindex, nofollow" ] && echo true || echo false)"

SUMMARY="$(curl -fsS "$BASE/api/v1/analytics/summary?site_id=$SITE_ID" "${AUTH[@]}")"
check "Analytics counts impression through stack" "$([ "$(echo "$SUMMARY" | jq -r '.impressions')" -ge 1 ] && echo true || echo false)"
check "Analytics counts server-side click" "$([ "$(echo "$SUMMARY" | jq -r '.clicks')" -ge 1 ] && echo true || echo false)"
AUDIT="$(curl -fsS "$BASE/api/v1/audit-log?limit=100" "${AUTH[@]}")"
check "Audit log includes login through stack" "$(echo "$AUDIT" | jq -e '.audit_logs[] | select(.action=="LOGIN")' >/dev/null && echo true || echo false)"
check "Audit log includes publish through stack" "$(echo "$AUDIT" | jq -e '.audit_logs[] | select(.action=="PUBLISH_CAMPAIGN")' >/dev/null && echo true || echo false)"

# A client-supplied X-Forwarded-For must not create a fresh login bucket.
# Nginx overwrites the header with $remote_addr; if it appended instead, each
# spoofed hop below would get its own bucket and no 429 would ever appear.
XFF_BLOCKED=0
for i in $(seq 1 15); do
  XFF_CODE="$(code "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
    -H "X-Forwarded-For: 203.0.113.$i" \
    --data "$(jq -nc --arg email "$ADMIN_EMAIL" '{email:$email,password:"wrong-password"}')")"
  if [ "$XFF_CODE" = "429" ]; then XFF_BLOCKED=1; break; fi
done
check "Spoofed X-Forwarded-For cannot evade login rate limiting" "$XFF_BLOCKED"

printf '%s\n' "$SITE_ID" "$SITE_KEY" "$CAMPAIGN_ID" "$PLACEMENT_ID" "$CLICK_URL" > "$IDS_FILE"
chmod 600 "$IDS_FILE"
echo "Test site/campaign IDs: $IDS_FILE"

echo
echo "RESULT: $PASS passed, $FAIL failed"
if [ "$FAIL" -eq 0 ]; then
  echo "Note: this suite exhausts the login rate limit from this host; restart"
  echo "      the API or wait five minutes before signing in manually."
fi
[ "$FAIL" -eq 0 ]
