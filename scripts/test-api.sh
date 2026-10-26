#!/usr/bin/env bash
#
# Isolated API integration suite. Starts a throwaway container and database, so
# it is safe to run alongside the Compose stack.
#
# Usage: bash scripts/test-api.sh
# Requires: docker, curl, jq, openssl, and the built adcontrol-api:latest image:
#   docker build -t adcontrol-api:latest services/api
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PORT="${IT_PORT:-18088}"
BASE="http://127.0.0.1:${PORT}"
ORIGIN="http://localhost:${PORT}"
NAME="adcontrol-it-$$"
IT_DIR="$(mktemp -d /tmp/adcontrol-it.XXXXXX)"
PASS=0
FAIL=0

cleanup() {
  docker rm -f "$NAME" >/dev/null 2>&1 || true
  if [ "$FAIL" -eq 0 ]; then
    rm -rf "$IT_DIR"
  else
    echo "Integration artifacts kept at: $IT_DIR"
  fi
}
trap cleanup EXIT

check() {
  local label="$1"
  local condition="$2"
  if [ "$condition" = "true" ] || [ "$condition" = "1" ]; then
    PASS=$((PASS + 1))
    echo "PASS  $label"
  else
    FAIL=$((FAIL + 1))
    echo "FAIL  $label"
  fi
}

status_code() {
  local url="$1"; shift
  curl -sS -o /dev/null -w '%{http_code}' "$@" "$url"
}

docker image inspect adcontrol-api:latest >/dev/null 2>&1 || {
  echo "ERROR: adcontrol-api:latest not found. Build it with:" >&2
  echo "  docker build -t adcontrol-api:latest services/api" >&2
  exit 1
}
[[ -f "$ROOT/apps/sdk/dist/adcontrol.js" ]] || {
  echo "ERROR: apps/sdk/dist/adcontrol.js not found. Build the SDK first:" >&2
  echo "  (cd apps/sdk && npm ci && npm run build)" >&2
  exit 1
}

ADMIN_PASS="$(openssl rand -hex 20)"
SECRET_KEY="$(openssl rand -hex 32)"
mkdir -p "$IT_DIR/data"
chmod 700 "$IT_DIR" "$IT_DIR/data"

umask 077
cat > "$IT_DIR/env" <<EOF
ADCONTROL_ENV=production
PORT=8088
HOST=0.0.0.0
DB_PATH=/data/adcontrol.db
SECRET_KEY=$SECRET_KEY
ADMIN_EMAIL=admin@integration.test
ADMIN_PASSWORD=$ADMIN_PASS
TRUST_PROXY=false
SDK_PATH=/app/sdk/adcontrol.js
EVENT_RETENTION_DAYS=180
BACKUP_DIR=/data/backups
BACKUP_INTERVAL_HOURS=24
BACKUP_RETENTION=3
PUBLIC_BASE_URL=https://learning.integration.test
EOF
chmod 600 "$IT_DIR/env"

docker run -d --name "$NAME" \
  --user "$(id -u):$(id -g)" \
  -p "127.0.0.1:${PORT}:8088" \
  --env-file "$IT_DIR/env" \
  -v "$IT_DIR/data:/data" \
  -v "$ROOT/apps/sdk/dist:/app/sdk:ro" \
  adcontrol-api:latest >/dev/null

READY=0
for _ in $(seq 1 40); do
  if curl -fsS "$BASE/readyz" >/dev/null 2>&1; then READY=1; break; fi
  sleep 0.5
done
check "API becomes ready" "$READY"
if [ "$READY" != "1" ]; then
  docker logs "$NAME" 2>&1 | tail -40
  exit 1
fi

HEALTH="$(curl -fsS "$BASE/healthz" | jq -r '.status')"
check "healthz returns ok" "$([ "$HEALTH" = "ok" ] && echo true || echo false)"

SDK_CT="$(curl -sS -D - -o /dev/null "$BASE/sdk/v1/adcontrol.js" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print $2}')"
check "SDK JavaScript content type" "$(echo "$SDK_CT" | grep -qi 'javascript' && echo true || echo false)"
CSS_CT="$(curl -sS -D - -o /dev/null "$BASE/sdk/v1/adcontrol.css" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print $2}')"
check "SDK CSS content type" "$(echo "$CSS_CT" | grep -qi 'text/css' && echo true || echo false)"
CSS_BODY="$(curl -fsS "$BASE/sdk/v1/adcontrol.css")"
check "SDK CSS served" "$(echo "$CSS_BODY" | grep -q '.adcontrol-root' && echo true || echo false)"
SDK_BODY="$(curl -fsS "$BASE/sdk/v1/adcontrol.js")"
check "SDK JS is v1.3.0" "$(echo "$SDK_BODY" | grep -q '1.3.0' && echo true || echo false)"

BAD_LOGIN_CODE="$(status_code "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' -d '{"email":"admin@integration.test","password":"wrong-password"}')"
check "Wrong password rejected" "$([ "$BAD_LOGIN_CODE" = "401" ] && echo true || echo false)"

LOGIN_JSON="$(curl -fsS "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email 'admin@integration.test' --arg password "$ADMIN_PASS" '{email:$email,password:$password}')")"
TOKEN="$(echo "$LOGIN_JSON" | jq -r '.token')"
check "Admin login returns bearer token" "$([ -n "$TOKEN" ] && [ "$TOKEN" != "null" ] && echo true || echo false)"
AUTH=(-H "Authorization: Bearer $TOKEN")

ME="$(curl -fsS "$BASE/api/v1/auth/me" "${AUTH[@]}")"
check "GET /auth/me identifies admin" "$([ "$(echo "$ME" | jq -r '.user.role')" = "admin" ] && echo true || echo false)"

SETTINGS="$(curl -fsS "$BASE/api/v1/settings" "${AUTH[@]}")"
check "Settings omit secrets" "$(echo "$SETTINGS" | jq -e 'has("secret_key")|not' >/dev/null && echo true || echo false)"
check "Settings report SDK 1.3.0" "$([ "$(echo "$SETTINGS" | jq -r '.sdk_version')" = "1.3.0" ] && echo true || echo false)"

SITE_JSON="$(jq -nc --arg domain "localhost:${PORT}" \
  '{name:"Integration Publisher",primary_domain:$domain,allowed_domains:[$domain,"127.0.0.1:'"${PORT}"'"],status:"active"}')"
SITE="$(curl -fsS "$BASE/api/v1/sites" "${AUTH[@]}" -H 'Content-Type: application/json' --data "$SITE_JSON")"
SITE_ID="$(echo "$SITE" | jq -r '.id')"
SITE_KEY="$(echo "$SITE" | jq -r '.public_site_key')"
check "Create site" "$([ -n "$SITE_ID" ] && [ "$SITE_ID" != "null" ] && echo true || echo false)"
check "Site key generated" "$(echo "$SITE_KEY" | grep -q '^site_pub_' && echo true || echo false)"

PLACEMENT="$(curl -fsS "$BASE/api/v1/placements" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$SITE_ID" '{site_id:$sid,placement_key:"header-banner",name:"Header Banner",format:"responsive_banner",width:728,height:90,fallback_behavior:"blank",status:"active"}')")"
PLACEMENT_ID="$(echo "$PLACEMENT" | jq -r '.id')"
check "Create placement" "$([ -n "$PLACEMENT_ID" ] && [ "$PLACEMENT_ID" != "null" ] && echo true || echo false)"

UNSAFE_CODE="$(status_code "$BASE/api/v1/campaigns" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$SITE_ID" --arg pid "$PLACEMENT_ID" \
    '{site_id:$sid,placement_id:$pid,name:"Unsafe Campaign",destination_url:"javascript:alert(1)",format:"responsive_banner",title:"Unsafe",body:"",image_url:"",alt_text:"",cta_label:"",status:"draft",starts_at:"",ends_at:"",priority:1,weight:100}')")"
check "Unsafe destination rejected" "$([ "$UNSAFE_CODE" = "400" ] && echo true || echo false)"


CAMPAIGN="$(curl -fsS "$BASE/api/v1/campaigns" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$SITE_ID" --arg pid "$PLACEMENT_ID" \
    '{site_id:$sid,placement_id:$pid,name:"Integration Campaign",destination_url:"https://example.com/landing?utm_source=it",format:"responsive_banner",title:"Integration test advertisement",body:"A harmless integration test creative.",image_url:"https://images.example.com/ad.png",alt_text:"Integration test advertisement",cta_label:"Learn more",status:"draft",priority:1,weight:100}')")"
CAMPAIGN_ID="$(echo "$CAMPAIGN" | jq -r '.id')"
check "Create campaign" "$([ -n "$CAMPAIGN_ID" ] && [ "$CAMPAIGN_ID" != "null" ] && echo true || echo false)"

PUBLISHED="$(curl -fsS -X POST "$BASE/api/v1/campaigns/$CAMPAIGN_ID/publish" "${AUTH[@]}")"
check "Publish campaign" "$([ "$(echo "$PUBLISHED" | jq -r '.status')" = "active" ] && echo true || echo false)"

