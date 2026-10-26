#!/usr/bin/env bash
#
# On-demand, application-consistent SQLite backup for AdControl.
#
# The API also takes automatic backups using SQLite's online backup API
# (BACKUP_INTERVAL_HOURS). This script is for backups before upgrades,
# manual snapshots, and copying off the host.
#
# Usage:
#   ./scripts/backup.sh [output-directory]
#
# Environment:
#   COMPOSE_FILE   Path to docker-compose.yml
#   SERVICE        Compose service name (default: api)
#   DB_PATH        Database path inside the container (default: /data/adcontrol.db)
#   BACKUP_RETENTION  Number of manual backups to keep (default: 30)
#
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
COMPOSE_FILE="${COMPOSE_FILE:-$ROOT/deploy/docker-compose.yml}"
SERVICE="${SERVICE:-api}"
DB_PATH="${DB_PATH:-/data/adcontrol.db}"
OUTPUT_DIR="${1:-$ROOT/data/backups}"
RETENTION="${BACKUP_RETENTION:-30}"
STAMP="$(date -u +%Y%m%d-%H%M%S)"
CONTAINER_TMP="/data/backups/.manual-$STAMP.db"
HOST_TMP="$OUTPUT_DIR/.manual-$STAMP.db"
FINAL="$OUTPUT_DIR/adcontrol-manual-$STAMP.db.gz"

compose=(docker compose -f "$COMPOSE_FILE")

die() { echo "ERROR: $*" >&2; exit 1; }
cleanup() {
    "${compose[@]}" exec -T "$SERVICE" rm -f "$CONTAINER_TMP" >/dev/null 2>&1 || true
    rm -f "$HOST_TMP" "$HOST_TMP.gz" 2>/dev/null || true
}
trap cleanup EXIT

command -v docker >/dev/null 2>&1 || die "docker is required"
command -v gzip >/dev/null 2>&1 || die "gzip is required"
[[ -f "$COMPOSE_FILE" ]] || die "compose file not found: $COMPOSE_FILE"
mkdir -p "$OUTPUT_DIR"
chmod 700 "$OUTPUT_DIR"

"${compose[@]}" ps --status running --services 2>/dev/null | grep -qx "$SERVICE" \
    || die "service '$SERVICE' is not running (docker compose -f $COMPOSE_FILE up -d)"

echo "[backup] Snapshotting $DB_PATH -> $CONTAINER_TMP"
"${compose[@]}" exec -T "$SERVICE" sh -c 'mkdir -p /data/backups && chmod 700 /data/backups'
"${compose[@]}" exec -T "$SERVICE" \
    sqlite3 "$DB_PATH" ".timeout 5000" ".backup '$CONTAINER_TMP'"

echo "[backup] Verifying integrity"
CHECK="$("${compose[@]}" exec -T "$SERVICE" sqlite3 "$CONTAINER_TMP" 'PRAGMA integrity_check;' | tr -d '\r')"
[[ "$CHECK" == "ok" ]] || die "integrity check failed: $CHECK"

echo "[backup] Copying to host"
"${compose[@]}" cp "$SERVICE:$CONTAINER_TMP" "$HOST_TMP"
chmod 600 "$HOST_TMP"
gzip -9 "$HOST_TMP"
mv "$HOST_TMP.gz" "$FINAL"
chmod 600 "$FINAL"

# Keep only the newest manual backups. Automatic backups are pruned by the API.
mapfile -t BACKUPS < <(find "$OUTPUT_DIR" -maxdepth 1 -type f -name 'adcontrol-manual-*.db.gz' -print | sort)
if (( ${#BACKUPS[@]} > RETENTION )); then
    for old in "${BACKUPS[@]:0:${#BACKUPS[@]}-RETENTION}"; do
        rm -f -- "$old"
        echo "[backup] Pruned $old"
    done
fi

echo "[backup] OK: $FINAL"
echo "[backup] Copy this file off the host (and encrypt it at rest):"
echo "         restic backup '$FINAL'   # or rclone/rsync/age/gpg"
