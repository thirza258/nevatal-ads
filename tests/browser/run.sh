#!/usr/bin/env bash
#
# Browser end-to-end and strict-CSP suites for the AdControl SDK.
#
# These suites log in as the admin, mutate the seeded test site/campaign, and
# must only run against a local or staging stack, never production.
#
# Prerequisites:
#   1. The Compose stack is running and healthy.
#   2. `bash scripts/test-stack.sh` has seeded the test site and written
#      $ADCONTROL_TEST_IDS (default /tmp/adcontrol_stack_ids).
#   3. The API has been restarted after that suite to clear the login
#      rate-limit bucket.
#
# Usage: bash tests/browser/run.sh
#
# Optional environment variables:
#   ADCONTROL_TEST_PORT        Web port (defaults to HTTP_PORT from deploy/.env)
#   ADCONTROL_BASE            Full base URL override
#   ADCONTROL_TEST_IDS        IDs file written by scripts/test-stack.sh
#   ADCONTROL_PLAYWRIGHT_IMAGE
#   HTTP_PORT                 Used when ADCONTROL_TEST_PORT is unset
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
ENV_FILE="$ROOT/deploy/.env"
IDS_FILE="${ADCONTROL_TEST_IDS:-/tmp/adcontrol_stack_ids}"
IMAGE="${ADCONTROL_PLAYWRIGHT_IMAGE:-mcr.microsoft.com/playwright:v1.64.1-noble}"

if [ ! -f "$ENV_FILE" ]; then
  echo "ERROR: $ENV_FILE is missing. Copy deploy/.env.example and configure it." >&2
  exit 1
fi
if [ ! -s "$IDS_FILE" ]; then
  echo "ERROR: $IDS_FILE is missing or empty." >&2
  echo "Run 'bash scripts/test-stack.sh' first, then restart the API." >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

PORT="${ADCONTROL_TEST_PORT:-${HTTP_PORT:-18080}}"
BASE="${ADCONTROL_BASE:-http://127.0.0.1:${PORT}}"

if ! curl -fsS --max-time 5 "$BASE/readyz" >/dev/null; then
  echo "ERROR: $BASE/readyz is not ready. Is the stack running?" >&2
  exit 1
fi

echo "Running AdControl browser suites against $BASE"
echo "These suites mutate the test site and analytics; do not target production."
echo

# Host networking lets Chromium reach the loopback-bound web container using the
# same 127.0.0.1 origin that scripts/test-stack.sh added to the site allowlist.
# The repository, database, and .env file are not mounted into the container;
# only the read-only test files and seeded IDs file are. The container receives
# just the admin credentials it needs, not SECRET_KEY.
docker run --rm \
  --network host \
  -v "$SCRIPT_DIR:/tests:ro" \
  -v "$IDS_FILE:/run/adcontrol_stack_ids:ro" \
  -e ADMIN_EMAIL -e ADMIN_PASSWORD \
  -e ADCONTROL_IDS_FILE=/run/adcontrol_stack_ids \
  -e ADCONTROL_BASE="$BASE" \
  -e ADCONTROL_TEST_PORT="$PORT" \
  "$IMAGE" bash -c '
    set -e
    work="$(mktemp -d)"
    trap "rm -rf \"$work\"" EXIT
    cp /tests/package.json /tests/package-lock.json \
       /tests/sdk-e2e.js /tests/csp-strict.js /tests/learning-smoke.js "$work"/
    cd "$work"
    npm ci --no-audit --no-fund --loglevel=error

    set +e
    echo "=== SDK end-to-end suite ==="
    node sdk-e2e.js
    sdk_status=$?
    echo
    echo "=== Strict-CSP suite ==="
    node csp-strict.js
    csp_status=$?
    echo
    echo "=== Public learning smoke suite ==="
    node learning-smoke.js
    learning_status=$?
    set -e

    echo
    echo "Browser suites: sdk-e2e=$sdk_status csp-strict=$csp_status learning=$learning_status (0 = pass)"
    [ "$sdk_status" -eq 0 ] && [ "$csp_status" -eq 0 ] && [ "$learning_status" -eq 0 ]
  '