CONFIG_HEADERS="$IT_DIR/config.headers"
CONFIG="$(curl -fsS -D "$CONFIG_HEADERS" -H "Origin: $ORIGIN" "$BASE/delivery/v1/sites/$SITE_KEY/config")"
ETAG="$(tr -d '\r' < "$CONFIG_HEADERS" | awk -F': ' 'tolower($1)=="etag"{print $2}')"
CLICK_URL="$(echo "$CONFIG" | jq -r '.placements["header-banner"].click_url // empty')"
check "Delivery config includes signed click URL" "$(echo "$CLICK_URL" | grep -q '^/click/' && echo true || echo false)"
check "Delivery config has ETag" "$([ -n "$ETAG" ] && echo true || echo false)"
check "Delivery CORS exposes ETag" "$(tr -d '\r' < "$CONFIG_HEADERS" | grep -qi 'access-control-expose-headers: ETag' && echo true || echo false)"
CACHE_HEADER="$(tr -d '\r' < "$CONFIG_HEADERS" | awk -F': ' 'tolower($1)=="cache-control"{print $2}')"
check "Delivery config is briefly cacheable" "$(echo "$CACHE_HEADER" | grep -q 'max-age=30' && echo true || echo false)"

ETAG_304="$(status_code "$BASE/delivery/v1/sites/$SITE_KEY/config" -H "Origin: $ORIGIN" -H "If-None-Match: $ETAG")"
check "Conditional config request returns 304" "$([ "$ETAG_304" = "304" ] && echo true || echo false)"
BAD_ORIGIN_CODE="$(status_code "$BASE/delivery/v1/sites/$SITE_KEY/config" -H 'Origin: https://evil.example')"
check "Disallowed origin rejected on delivery" "$([ "$BAD_ORIGIN_CODE" = "403" ] && echo true || echo false)"

SANDBOX="$(curl -fsS "$BASE/api/v1/sandbox/sites/$SITE_ID" "${AUTH[@]}")"
check "Sandbox returns deliverable creative" "$([ "$(echo "$SANDBOX" | jq -r '.placements[0].campaign.id')" = "$CAMPAIGN_ID" ] && echo true || echo false)"
check "Sandbox preview click URL" "$(echo "$SANDBOX" | jq -r '.placements[0].click_url' | grep -q '^/click/' && echo true || echo false)"

EVENT_BODY="$(jq -nc --arg sk "$SITE_KEY" --arg cid "$CAMPAIGN_ID" \
  '{site_key:$sk,event_type:"impression",event_id:"it-impression-0001",placement_key:"header-banner",campaign_id:$cid,page_origin:"http://localhost:'"${PORT}"'",referrer:"https://search.example/"}')"
EVENT_RESPONSE="$(curl -fsS "$BASE/delivery/v1/events" -H 'Content-Type: application/json' -H "Origin: $ORIGIN" --data "$EVENT_BODY")"
check "Impression event recorded" "$([ "$(echo "$EVENT_RESPONSE" | jq -r '.status')" = "recorded" ] && echo true || echo false)"
DUP_RESPONSE="$(curl -fsS "$BASE/delivery/v1/events" -H 'Content-Type: application/json' -H "Origin: $ORIGIN" --data "$EVENT_BODY")"
check "Duplicate event idempotent" "$([ "$(echo "$DUP_RESPONSE" | jq -r '.status')" = "deduplicated" ] && echo true || echo false)"
BAD_EVENT_CODE="$(status_code "$BASE/delivery/v1/events" -H 'Content-Type: application/json' -H "Origin: $ORIGIN" \
  --data "$(jq -nc --arg sk "$SITE_KEY" --arg cid "$CAMPAIGN_ID" '{site_key:$sk,event_type:"impression",event_id:"bad id",placement_key:"header-banner",campaign_id:$cid}')")"
check "Unsafe event id rejected" "$([ "$BAD_EVENT_CODE" = "400" ] && echo true || echo false)"
BAD_EVENT_ORIGIN="$(status_code "$BASE/delivery/v1/events" -H 'Content-Type: application/json' -H 'Origin: https://evil.example' --data "$EVENT_BODY")"
check "Disallowed event origin rejected" "$([ "$BAD_EVENT_ORIGIN" = "403" ] && echo true || echo false)"
CLICK_BEACON_CODE="$(status_code "$BASE/delivery/v1/events" -H 'Content-Type: application/json' -H "Origin: $ORIGIN" \
  --data "$(jq -nc --arg sk "$SITE_KEY" --arg cid "$CAMPAIGN_ID" '{site_key:$sk,event_type:"click",event_id:"it-click-beacon-0001",placement_key:"header-banner",campaign_id:$cid,page_origin:"http://localhost:'"${PORT}"'"}')")"
check "Client-submitted click beacons rejected" "$([ "$CLICK_BEACON_CODE" = "400" ] && echo true || echo false)"
PRE_CLICK_SUMMARY="$(curl -fsS "$BASE/api/v1/analytics/summary?site_id=$SITE_ID" "${AUTH[@]}")"
check "Rejected click beacon does not increment analytics" "$([ "$(echo "$PRE_CLICK_SUMMARY" | jq -r '.clicks')" = "0" ] && echo true || echo false)"

CLICK_HEADERS="$IT_DIR/click.headers"
CLICK_STATUS="$(curl -sS -o /dev/null -D "$CLICK_HEADERS" -w '%{http_code}' -H "Origin: $ORIGIN" "$BASE$CLICK_URL")"
check "Signed click redirects" "$([ "$CLICK_STATUS" = "302" ] && echo true || echo false)"
LOCATION="$(tr -d '\r' < "$CLICK_HEADERS" | awk -F': ' 'tolower($1)=="location"{print $2}')"
check "Click redirects to configured destination" "$([ "$LOCATION" = "https://example.com/landing?utm_source=it" ] && echo true || echo false)"
BAD_CLICK_ORIGIN="$(status_code "$BASE$CLICK_URL" -H 'Origin: https://evil.example')"
check "Disallowed click origin rejected" "$([ "$BAD_CLICK_ORIGIN" = "403" ] && echo true || echo false)"
INVALID_CLICK_CODE="$(status_code "$BASE/click/not-a-real-token" -H "Origin: $ORIGIN")"
check "Invalid click token rejected" "$([ "$INVALID_CLICK_CODE" = "400" ] && echo true || echo false)"

SUMMARY="$(curl -fsS "$BASE/api/v1/analytics/summary?site_id=$SITE_ID" "${AUTH[@]}")"
check "Summary counts impression" "$([ "$(echo "$SUMMARY" | jq -r '.impressions')" -ge 1 ] && echo true || echo false)"
check "Summary counts click" "$([ "$(echo "$SUMMARY" | jq -r '.clicks')" -ge 1 ] && echo true || echo false)"
TIMESERIES="$(curl -fsS "$BASE/api/v1/analytics/timeseries?site_id=$SITE_ID&days=7" "${AUTH[@]}")"
check "Timeseries returns array" "$(echo "$TIMESERIES" | jq -e 'type == "array"' >/dev/null && echo true || echo false)"
BREAKDOWN="$(curl -fsS "$BASE/api/v1/analytics/breakdown?dimension=campaign&site_id=$SITE_ID" "${AUTH[@]}")"
check "Breakdown includes campaign" "$(echo "$BREAKDOWN" | jq -e --arg cid "$CAMPAIGN_ID" '.rows[] | select(.key == $cid)' >/dev/null && echo true || echo false)"
CSV_HEADERS="$IT_DIR/csv.headers"
CSV_STATUS="$(curl -sS -o "$IT_DIR/events.csv" -D "$CSV_HEADERS" -w '%{http_code}' \
  "$BASE/api/v1/analytics/export.csv?site_id=$SITE_ID" "${AUTH[@]}")"
check "Authenticated CSV export succeeds" "$([ "$CSV_STATUS" = "200" ] && echo true || echo false)"
check "CSV has attachment header" "$(tr -d '\r' < "$CSV_HEADERS" | grep -qi 'content-disposition: attachment' && echo true || echo false)"
check "CSV contains event rows" "$(grep -q 'it-impression-0001' "$IT_DIR/events.csv" && echo true || echo false)"
CSV_TOKEN_CODE="$(status_code "$BASE/api/v1/analytics/export.csv?site_id=$SITE_ID&token=$TOKEN")"
check "CSV rejects query-string tokens" "$([ "$CSV_TOKEN_CODE" = "401" ] && echo true || echo false)"

AUDIT="$(curl -fsS "$BASE/api/v1/audit-log?limit=50" "${AUTH[@]}")"
check "Audit log records campaign publish" "$(echo "$AUDIT" | jq -e '.audit_logs[] | select(.action == "PUBLISH_CAMPAIGN")' >/dev/null && echo true || echo false)"
check "Audit log records login" "$(echo "$AUDIT" | jq -e '.audit_logs[] | select(.action == "LOGIN")' >/dev/null && echo true || echo false)"

# ==========================================================================
# Phase 2: publisher tenancy, domain verification, affiliate offers, /r/
# ==========================================================================
PUB_EMAIL="publisher-$$@integration.test"
PUB_PASS="$(openssl rand -hex 16)"
PUB_CREATE="$(curl -fsS "$BASE/api/v1/publishers" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$PUB_EMAIL" --arg pass "$PUB_PASS" \
    '{name:"Integration Affiliate Publisher",owner_email:$email,password:$pass,status:"active",contact_email:$email,timezone:"UTC"}')")"
PUBLISHER_ID="$(echo "$PUB_CREATE" | jq -r '.id')"
check "Admin provisions publisher account" "$([ -n "$PUBLISHER_ID" ] && [ "$PUBLISHER_ID" != "null" ] && echo true || echo false)"
check "Publisher response omits password material" "$(echo "$PUB_CREATE" | jq -e '(has("password")|not) and (has("password_hash")|not)' >/dev/null && echo true || echo false)"

