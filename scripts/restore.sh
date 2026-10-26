#!/usr/bin/env bash
#
# Restore an AdControl SQLite backup.
#
# Usage:
#   ./scripts/restore.sh <backup.db|backup.db.gz> [--yes]
#
# The script:
#   1. verifies the backup's SQLite integrity,
#   2. stops the API,
#   3. snapshots the current database (pre-restore-*.db) in /data/backups,
#   4. restores the backup and removes stale -wal/-shm files,
#   5. starts the API and waits for /readyz.
#
# This is destructive. Run a backup and test a restore in a staging
# environment before relying on this procedure.
#
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
COMPOSE_FILE="${COMPOSE_FILE:-$ROOT/deploy/docker-compose.yml}"
SERVICE="${SERVICE:-api}"
DATA_DIR="${ADCONTROL_DATA_DIR:-$ROOT/data}"
STAMP="$(date -u +%Y%m%d-%H%M%S)"

BACKUP_FILE="${1:-}"
ASSUME_YES="${2:-}"

die() { echo "ERROR: $*" >&2; exit 1; }
usage() {
    echo "Usage: $0 <backup.db|backup.db.gz> [--yes]" >&2
    exit 2
}

[[ -n "$BACKUP_FILE" ]] || usage
[[ -f "$BACKUP_FILE" ]] || die "backup file not found: $BACKUP_FILE"
[[ "$ASSUME_YES" == "" || "$ASSUME_YES" == "--yes" ]] || usage
command -v docker >/dev/null 2>&1 || die "docker is required"
[[ -f "$COMPOSE_FILE" ]] || die "compose file not found: $COMPOSE_FILE"
[[ -d "$DATA_DIR" ]] || die "data directory not found: $DATA_DIR"

compose=(docker compose -f "$COMPOSE_FILE")
BACKUP_ABS="$(cd -- "$(dirname -- "$BACKUP_FILE")" && pwd)/$(basename -- "$BACKUP_FILE")"
TMP_DB=""
cleanup() { [[ -n "$TMP_DB" ]] && rm -f -- "$TMP_DB"; }
trap cleanup EXIT

# --- Verify the backup before touching the live database -------------------
if [[ "$BACKUP_ABS" == *.gz ]]; then
    command -v gzip >/dev/null 2>&1 || die "gzip is required to restore .gz backups"
    gzip -t "$BACKUP_ABS" || die "gzip integrity check failed"
    TMP_DB="$(mktemp "${TMPDIR:-/tmp}/adcontrol-restore-XXXXXX.db")"
    gzip -dc "$BACKUP_ABS" > "$TMP_DB"
else
    TMP_DB="$(mktemp "${TMPDIR:-/tmp}/adcontrol-restore-XXXXXX.db")"
    cp -- "$BACKUP_ABS" "$TMP_DB"
fi
chmod 600 "$TMP_DB"

# Sized backup should be a SQLite database.
[[ -s "$TMP_DB" ]] || die "backup is empty"
if [[ "$(head -c 15 "$TMP_DB")" != "SQLite format 3" ]]; then
    die "not a SQLite database: $BACKUP_ABS"
fi

echo "[restore] Verifying backup integrity"
# Open the verified snapshot as an immutable database: the read-only bind
# mount may not permit SQLite to create the -shm file needed by WAL mode.
CHECK="$("${compose[@]}" run --rm --no-deps -T --entrypoint sh \
    -v "$TMP_DB:/restore/backup.db:ro" "$SERVICE" \
    -c "sqlite3 'file:/restore/backup.db?immutable=1' 'PRAGMA integrity_check;'" | tr -d '\r')"
[[ "$CHECK" == "ok" ]] || die "integrity check failed: $CHECK"

if [[ "$ASSUME_YES" != "--yes" ]]; then
    echo
    echo "This will REPLACE the live database for compose project:"
    echo "  compose file: $COMPOSE_FILE"
    echo "  data dir:     $DATA_DIR"
    echo "  backup:       $BACKUP_ABS"
    echo
    read -r -p "Type 'restore' to continue: " CONFIRM
    [[ "$CONFIRM" == "restore" ]] || die "aborted"
fi

# --- Stop the API so no writer is active -----------------------------------
echo "[restore] Stopping $SERVICE"
"${compose[@]}" stop "$SERVICE" >/dev/null

# --- Snapshot the current database -----------------------------------------
PRE_RESTORE="/data/backups/pre-restore-$STAMP.db"
echo "[restore] Saving current database to $PRE_RESTORE"
"${compose[@]}" run --rm --no-deps -T --entrypoint sh "$SERVICE" -c \
    "mkdir -p /data/backups && chmod 700 /data/backups && if [ -f /data/adcontrol.db ]; then sqlite3 /data/adcontrol.db '.timeout 5000' \".backup '$PRE_RESTORE'\" && chmod 600 '$PRE_RESTORE'; else echo 'no existing database'; fi"

# --- Restore ---------------------------------------------------------------
echo "[restore] Installing backup"
"${compose[@]}" run --rm --no-deps -T --entrypoint sh \
    -v "$TMP_DB:/restore/backup.db:ro" "$SERVICE" -c \
    "cp /restore/backup.db /data/adcontrol.db && rm -f /data/adcontrol.db-wal /data/adcontrol.db-shm && chmod 600 /data/adcontrol.db"

# --- Start and verify ------------------------------------------------------
echo "[restore] Starting $SERVICE"
"${compose[@]}" up -d --no-deps "$SERVICE" >/dev/null

READY=0
for _ in $(seq 1 30); do
    if "${compose[@]}" exec -T "$SERVICE" curl -fsS http://127.0.0.1:8088/readyz >/dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 1
done

if [[ "$READY" == "1" ]]; then
    echo "[restore] OK: API is ready."
    echo "[restore] Previous database snapshot: $DATA_DIR/backups/pre-restore-$STAMP.db"
else
    echo "ERROR: API did not become ready. Check: docker compose -f '$COMPOSE_FILE' logs $SERVICE" >&2
    echo "       Previous database snapshot: $DATA_DIR/backups/pre-restore-$STAMP.db" >&2
    exit 1
fi