PUB_LOGIN="$(curl -fsS "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$PUB_EMAIL" --arg password "$PUB_PASS" '{email:$email,password:$password}')")"
PUB_TOKEN="$(echo "$PUB_LOGIN" | jq -r '.token')"
PUB_AUTH=(-H "Authorization: Bearer $PUB_TOKEN")
check "Publisher can log in" "$([ -n "$PUB_TOKEN" ] && [ "$PUB_TOKEN" != "null" ] && echo true || echo false)"
check "Publisher identity is tenant-scoped" "$(echo "$PUB_LOGIN" | jq -e --arg pid "$PUBLISHER_ID" '.user.role == "publisher" and .user.publisher_id == $pid' >/dev/null && echo true || echo false)"

PUB_SITE_JSON="$(jq -nc --arg domain "shop.publisher.test" --arg origin "$ORIGIN" \
  '{name:"Publisher Monetized Site",primary_domain:$domain,allowed_domains:[$domain,$origin],default_disclosure_text:"Affiliate disclosure: this site may earn a commission.",status:"active"}')"
PUB_SITE="$(curl -fsS "$BASE/api/v1/sites" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' --data "$PUB_SITE_JSON")"
PUB_SITE_ID="$(echo "$PUB_SITE" | jq -r '.id')"
PUB_SITE_KEY="$(echo "$PUB_SITE" | jq -r '.public_site_key')"
check "Publisher creates owned site" "$([ -n "$PUB_SITE_ID" ] && [ "$PUB_SITE_ID" != "null" ] && echo true || echo false)"
check "Publisher site starts unverified and non-monetized" "$(echo "$PUB_SITE" | jq -e --arg pid "$PUBLISHER_ID" '.publisher_id == $pid and .verification_status == "unverified" and .monetization_status == "pending"' >/dev/null && echo true || echo false)"
check "Verification token never appears on the site object" "$(echo "$PUB_SITE" | jq -e 'has("verification_token")|not' >/dev/null && echo true || echo false)"
UNVERIFIED_DELIVERY="$(status_code "$BASE/delivery/v1/sites/$PUB_SITE_KEY/config" -H "Origin: $ORIGIN")"
check "Unverified publisher site is dark in delivery" "$([ "$UNVERIFIED_DELIVERY" = "404" ] && echo true || echo false)"

VERIFICATION="$(curl -fsS "$BASE/api/v1/sites/$PUB_SITE_ID/verification" "${PUB_AUTH[@]}")"
check "Publisher can read its DNS TXT challenge" "$(echo "$VERIFICATION" | jq -e '.record_type == "TXT" and (.record_name | startswith("_adcontrol-challenge.")) and (.record_value | startswith("adcontrol-site-verification="))' >/dev/null && echo true || echo false)"
PUB_MANUAL_CODE="$(status_code "$BASE/api/v1/sites/$PUB_SITE_ID/verify" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' --data '{"method":"manual"}')"
check "Publisher cannot self-approve manual verification" "$([ "$PUB_MANUAL_CODE" = "403" ] && echo true || echo false)"
MANUAL_VERIFY="$(curl -fsS "$BASE/api/v1/sites/$PUB_SITE_ID/verify" "${AUTH[@]}" -H 'Content-Type: application/json' --data '{"method":"manual"}')"
check "Admin manual verification fallback succeeds" "$(echo "$MANUAL_VERIFY" | jq -e '.status == "verified" and .site.verification_status == "verified" and .site.monetization_status == "enabled"' >/dev/null && echo true || echo false)"
check "Verified site drops its outstanding token" "$(echo "$MANUAL_VERIFY" | jq -e '.site | has("verification_token")|not' >/dev/null && echo true || echo false)"

PUB_PLACEMENT="$(curl -fsS "$BASE/api/v1/placements" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$PUB_SITE_ID" \
    '{site_id:$sid,placement_key:"product-feed",name:"Product Feed",format:"product_card",width:0,height:0,fallback_behavior:"hide",allowed_source_types:["house","direct","affiliate"],max_items:1,disclosure_mode:"required",status:"active"}')")"
PUB_PLACEMENT_ID="$(echo "$PUB_PLACEMENT" | jq -r '.id')"
check "Publisher creates product-card placement" "$([ -n "$PUB_PLACEMENT_ID" ] && [ "$PUB_PLACEMENT_ID" != "null" ] && echo true || echo false)"

offer_body() {
  jq -nc --arg url "$1" --arg pub "$2" \
    '{provider:"shopee_affiliate",offer_name:"Shopee integration offer",product_title:"Integration Product",product_image_url:"https://cdn.example.com/product.jpg",product_description:"An integration product description.",merchant_name:"Shopee Merchant",affiliate_url:$url,canonical_product_url:"https://shopee.example.com/product/123",price_text:"IDR 199.000",currency:"IDR",availability_checked_at:"",disclosure_text:"Affiliate disclosure: we may earn a commission from purchases made through this link.",status:"draft"} + (if $pub != "" then {publisher_id:$pub} else {} end)'
}
offer_create_code() {
  status_code "$BASE/api/v1/affiliate-offers" "$@" -H 'Content-Type: application/json'
}

OFFER_URL='https://shopee.example.com/product/123/456?smtt=0.123&utm_content=abc%20def&affiliate_id=XYZ&plus=1%2B2&empty='
BAD_OFFER_HTTP="$(offer_create_code "${PUB_AUTH[@]}" --data "$(offer_body 'http://shopee.example.com/product/1' '')")"
check "Affiliate offer rejects plain HTTP" "$([ "$BAD_OFFER_HTTP" = "400" ] && echo true || echo false)"
BAD_OFFER_JS="$(offer_create_code "${PUB_AUTH[@]}" --data "$(offer_body 'javascript:alert(1)' '')")"
check "Affiliate offer rejects javascript scheme" "$([ "$BAD_OFFER_JS" = "400" ] && echo true || echo false)"
BAD_OFFER_USERINFO="$(offer_create_code "${PUB_AUTH[@]}" --data "$(offer_body 'https://user:pass@shopee.example.com/product/1' '')")"
check "Affiliate offer rejects URL userinfo" "$([ "$BAD_OFFER_USERINFO" = "400" ] && echo true || echo false)"
BAD_OFFER_LOCALHOST="$(offer_create_code "${PUB_AUTH[@]}" --data "$(offer_body 'https://localhost/product/1' '')")"
check "Affiliate offer rejects non-public hostnames" "$([ "$BAD_OFFER_LOCALHOST" = "400" ] && echo true || echo false)"
BAD_OFFER_SPACE="$(offer_create_code "${PUB_AUTH[@]}" --data "$(offer_body ' https://shopee.example.com/product/1' '')")"
check "Affiliate offer rejects leading whitespace" "$([ "$BAD_OFFER_SPACE" = "400" ] && echo true || echo false)"

OFFER="$(curl -fsS "$BASE/api/v1/affiliate-offers" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' --data "$(offer_body "$OFFER_URL" '')")"
OFFER_ID="$(echo "$OFFER" | jq -r '.id')"
check "Publisher creates Shopee affiliate offer" "$([ -n "$OFFER_ID" ] && [ "$OFFER_ID" != "null" ] && echo true || echo false)"
check "Affiliate URL is stored byte-for-byte" "$(echo "$OFFER" | jq -e --arg url "$OFFER_URL" '.affiliate_url == $url' >/dev/null && echo true || echo false)"
check "New offer requires explicit structural validation" "$(echo "$OFFER" | jq -e '.validation_status == "unverified"' >/dev/null && echo true || echo false)"

OFFER_VALIDATED="$(curl -fsS -X POST "$BASE/api/v1/affiliate-offers/$OFFER_ID/validate" "${PUB_AUTH[@]}")"
check "Offer validation is structural only" "$(echo "$OFFER_VALIDATED" | jq -e '.validation_status == "valid" and (.validation_reason | test("not fetched"))' >/dev/null && echo true || echo false)"
OFFER_ACTIVE="$(curl -fsS -X POST "$BASE/api/v1/affiliate-offers/$OFFER_ID/publish" "${PUB_AUTH[@]}")"
check "Offer can be activated after validation" "$(echo "$OFFER_ACTIVE" | jq -e '.status == "active"' >/dev/null && echo true || echo false)"
OFFER_FETCHED="$(curl -fsS "$BASE/api/v1/affiliate-offers/$OFFER_ID" "${PUB_AUTH[@]}")"
check "Stored affiliate URL is not rewritten on read" "$(echo "$OFFER_FETCHED" | jq -e --arg url "$OFFER_URL" '.affiliate_offer.affiliate_url == $url' >/dev/null && echo true || echo false)"

PLACED="$(curl -fsS "$BASE/api/v1/affiliate-offers/$OFFER_ID/placements" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg pid "$PUB_PLACEMENT_ID" \
    '{placement_id:$pid,name:"Integration Product Card",status:"draft",starts_at:"",ends_at:"",priority:1,weight:100}')")"
AFF_CAMPAIGN_ID="$(echo "$PLACED" | jq -r '.id')"
check "Offer can be placed with a server-derived destination" "$(echo "$PLACED" | jq -e --arg url "$OFFER_URL" --arg oid "$OFFER_ID" '.source_type == "affiliate" and .affiliate_offer_id == $oid and .format == "product_card" and .destination_url == $url' >/dev/null && echo true || echo false)"
AFF_PUBLISHED="$(curl -fsS -X POST "$BASE/api/v1/campaigns/$AFF_CAMPAIGN_ID/publish" "${PUB_AUTH[@]}")"
check "Affiliate product campaign publishes" "$(echo "$AFF_PUBLISHED" | jq -e '.status == "active"' >/dev/null && echo true || echo false)"

AFF_CONFIG="$(curl -fsS -H "Origin: $ORIGIN" "$BASE/delivery/v1/sites/$PUB_SITE_KEY/config")"
AFF_CLICK_URL="$(echo "$AFF_CONFIG" | jq -r '.placements["product-feed"].click_url // empty')"
check "Affiliate delivery uses first-party /r/ click URL" "$(echo "$AFF_CLICK_URL" | grep -q '^/r/' && echo true || echo false)"
check "Affiliate creative is delivered as product_card" "$(echo "$AFF_CONFIG" | jq -e '.placements["product-feed"].format == "product_card" and .placements["product-feed"].source_type == "affiliate" and .placements["product-feed"].affiliate == true' >/dev/null && echo true || echo false)"
check "Affiliate delivery carries merchant and price" "$(echo "$AFF_CONFIG" | jq -e '.placements["product-feed"].merchant_name == "Shopee Merchant" and .placements["product-feed"].price_text == "IDR 199.000"' >/dev/null && echo true || echo false)"
check "Affiliate delivery always carries visible disclosure" "$(echo "$AFF_CONFIG" | jq -e '(.placements["product-feed"].disclosure_text | length) > 0' >/dev/null && echo true || echo false)"

AFF_CLICK_HEADERS="$IT_DIR/affiliate-click.headers"
AFF_CLICK_STATUS="$(curl -sS -o /dev/null -D "$AFF_CLICK_HEADERS" -w '%{http_code}' -H "Origin: $ORIGIN" "$BASE$AFF_CLICK_URL")"
check "First-party affiliate redirect returns 302" "$([ "$AFF_CLICK_STATUS" = "302" ] && echo true || echo false)"
AFF_LOCATION="$(tr -d '\r' < "$AFF_CLICK_HEADERS" | awk -F': ' 'tolower($1)=="location"{print $2}')"
check "Affiliate redirect preserves the exact stored URL" "$([ "$AFF_LOCATION" = "$OFFER_URL" ] && echo true || echo false)"
TAMPER_HEADERS="$IT_DIR/affiliate-tamper.headers"
TAMPER_STATUS="$(curl -sS -o /dev/null -D "$TAMPER_HEADERS" -w '%{http_code}' -H "Origin: $ORIGIN" "$BASE$AFF_CLICK_URL?next=https%3A%2F%2Fevil.example")"
TAMPER_LOCATION="$(tr -d '\r' < "$TAMPER_HEADERS" | awk -F': ' 'tolower($1)=="location"{print $2}')"
check "Query-string destination cannot override affiliate redirect" "$([ "$TAMPER_STATUS" = "302" ] && [ "$TAMPER_LOCATION" = "$OFFER_URL" ] && echo true || echo false)"
check "Invalid /r/ token is rejected" "$([ "$(status_code "$BASE/r/not-a-real-token" -H "Origin: $ORIGIN")" = "400" ] && echo true || echo false)"

AFF_SUMMARY="$(curl -fsS "$BASE/api/v1/analytics/summary?site_id=$PUB_SITE_ID" "${PUB_AUTH[@]}")"
check "Affiliate clicks appear in observed analytics" "$([ "$(echo "$AFF_SUMMARY" | jq -r '.affiliate_clicks')" -ge 1 ] && echo true || echo false)"
check "Outbound redirects are counted separately" "$([ "$(echo "$AFF_SUMMARY" | jq -r '.outbound_redirects')" -ge 1 ] && echo true || echo false)"
check "Confirmed commission is never inferred" "$(echo "$AFF_SUMMARY" | jq -e '.confirmed_conversions == 0 and .confirmed_commission == 0 and .provenance.conversions == "not_connected"' >/dev/null && echo true || echo false)"
AFF_BREAKDOWN="$(curl -fsS "$BASE/api/v1/analytics/breakdown?dimension=source&site_id=$PUB_SITE_ID" "${PUB_AUTH[@]}")"
check "Analytics supports ad-source breakdown" "$(echo "$AFF_BREAKDOWN" | jq -e '.rows[] | select(.key == "affiliate" and .outbound_redirects >= 1)' >/dev/null && echo true || echo false)"

# Narrowing a site's source policy must take effect immediately for existing
# campaigns, not only when a new campaign is created.
SOURCE_RESTRICTED="$(curl -fsS -X PATCH "$BASE/api/v1/sites/$PUB_SITE_ID" "${PUB_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"allowed_ad_sources":["house"]}')"
check "Site source policy can be narrowed" \
  "$(echo "$SOURCE_RESTRICTED" | jq -e '.allowed_ad_sources == ["house"]' >/dev/null && echo true || echo false)"
RESTRICTED_CONFIG="$(curl -fsS -H "Origin: $ORIGIN" "$BASE/delivery/v1/sites/$PUB_SITE_KEY/config")"
check "Source-disallowed affiliate campaign stops delivering" \
  "$(echo "$RESTRICTED_CONFIG" | jq -e '.placements["product-feed"].campaign_id == "" and (.placements["product-feed"] | has("click_url") | not)' >/dev/null && echo true || echo false)"
check "Source-disallowed affiliate click is refused" \
  "$([ "$(status_code "$BASE$AFF_CLICK_URL" -H "Origin: $ORIGIN")" = "410" ] && echo true || echo false)"
SOURCE_RESTORED="$(curl -fsS -X PATCH "$BASE/api/v1/sites/$PUB_SITE_ID" "${PUB_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"allowed_ad_sources":["house","direct","affiliate"]}')"
check "Site source policy restores" \
  "$(echo "$SOURCE_RESTORED" | jq -e '.allowed_ad_sources | index("affiliate") != null' >/dev/null && echo true || echo false)"

# Tenant isolation: another publisher's inventory is invisible and cannot be
# mixed into this publisher's placements.
PUB2_EMAIL="publisher2-$$@integration.test"
PUB2_CREATE="$(curl -fsS "$BASE/api/v1/publishers" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$PUB2_EMAIL" \
    '{name:"Other Affiliate Publisher",owner_email:$email,password:"another-strong-test-password",status:"active",contact_email:$email,timezone:"UTC"}')")"
PUB2_ID="$(echo "$PUB2_CREATE" | jq -r '.id')"
OFFER2="$(curl -fsS "$BASE/api/v1/affiliate-offers" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(offer_body 'https://shopee.example.com/product/other?aff=2' "$PUB2_ID")")"
OFFER2_ID="$(echo "$OFFER2" | jq -r '.id')"
check "Cross-tenant offer fetch is hidden" "$([ "$(status_code "$BASE/api/v1/affiliate-offers/$OFFER2_ID" "${PUB_AUTH[@]}")" = "404" ] && echo true || echo false)"
check "Cross-tenant publisher profile is hidden" "$([ "$(status_code "$BASE/api/v1/publishers/$PUB2_ID" "${PUB_AUTH[@]}")" = "404" ] && echo true || echo false)"
CROSS_PLACE="$(status_code "$BASE/api/v1/affiliate-offers/$OFFER2_ID/placements" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg pid "$PUB_PLACEMENT_ID" '{placement_id:$pid,name:"Cross tenant",status:"draft",starts_at:"",ends_at:"",priority:1,weight:100}')")"
check "Placement rejects an offer from another publisher" "$([ "$CROSS_PLACE" = "400" ] && echo true || echo false)"
check "Publisher cannot read the admin audit log" "$([ "$(status_code "$BASE/api/v1/audit-log" "${PUB_AUTH[@]}")" = "403" ] && echo true || echo false)"
PUB_DIRECT_CODE="$(status_code "$BASE/api/v1/campaigns" "${PUB_AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg sid "$PUB_SITE_ID" --arg pid "$PUB_PLACEMENT_ID" \
    '{site_id:$sid,placement_id:$pid,name:"Forbidden Direct",destination_url:"https://example.com/direct",format:"responsive_banner",source_type:"direct",status:"draft",starts_at:"",ends_at:"",priority:1,weight:100}')")"
check "Publisher cannot create direct advertiser campaigns" "$([ "$PUB_DIRECT_CODE" = "400" ] && echo true || echo false)"
PUB_VERIFY_FIELD_CODE="$(status_code "$BASE/api/v1/sites/$PUB_SITE_ID" "${PUB_AUTH[@]}" -X PATCH -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg domain "shop.publisher.test" --arg origin "$ORIGIN" \
    '{name:"Publisher Monetized Site",primary_domain:$domain,allowed_domains:[$domain,$origin],default_disclosure_text:"Affiliate disclosure: this site may earn a commission.",allowed_ad_sources:["house","affiliate"],status:"active",verification_status:"verified",monetization_status:"enabled"}')")"
check "Publisher cannot mutate verification fields" "$([ "$PUB_VERIFY_FIELD_CODE" = "403" ] && echo true || echo false)"

# Publisher profile updates are scoped to contact details. Tenant identity,
# lifecycle status, and ownership belong to the administrator.
PUB_CONTACT_UPDATE="$(curl -fsS -X PATCH "$BASE/api/v1/publishers/$PUBLISHER_ID" "${PUB_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data '{"contact_email":"billing@integration.test","timezone":"Asia/Jakarta"}')"
check "Publisher can update its own contact details" \
  "$(echo "$PUB_CONTACT_UPDATE" | jq -e '.contact_email == "billing@integration.test" and .timezone == "Asia/Jakarta"' >/dev/null && echo true || echo false)"
PUB_NAME_CODE="$(status_code "$BASE/api/v1/publishers/$PUBLISHER_ID" "${PUB_AUTH[@]}" -X PATCH \
  -H 'Content-Type: application/json' --data '{"name":"Renamed By Publisher"}')"
check "Publisher cannot rename its tenant" "$([ "$PUB_NAME_CODE" = "403" ] && echo true || echo false)"
PUB_STATUS_CODE="$(status_code "$BASE/api/v1/publishers/$PUBLISHER_ID" "${PUB_AUTH[@]}" -X PATCH \
  -H 'Content-Type: application/json' --data '{"status":"suspended"}')"
check "Publisher cannot change its own lifecycle status" "$([ "$PUB_STATUS_CODE" = "403" ] && echo true || echo false)"

PAUSE="$(curl -fsS -X POST "$BASE/api/v1/campaigns/$CAMPAIGN_ID/pause" "${AUTH[@]}")"
check "Campaign paused" "$([ "$(echo "$PAUSE" | jq -r '.status')" = "paused" ] && echo true || echo false)"
PAUSED_CONFIG="$(curl -fsS -H "Origin: $ORIGIN" "$BASE/delivery/v1/sites/$SITE_KEY/config")"
check "Paused campaign removed from delivery" "$(echo "$PAUSED_CONFIG" | jq -e '.placements["header-banner"].campaign_id == ""' >/dev/null && echo true || echo false)"
check "Paused placement exposes blank fallback metadata" "$(echo "$PAUSED_CONFIG" | jq -e '.placements["header-banner"].fallback_behavior == "blank" and .placements["header-banner"].width == 728 and .placements["header-banner"].height == 90' >/dev/null && echo true || echo false)"
check "Fallback entry has no signed click URL" "$(echo "$PAUSED_CONFIG" | jq -e '.placements["header-banner"] | has("click_url") | not' >/dev/null && echo true || echo false)"
PAUSED_CLICK="$(status_code "$BASE$CLICK_URL" -H "Origin: $ORIGIN")"
check "Paused campaign click returns 410" "$([ "$PAUSED_CLICK" = "410" ] && echo true || echo false)"
PAUSED_SANDBOX="$(curl -fsS "$BASE/api/v1/sandbox/sites/$SITE_ID" "${AUTH[@]}")"
check "Sandbox reports no campaign when paused" "$([ "$(echo "$PAUSED_SANDBOX" | jq -r '.placements[0].campaign')" = "null" ] && echo true || echo false)"

SITE_DISABLE="$(curl -fsS -X PATCH "$BASE/api/v1/sites/$SITE_ID" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg domain "localhost:${PORT}" '{name:"Integration Publisher",primary_domain:$domain,allowed_domains:[$domain,"127.0.0.1:'"${PORT}"'"],status:"disabled"}')")"
check "Site disabled" "$([ "$(echo "$SITE_DISABLE" | jq -r '.status')" = "disabled" ] && echo true || echo false)"
DISABLED_CODE="$(status_code "$BASE/delivery/v1/sites/$SITE_KEY/config" -H "Origin: $ORIGIN")"
check "Disabled site returns 404" "$([ "$DISABLED_CODE" = "404" ] && echo true || echo false)"

PASSWORD_SHORT="$(status_code "$BASE/api/v1/auth/change-password" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg cur "$ADMIN_PASS" '{current_password:$cur,new_password:"short"}')")"
check "Short password rejected" "$([ "$PASSWORD_SHORT" = "400" ] && echo true || echo false)"
PASSWORD_WRONG="$(status_code "$BASE/api/v1/auth/change-password" "${AUTH[@]}" -H 'Content-Type: application/json' \
  --data '{"current_password":"wrong-password","new_password":"a-valid-new-password"}')"
check "Wrong current password rejected" "$([ "$PASSWORD_WRONG" = "401" ] && echo true || echo false)"

# ==========================================================================
# Phase 3: learning catalog, roles, authoring, publication, progress, quizzes
# ==========================================================================
echo
echo "-- Phase 3: learning --"

CATALOG_JSON="$(curl -fsS "$BASE/api/v3/catalog/courses")"
CATALOG_TOPICS="$(curl -fsS "$BASE/api/v3/catalog/topics")"
CATALOG_PATHS="$(curl -fsS "$BASE/api/v3/catalog/learning-paths")"
check "Public course catalog is populated" \
  "$(echo "$CATALOG_JSON" | jq -e '.courses | length >= 8' >/dev/null && echo true || echo false)"
check "Public topic catalog is populated" \
  "$(echo "$CATALOG_TOPICS" | jq -e '.topics | length >= 8' >/dev/null && echo true || echo false)"
check "Public learning paths are populated" \
  "$(echo "$CATALOG_PATHS" | jq -e '.learning_paths | length >= 4' >/dev/null && echo true || echo false)"

PUBLIC_COURSE_SLUG="$(echo "$CATALOG_JSON" | jq -r '.courses[0].slug')"
PUBLIC_COURSE_DETAIL="$(curl -fsS "$BASE/api/v3/catalog/courses/$PUBLIC_COURSE_SLUG")"
check "Public course detail is available without a session" \
  "$(echo "$PUBLIC_COURSE_DETAIL" | jq -e '.enrollment_required == true and .curriculum.lesson_count > 0' >/dev/null && echo true || echo false)"
check "Public detail never exposes author identity" \
  "$(echo "$PUBLIC_COURSE_DETAIL" | jq -e '.course | has("author_email") | not' >/dev/null && echo true || echo false)"
check "Public detail never exposes quiz answer keys" \
  "$(echo "$PUBLIC_COURSE_DETAIL" | jq -e '[.. | objects | select(has("is_correct"))] | length == 0' >/dev/null && echo true || echo false)"
check "Public detail withholds non-preview lesson content" \
  "$(echo "$PUBLIC_COURSE_DETAIL" | jq -e '[.curriculum.modules[].lessons[] | select((.is_preview | not) and ((.content_md | length) > 0 or .is_locked != true))] | length == 0' >/dev/null && echo true || echo false)"

SITEMAP="$(curl -fsS "$BASE/sitemap.xml")"
ROBOTS="$(curl -fsS "$BASE/robots.txt")"
check "Sitemap uses the configured public origin" \
  "$(echo "$SITEMAP" | grep -Fq "<loc>https://learning.integration.test/courses/$PUBLIC_COURSE_SLUG</loc>" && echo true || echo false)"
check "Sitemap includes the public topics index" \
  "$(echo "$SITEMAP" | grep -Fq "<loc>https://learning.integration.test/topics</loc>" && echo true || echo false)"
check "Sitemap excludes account and authoring routes" \
  "$(echo "$SITEMAP" | grep -qE '/(author|my-learning|login|register)' && echo false || echo true)"
check "Robots disallows authoring and My Learning" \
  "$(echo "$ROBOTS" | grep -q '^Disallow: /author$' && echo "$ROBOTS" | grep -q '^Disallow: /my-learning$' && echo true || echo false)"
check "Robots disallows the back-office dashboard" \
  "$(echo "$ROBOTS" | grep -q '^Disallow: /dashboard$' && echo true || echo false)"
check "Robots allows all crawlers on public learning pages" \
  "$(echo "$ROBOTS" | grep -q '^User-agent: \*$' && echo "$ROBOTS" | grep -q '^Allow: /$' && echo "$ROBOTS" | grep -q '^Allow: /courses$' && echo "$ROBOTS" | grep -q '^Allow: /topics$' && echo "$ROBOTS" | grep -q '^Allow: /learning-paths$' && echo true || echo false)"
check "Robots advertises the sitemap" \
  "$(echo "$ROBOTS" | grep -Fq 'Sitemap: https://learning.integration.test/sitemap.xml' && echo true || echo false)"

# Registration always creates a learner; role selection in the body is ignored.
LEARN_EMAIL="learner-$$@integration.test"
LEARN_PASS="learner-$(openssl rand -hex 12)"
LEARN_REG="$(curl -fsS -X POST "$BASE/api/v3/auth/register" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$LEARN_EMAIL" --arg password "$LEARN_PASS" \
    '{email:$email,password:$password,role:"editor"}')")"
LEARN_TOKEN="$(echo "$LEARN_REG" | jq -r '.token')"
LEARN_AUTH=(-H "Authorization: Bearer $LEARN_TOKEN")
check "Registration forces the learner role" \
  "$(echo "$LEARN_REG" | jq -e '.user.role == "learner"' >/dev/null && echo true || echo false)"
check "Registration response contains no password material" \
  "$(echo "$LEARN_REG" | jq -e '[.. | objects | select(has("password_hash") or has("salt"))] | length == 0' >/dev/null && echo true || echo false)"
LEARN_MY_EMPTY="$(curl -fsS "$BASE/api/v3/learning/my-learning" "${LEARN_AUTH[@]}")"
check "Registration does not auto-enroll in any course" \
  "$(echo "$LEARN_MY_EMPTY" | jq -e '.enrollments | length == 0' >/dev/null && echo true || echo false)"
check "Learner cannot use authoring routes" \
  "$([ "$(status_code "$BASE/api/v3/author/courses" "${LEARN_AUTH[@]}")" = "403" ] && echo true || echo false)"
check "Learner cannot use learning administration" \
  "$([ "$(status_code "$BASE/api/v3/admin/learning/analytics" "${LEARN_AUTH[@]}")" = "403" ] && echo true || echo false)"
check "Learner cannot use back-office routes" \
  "$([ "$(status_code "$BASE/api/v1/campaigns" "${LEARN_AUTH[@]}")" = "403" ] && echo true || echo false)"
check "Back-office publisher cannot use learner routes" \
  "$([ "$(status_code "$BASE/api/v3/learning/my-learning" "${PUB_AUTH[@]}")" = "403" ] && echo true || echo false)"
check "Authoring requires authentication" \
  "$([ "$(status_code "$BASE/api/v3/author/courses")" = "401" ] && echo true || echo false)"

INSTR_EMAIL="instructor-$$@integration.test"
INSTR_PASS="instructor-$(openssl rand -hex 12)"
INSTR_CREATE="$(curl -fsS -X POST "$BASE/api/v3/admin/learning/users" "${AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$INSTR_EMAIL" --arg password "$INSTR_PASS" \
    '{email:$email,password:$password,role:"instructor"}')")"
check "Administrator creates an instructor" \
  "$(echo "$INSTR_CREATE" | jq -e '.user.role == "instructor"' >/dev/null && echo true || echo false)"
INSTR_LOGIN="$(curl -fsS -X POST "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$INSTR_EMAIL" --arg password "$INSTR_PASS" '{email:$email,password:$password}')")"
INSTR_TOKEN="$(echo "$INSTR_LOGIN" | jq -r '.token')"
INSTR_AUTH=(-H "Authorization: Bearer $INSTR_TOKEN")

INSTR2_EMAIL="instructor2-$$@integration.test"
INSTR2_PASS="instructor-$(openssl rand -hex 12)"
curl -fsS -X POST "$BASE/api/v3/admin/learning/users" "${AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$INSTR2_EMAIL" --arg password "$INSTR2_PASS" \
    '{email:$email,password:$password,role:"instructor"}')" >/dev/null
INSTR2_LOGIN="$(curl -fsS -X POST "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$INSTR2_EMAIL" --arg password "$INSTR2_PASS" '{email:$email,password:$password}')")"
INSTR2_AUTH=(-H "Authorization: Bearer $(echo "$INSTR2_LOGIN" | jq -r '.token')")

EDITOR_EMAIL="editor-$$@integration.test"
EDITOR_PASS="editor-$(openssl rand -hex 12)"
curl -fsS -X POST "$BASE/api/v3/admin/learning/users" "${AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$EDITOR_EMAIL" --arg password "$EDITOR_PASS" \
    '{email:$email,password:$password,role:"editor"}')" >/dev/null
EDITOR_LOGIN="$(curl -fsS -X POST "$BASE/api/v1/auth/login" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$EDITOR_EMAIL" --arg password "$EDITOR_PASS" '{email:$email,password:$password}')")"
EDITOR_AUTH=(-H "Authorization: Bearer $(echo "$EDITOR_LOGIN" | jq -r '.token')")
check "Instructor cannot create staff accounts" \
  "$([ "$(status_code "$BASE/api/v3/admin/learning/users" "${INSTR_AUTH[@]}" -H 'Content-Type: application/json' \
    --data "$(jq -nc --arg email "forbidden-editor-$$@integration.test" '{email:$email,password:"a-long-enough-password",role:"editor"}')")" = "403" ] && echo true || echo false)"
check "Editor cannot create administrator-owned staff accounts" \
  "$([ "$(status_code "$BASE/api/v3/admin/learning/users" "${EDITOR_AUTH[@]}" -H 'Content-Type: application/json' \
    --data "$(jq -nc --arg email "forbidden-instructor-$$@integration.test" '{email:$email,password:"a-long-enough-password",role:"instructor"}')")" = "403" ] && echo true || echo false)"

LC_SLUG="learning-it-$$"
LC_TOPIC_ID="$(echo "$CATALOG_TOPICS" | jq -r '[.topics[] | select(.status == "published")][0].id')"
LC_CREATE="$(curl -fsS -X POST "$BASE/api/v3/author/courses" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg slug "$LC_SLUG" --arg tid "$LC_TOPIC_ID" '{
    slug:$slug,
    title:"Integration Learning Course",
    summary:"A focused integration-test course about honest, read-only measurement.",
    description_md:"## Learn by inspecting\n\nUse read-only exercises and keep production campaigns unchanged.\n",
    level:"beginner",
    duration_minutes:30,
    topic_id:$tid,
    outcomes:["Inspect a report without changing production data"]
  }')")"
LC_ID="$(echo "$LC_CREATE" | jq -r '.course.id')"
check "Instructor creates a draft course" \
  "$(echo "$LC_CREATE" | jq -e '.course.status == "draft"' >/dev/null && echo true || echo false)"
check "Instructor cannot author as a learner" \
  "$([ "$(status_code "$BASE/api/v3/author/courses" "${LEARN_AUTH[@]}" -H 'Content-Type: application/json' \
    --data '{"slug":"forbidden-learner-course","title":"Forbidden","summary":"This should not be created.","description_md":"x","topic_id":"x"}')" = "403" ] && echo true || echo false)"
check "Another instructor cannot read the draft" \
  "$([ "$(status_code "$BASE/api/v3/author/courses/$LC_ID" "${INSTR2_AUTH[@]}")" = "403" ] && echo true || echo false)"
check "Another instructor cannot change the draft" \
  "$([ "$(status_code "$BASE/api/v3/author/courses/$LC_ID/status" "${INSTR2_AUTH[@]}" -H 'Content-Type: application/json' \
    --data '{"status":"in_review","note":"Not mine"}')" = "403" ] && echo true || echo false)"
check "Editor can inspect any instructor draft" \
  "$([ "$(status_code "$BASE/api/v3/author/courses/$LC_ID" "${EDITOR_AUTH[@]}")" = "200" ] && echo true || echo false)"

LC_MODULE="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/modules" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data '{"title":"Read-only inspection","summary":"Observe before changing anything.","sort_order":1}')"
LC_MODULE_ID="$(echo "$LC_MODULE" | jq -r '.module.id')"
LC_CORE_LESSON="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/lessons" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg mid "$LC_MODULE_ID" '{
    module_id:$mid,
    slug:"core-lesson",
    title:"Read a performance report",
    content_md:"# Read a report\n\nOpen the report, record a baseline, and write one hypothesis before changing anything.\n",
    estimated_minutes:8,
    sort_order:1,
    is_preview:false,
    video_provider:"",
    video_id:""
  }')")"
LC_CORE_LESSON_ID="$(echo "$LC_CORE_LESSON" | jq -r '.lesson.id')"
LC_PREVIEW_LESSON="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/lessons" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg mid "$LC_MODULE_ID" '{
    module_id:$mid,
    slug:"preview-lesson",
    title:"Preview: the measurement mindset",
    content_md:"# Measurement mindset\n\nA preview lesson is visible before enrollment, but its knowledge check stays hidden.\n",
    estimated_minutes:4,
    sort_order:2,
    is_preview:true,
    video_provider:"",
    video_id:""
  }')")"
LC_PREVIEW_LESSON_ID="$(echo "$LC_PREVIEW_LESSON" | jq -r '.lesson.id')"
LC_QUIZ="$(curl -fsS -X PUT "$BASE/api/v3/author/lessons/$LC_CORE_LESSON_ID/quiz" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data '{
    "title":"Report reading check",
    "passing_score":70,
    "questions":[{
      "prompt":"What should you do before changing a live campaign?",
      "explanation":"Record a baseline and a hypothesis so the change can be evaluated honestly.",
      "options":[
        {"text":"Record a baseline and write a hypothesis","is_correct":true},
        {"text":"Increase the budget immediately","is_correct":false}
      ]
    }]
  }')"
check "Quiz accepts exactly one answer key" \
  "$(echo "$LC_QUIZ" | jq -e '[.quiz.questions[0].options[] | select(.is_correct == true)] | length == 1' >/dev/null && echo true || echo false)"
AUTHOR_COURSE_JSON="$(curl -fsS "$BASE/api/v3/author/courses/$LC_ID" "${INSTR_AUTH[@]}")"
check "Authoring payload includes the answer key" \
  "$(echo "$AUTHOR_COURSE_JSON" | jq -e --arg lid "$LC_CORE_LESSON_ID" \
    '[.modules[].lessons[] | select(.id == $lid) | .quiz.questions[0].options[] | select(.is_correct == true)] | length == 1' >/dev/null && echo true || echo false)"
check "Authoring payload includes quiz explanations" \
  "$(echo "$AUTHOR_COURSE_JSON" | jq -e --arg lid "$LC_CORE_LESSON_ID" \
    '.modules[].lessons[] | select(.id == $lid) | (.quiz.questions[0].explanation | length) > 10' >/dev/null && echo true || echo false)"

check "Draft course is absent from the public catalog" \
  "$(curl -fsS "$BASE/api/v3/catalog/courses?limit=100" | jq -e --arg slug "$LC_SLUG" \
    '[.courses[] | select(.slug == $slug)] | length == 0' >/dev/null && echo true || echo false)"
check "Draft course detail is not public" \
  "$([ "$(status_code "$BASE/api/v3/catalog/courses/$LC_SLUG")" = "404" ] && echo true || echo false)"
check "Draft course cannot be enrolled in" \
  "$([ "$(status_code "$BASE/api/v3/learning/courses/$LC_SLUG/enroll" -X POST "${LEARN_AUTH[@]}")" = "404" ] && echo true || echo false)"
check "Draft course is absent from the sitemap" \
  "$(curl -fsS "$BASE/sitemap.xml" | grep -Fq "/courses/$LC_SLUG" && echo false || echo true)"

LC_SUBMIT="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data '{"status":"in_review","note":"Ready for editorial review"}')"
check "Instructor submits the course for review" \
  "$(echo "$LC_SUBMIT" | jq -e '.status == "in_review"' >/dev/null && echo true || echo false)"
check "Instructor cannot edit a course in review" \
  "$([ "$(status_code "$BASE/api/v3/author/courses/$LC_ID" "${INSTR_AUTH[@]}" -X PATCH \
    -H 'Content-Type: application/json' --data '{"summary":"Changed while in review"}' )" = "409" ] && echo true || echo false)"
check "Instructor cannot approve their own course" \
  "$([ "$(status_code "$BASE/api/v3/author/courses/$LC_ID/status" "${INSTR_AUTH[@]}" -X POST \
    -H 'Content-Type: application/json' --data '{"status":"approved","note":"Self approval"}')" = "403" ] && echo true || echo false)"
LC_APPROVE="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${EDITOR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"approved","note":"Editorial checks passed"}')"
check "Editor approves the course" \
  "$(echo "$LC_APPROVE" | jq -e '.status == "approved"' >/dev/null && echo true || echo false)"
LC_PUBLISH="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${EDITOR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"published","note":"Initial release"}')"
check "Editor publishes version 1" \
  "$(echo "$LC_PUBLISH" | jq -e '.status == "published" and .course.version == 1' >/dev/null && echo true || echo false)"

LC_PUBLIC="$(curl -fsS "$BASE/api/v3/catalog/courses/$LC_SLUG")"
check "Published course appears publicly" \
  "$(echo "$LC_PUBLIC" | jq -e '.course.slug == $slug and .course.version == 1' --arg slug "$LC_SLUG" >/dev/null && echo true || echo false)"
check "Published course has freshness and review dates" \
  "$(echo "$LC_PUBLIC" | jq -e '(.course.published_at | length) > 0 and (.course.last_reviewed_at | length) > 0 and (.course.next_review_due_at | length) > 0' >/dev/null && echo true || echo false)"
check "Public curriculum reveals preview content" \
  "$(echo "$LC_PUBLIC" | jq -e --arg lid "$LC_PREVIEW_LESSON_ID" \
    '[.curriculum.modules[].lessons[] | select(.id == $lid) | select(.is_locked != false or (.content_md | length) == 0)] | length == 0' >/dev/null && echo true || echo false)"
check "Public curriculum keeps the paid-path lesson locked" \
  "$(echo "$LC_PUBLIC" | jq -e --arg lid "$LC_CORE_LESSON_ID" \
    '[.curriculum.modules[].lessons[] | select(.id == $lid) | select(.is_locked != true or .content_md != "")] | length == 0' >/dev/null && echo true || echo false)"
check "Public curriculum never includes a quiz" \
  "$(echo "$LC_PUBLIC" | jq -e '[.curriculum.modules[].lessons[] | select(has("quiz"))] | length == 0' >/dev/null && echo true || echo false)"
SITEMAP="$(curl -fsS "$BASE/sitemap.xml")"
check "Published course enters the sitemap" \
  "$(echo "$SITEMAP" | grep -Fq "<loc>https://learning.integration.test/courses/$LC_SLUG</loc>" && echo true || echo false)"
check "Published course appears in the public catalog" \
  "$(curl -fsS "$BASE/api/v3/catalog/courses?limit=100" | jq -e --arg slug "$LC_SLUG" \
    '[.courses[] | select(.slug == $slug)] | length == 1' >/dev/null && echo true || echo false)"

LC_REVISIONS="$(curl -fsS "$BASE/api/v3/author/courses/$LC_ID/revisions" "${INSTR_AUTH[@]}")"
LC_REV1_ID="$(echo "$LC_REVISIONS" | jq -r '[.revisions[] | select(.version == 1)][0].id')"
LC_REV1="$(curl -fsS "$BASE/api/v3/author/courses/$LC_ID/revisions/$LC_REV1_ID" "${INSTR_AUTH[@]}")"
check "Publication creates one immutable revision" \
  "$(echo "$LC_REVISIONS" | jq -e '.revisions | length == 1 and .[0].version == 1 and .[0].note == "Initial release"' >/dev/null && echo true || echo false)"
check "Revision snapshot contains the curriculum" \
  "$(echo "$LC_REV1" | jq -e --arg slug "$LC_SLUG" \
    '.snapshot.course.slug == $slug and ([.snapshot.modules[].lessons[]] | length) == 2' >/dev/null && echo true || echo false)"

check "Enrollment requires a signed-in learning account" \
  "$([ "$(status_code "$BASE/api/v3/learning/courses/$LC_SLUG/enroll" -X POST)" = "401" ] && echo true || echo false)"
LC_ENROLL_1="$(curl -fsS -X POST "$BASE/api/v3/learning/courses/$LC_SLUG/enroll" "${LEARN_AUTH[@]}")"
LC_ENROLL_2="$(curl -fsS -X POST "$BASE/api/v3/learning/courses/$LC_SLUG/enroll" "${LEARN_AUTH[@]}")"
LC_ENROLLMENT_ID="$(echo "$LC_ENROLL_1" | jq -r '.enrollment.id')"
check "Learner enrolls in the published course" \
  "$(echo "$LC_ENROLL_1" | jq -e '.created == true' >/dev/null && echo true || echo false)"
check "Enrollment is idempotent" \
  "$(echo "$LC_ENROLL_2" | jq -e --arg id "$LC_ENROLLMENT_ID" '.created == false and .enrollment.id == $id' >/dev/null && echo true || echo false)"
LC_LEARNER_COURSE="$(curl -fsS "$BASE/api/v3/learning/courses/$LC_SLUG" "${LEARN_AUTH[@]}")"
check "Enrolled learner receives full lesson content" \
  "$(echo "$LC_LEARNER_COURSE" | jq -e '[.curriculum.modules[].lessons[] | select(.is_locked == true or (.content_md | length) == 0)] | length == 0' >/dev/null && echo true || echo false)"
check "Learner payload never exposes the answer key or explanations" \
  "$(echo "$LC_LEARNER_COURSE" | jq -e '[.. | objects | select(has("is_correct") or has("explanation"))] | length == 0' >/dev/null && echo true || echo false)"
# Read the answer key from the authoring payload only to construct test
# submissions; the learner payload above is separately asserted to omit it.
LC_QUESTION_ID="$(echo "$AUTHOR_COURSE_JSON" | jq -r --arg lid "$LC_CORE_LESSON_ID" \
  '.modules[].lessons[] | select(.id == $lid) | .quiz.questions[0].id')"
LC_CORRECT_OPTION="$(echo "$AUTHOR_COURSE_JSON" | jq -r --arg lid "$LC_CORE_LESSON_ID" \
  '[.modules[].lessons[] | select(.id == $lid) | .quiz.questions[0].options[] | select(.is_correct == true)][0].id')"
LC_WRONG_OPTION="$(echo "$AUTHOR_COURSE_JSON" | jq -r --arg lid "$LC_CORE_LESSON_ID" \
  '[.modules[].lessons[] | select(.id == $lid) | .quiz.questions[0].options[] | select(.is_correct == false)][0].id')"
check "Quiz option IDs are present for attempt testing" \
  "$([ -n "$LC_QUESTION_ID" ] && [ "$LC_CORRECT_OPTION" != "null" ] && [ "$LC_WRONG_OPTION" != "null" ] && echo true || echo false)"

LC_QUIZ_RIGHT="$(curl -fsS -X POST "$BASE/api/v3/learning/lessons/$LC_CORE_LESSON_ID/quiz/attempt" "${LEARN_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg q "$LC_QUESTION_ID" --arg o "$LC_CORRECT_OPTION" '{answers:[{question_id:$q,option_id:$o}]}')")"
check "Correct quiz answer scores 100 and passes" \
  "$(echo "$LC_QUIZ_RIGHT" | jq -e '.score == 100 and .passed == true and .correct_count == 1' >/dev/null && echo true || echo false)"
check "Graded attempt returns the answer key and explanation" \
  "$(echo "$LC_QUIZ_RIGHT" | jq -e --arg o "$LC_CORRECT_OPTION" \
    '.results[0].correct_option_id == $o and .results[0].is_correct == true and (.results[0].explanation | length) > 10' >/dev/null && echo true || echo false)"
LC_QUIZ_WRONG="$(curl -fsS -X POST "$BASE/api/v3/learning/lessons/$LC_CORE_LESSON_ID/quiz/attempt" "${LEARN_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg q "$LC_QUESTION_ID" --arg o "$LC_WRONG_OPTION" '{answers:[{question_id:$q,option_id:$o}]}')")"
check "Incorrect quiz answer scores zero and fails" \
  "$(echo "$LC_QUIZ_WRONG" | jq -e '.score == 0 and .passed == false and .correct_count == 0' >/dev/null && echo true || echo false)"
check "Quiz rejects an option from outside its question" \
  "$([ "$(status_code "$BASE/api/v3/learning/lessons/$LC_CORE_LESSON_ID/quiz/attempt" "${LEARN_AUTH[@]}" -X POST \
    -H 'Content-Type: application/json' --data '{"answers":[{"question_id":"unknown","option_id":"unknown"}]}')" = "400" ] && echo true || echo false)"

LC_COMPLETE_1="$(curl -fsS -X POST "$BASE/api/v3/learning/lessons/$LC_CORE_LESSON_ID/complete" "${LEARN_AUTH[@]}")"
LC_COMPLETE_2="$(curl -fsS -X POST "$BASE/api/v3/learning/lessons/$LC_PREVIEW_LESSON_ID/complete" "${LEARN_AUTH[@]}")"
check "First completed lesson reports 50 percent progress" \
  "$(echo "$LC_COMPLETE_1" | jq -e '.enrollment.progress_percent == 50 and .enrollment.status == "active"' >/dev/null && echo true || echo false)"
check "Completing the final lesson completes the course" \
  "$(echo "$LC_COMPLETE_2" | jq -e '.enrollment.progress_percent == 100 and .enrollment.status == "completed"' >/dev/null && echo true || echo false)"
check "My Learning reflects the completed enrollment" \
  "$(curl -fsS "$BASE/api/v3/learning/my-learning" "${LEARN_AUTH[@]}" | jq -e --arg slug "$LC_SLUG" \
    '[.enrollments[] | select(.course.slug == $slug and .status == "completed" and .progress_percent == 100)] | length == 1' >/dev/null && echo true || echo false)"

LC_ANALYTICS="$(curl -fsS "$BASE/api/v3/author/courses/$LC_ID/analytics" "${INSTR_AUTH[@]}")"
check "Course analytics are aggregate and current" \
  "$(echo "$LC_ANALYTICS" | jq -e '.enrollments == 1 and .completions == 1 and .quiz_attempts == 2' >/dev/null && echo true || echo false)"
check "Course analytics expose no named learner" \
  "$(echo "$LC_ANALYTICS" | jq -e '[.. | objects | select(has("learner_email") or has("email"))] | length == 0' >/dev/null && echo true || echo false)"
ADMIN_LEARNING="$(curl -fsS "$BASE/api/v3/admin/learning/analytics" "${AUTH[@]}")"
check "Administrator sees aggregate learning totals" \
  "$(echo "$ADMIN_LEARNING" | jq -e '.users_by_role.learners >= 1 and .users_by_role.instructors >= 2 and .users_by_role.editors >= 1 and .totals.quiz_attempts >= 2' >/dev/null && echo true || echo false)"
check "Administrator analytics do not expose learner identities" \
  "$(echo "$ADMIN_LEARNING" | jq -e '[.. | objects | select(has("learner_email") or has("learner_id"))] | length == 0' >/dev/null && echo true || echo false)"

LEARN_EXPORT="$(curl -fsS "$BASE/api/v3/learning/me/export" "${LEARN_AUTH[@]}")"
check "Learner data export includes their enrollment" \
  "$(echo "$LEARN_EXPORT" | jq -e --arg email "$LEARN_EMAIL" --arg slug "$LC_SLUG" \
    '.user.email == $email and ([.enrollments[] | select(.course.slug == $slug)] | length) == 1' >/dev/null && echo true || echo false)"
check "Learner data export contains no password material" \
  "$(echo "$LEARN_EXPORT" | jq -e '[.. | objects | select(has("password_hash") or has("salt"))] | length == 0' >/dev/null && echo true || echo false)"

# Account deletion is learner-only and revokes the session.
DELETE_EMAIL="delete-me-$$@integration.test"
DELETE_PASS="delete-me-$(openssl rand -hex 12)"
DELETE_REG="$(curl -fsS -X POST "$BASE/api/v3/auth/register" -H 'Content-Type: application/json' \
  --data "$(jq -nc --arg email "$DELETE_EMAIL" --arg password "$DELETE_PASS" '{email:$email,password:$password}')")"
DELETE_TOKEN="$(echo "$DELETE_REG" | jq -r '.token')"
DELETE_AUTH=(-H "Authorization: Bearer $DELETE_TOKEN")
check "Learner can delete their own account" \
  "$([ "$(status_code "$BASE/api/v3/learning/me" "${DELETE_AUTH[@]}" -X DELETE)" = "204" ] && echo true || echo false)"
check "Deleted learner token is revoked" \
  "$([ "$(status_code "$BASE/api/v3/learning/my-learning" "${DELETE_AUTH[@]}")" = "401" ] && echo true || echo false)"
check "Instructor cannot self-delete a staff account" \
  "$([ "$(status_code "$BASE/api/v3/learning/me" "${INSTR_AUTH[@]}" -X DELETE)" = "403" ] && echo true || echo false)"

# Unpublishing removes public access while keeping learner history. Republishing
# creates a second immutable version.
LC_UNPUBLISH="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${EDITOR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"draft","note":"Refresh before next term"}')"
check "Editor unpublishes to draft" \
  "$(echo "$LC_UNPUBLISH" | jq -e '.status == "draft"' >/dev/null && echo true || echo false)"
check "Unpublished course leaves the public catalog" \
  "$([ "$(status_code "$BASE/api/v3/catalog/courses/$LC_SLUG")" = "404" ] && echo true || echo false)"
check "Unpublished course leaves the sitemap" \
  "$(curl -fsS "$BASE/sitemap.xml" | grep -Fq "/courses/$LC_SLUG" && echo false || echo true)"
check "Unpublished course is unavailable to existing learners" \
  "$([ "$(status_code "$BASE/api/v3/learning/courses/$LC_SLUG" "${LEARN_AUTH[@]}")" = "404" ] && echo true || echo false)"
check "My Learning marks the course unavailable without deleting history" \
  "$(curl -fsS "$BASE/api/v3/learning/my-learning" "${LEARN_AUTH[@]}" | jq -e --arg slug "$LC_SLUG" \
    '[.enrollments[] | select(.course.slug == $slug and .available == false and .progress_percent == 100)] | length == 1' >/dev/null && echo true || echo false)"

LC_UPDATE="$(curl -fsS -X PATCH "$BASE/api/v3/author/courses/$LC_ID" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' \
  --data '{"summary":"Refreshed integration course summary for the next review cycle."}')"
check "Instructor can edit the course after unpublishing" \
  "$(echo "$LC_UPDATE" | jq -e '.course.summary == "Refreshed integration course summary for the next review cycle."' >/dev/null && echo true || echo false)"
curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${INSTR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"in_review","note":"Refresh ready"}' >/dev/null
curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${EDITOR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"approved","note":"Refresh approved"}' >/dev/null
LC_REPUBLISH="$(curl -fsS -X POST "$BASE/api/v3/author/courses/$LC_ID/status" "${EDITOR_AUTH[@]}" \
  -H 'Content-Type: application/json' --data '{"status":"published","note":"Second release"}')"
check "Republishing creates version 2" \
  "$(echo "$LC_REPUBLISH" | jq -e '.status == "published" and .course.version == 2' >/dev/null && echo true || echo false)"
LC_REVISIONS_2="$(curl -fsS "$BASE/api/v3/author/courses/$LC_ID/revisions" "${INSTR_AUTH[@]}")"
check "Version history retains both publications" \
  "$(echo "$LC_REVISIONS_2" | jq -e '([.revisions[].version] | sort) == [1,2]' >/dev/null && echo true || echo false)"
check "Republished course is public again" \
  "$([ "$(status_code "$BASE/api/v3/catalog/courses/$LC_SLUG")" = "200" ] && echo true || echo false)"
check "Republished course returns to the sitemap" \
  "$(curl -fsS "$BASE/sitemap.xml" | grep -Fq "/courses/$LC_SLUG" && echo true || echo false)"
check "Existing learner can resume the republished course" \
  "$([ "$(status_code "$BASE/api/v3/learning/courses/$LC_SLUG" "${LEARN_AUTH[@]}")" = "200" ] && echo true || echo false)"

# Restart to force the startup online backup, then verify its integrity and content.
docker restart "$NAME" >/dev/null
READY2=0
for _ in $(seq 1 40); do
  if curl -fsS "$BASE/readyz" >/dev/null 2>&1; then READY2=1; break; fi
  sleep 0.5
done
check "API restarts after backup" "$READY2"
BACKUP_FILE=""
for _ in $(seq 1 40); do
  BACKUP_FILE="$(find "$IT_DIR/data/backups" -maxdepth 1 -name 'adcontrol-*.db' -type f 2>/dev/null | sort | tail -1)"
  [ -n "$BACKUP_FILE" ] && break
  sleep 0.5
done
check "Startup online backup exists" "$([ -n "$BACKUP_FILE" ] && echo true || echo false)"
if [ -n "$BACKUP_FILE" ]; then
  BACKUP_NAME="$(basename "$BACKUP_FILE")"
  sqlite_run() {
    docker run --rm --user "$(id -u):$(id -g)" -v "$IT_DIR/data:/data:ro" \
      --entrypoint sqlite3 adcontrol-api:latest "file:/data/backups/$BACKUP_NAME?immutable=1" "$1"
  }
  BACKUP_INTEGRITY="$(sqlite_run 'PRAGMA integrity_check;')"
  check "Backup passes integrity_check" "$([ "$BACKUP_INTEGRITY" = "ok" ] && echo true || echo false)"
  BACKUP_SITES="$(sqlite_run 'SELECT COUNT(*) FROM sites;')"
  check "Backup contains test data" "$([ "$BACKUP_SITES" -ge 1 ] && echo true || echo false)"
fi

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
