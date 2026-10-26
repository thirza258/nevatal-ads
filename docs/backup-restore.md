# Database backups, restore, and storage redundancy

AdControl stores all state in one SQLite database (`/data/adcontrol.db` inside the
API container, `./data/adcontrol.db` on the host). It runs in WAL mode, so copying
the `.db` file alone while the service is running is **not** a safe backup: recent
commits may live in `adcontrol.db-wal`.

The project provides two safe mechanisms:

1. **Automatic online backups** performed by the API using SQLite's backup API.
2. **On-demand backups** via `scripts/backup.sh`, using `sqlite3 .backup`.

Both produce a consistent snapshot while the service is running, then verify it
with `PRAGMA integrity_check`.

## Automatic backups

The API runs a maintenance thread. Every `BACKUP_INTERVAL_HOURS` (default 24) it
writes a verified backup to `BACKUP_DIR` (default `/data/backups`, host
`./data/backups`):

```
data/backups/adcontrol-YYYYMMDD-HHMMSS.db
```

- The temporary file is written first and atomically renamed only after the
  integrity check passes.
- The newest `BACKUP_RETENTION` files are kept (default 14).
- A backup is taken on startup and then on the configured interval.
- Set `BACKUP_INTERVAL_HOURS=0` to disable automatic backups.
- Backups contain password hashes, audit logs, site keys, campaign data, and
  learning accounts, enrollments, progress, quiz attempts, and revisions:
  protect them like the live database (`chmod 600`, encrypted disk/backups).

Configuration lives in `deploy/.env`:

```dotenv
BACKUP_INTERVAL_HOURS=24
BACKUP_RETENTION=14
```

Changing `BACKUP_DIR` requires the new path to be writable and persisted (add a
volume or keep it under `/data`).

## On-demand backup

```bash
cd /path/to/nevatal-ads
cp deploy/.env.example deploy/.env    # first time only; edit secrets
docker compose -f deploy/docker-compose.yml up -d --build
./scripts/backup.sh                   # writes data/backups/adcontrol-manual-*.db.gz
```

The script:

1. runs `sqlite3 .backup` inside the running API container,
2. verifies `PRAGMA integrity_check`,
3. copies the snapshot to the host and gzip-compresses it (`0600`),
4. prunes older manual backups (`BACKUP_RETENTION=30` by default for manual ones).

It does not contact the internet or upload anything.

## Off-host copies (required)

A backup on the same disk does not survive disk failure, ransomware, or an
accidental `rm -rf`. Copy backups off the host on a schedule, for example:

```bash
# Encrypted, deduplicated, versioned offsite backup
restic -r s3:s3.example.com/adcontrol init
restic -r s3:s3.example.com/adcontrol backup ./data/backups
restic -r s3:s3.example.com/adcontrol forget --keep-daily 7 --keep-weekly 4 --keep-monthly 12 --prune
```

Alternatives: `rclone`, `rsync` to another machine, `age`/`gpg` encrypted
archives, or filesystem snapshots replicated to another host. Follow a 3-2-1
rule: three copies, two media, one offsite. Test restores quarterly.

## Restore

> Restoring replaces the live database. Take a fresh backup first if the current
> database is still readable.

```bash
./scripts/restore.sh data/backups/adcontrol-manual-20260101-030000.db.gz
# or non-interactive:
./scripts/restore.sh data/backups/adcontrol-20260101-030000.db --yes
```

The script verifies the backup with `PRAGMA integrity_check`, stops the API,
saves the current database as `data/backups/pre-restore-<timestamp>.db`, removes
stale `-wal`/`-shm` files, installs the backup, starts the API, and waits for
`/readyz`.

After a restore:

- Verify login, site keys, and campaign data in the dashboard.
- Verify learning catalog and enrollment data (for example `/`,
  `/api/v3/catalog/courses`, and a test learner's My Learning page). A restored
  database may reintroduce a learner account that was deleted after the backup;
  delete it again if that deletion was intentional.
- If `SECRET_KEY` changed since the backup, outstanding click tokens are invalid
  and IP hashes from before the change no longer correlate (this is expected).
  Admin sessions are database rows and remain valid unless the password was
  changed; delete them by changing the password if needed. Issue fresh click
  tokens by reloading host pages.
- Run `./scripts/backup.sh` immediately after confirming the restore.

### Manual restore (if scripts are unavailable)

```bash
docker compose -f deploy/docker-compose.yml stop api
cp data/adcontrol.db data/adcontrol.db.before-restore
sqlite3 /path/to/verified-backup.db "PRAGMA integrity_check;"   # must print ok
cp /path/to/verified-backup.db data/adcontrol.db
rm -f data/adcontrol.db-wal data/adcontrol.db-shm
docker compose -f deploy/docker-compose.yml up -d api
curl -fsS http://127.0.0.1:18080/readyz
```

Never restore by copying only the `.db` while `-wal` files from a different
database remain: SQLite can apply them and corrupt the restored state.

## Backup verification

At minimum, periodically:

```bash
docker compose -f deploy/docker-compose.yml run --rm --no-deps --entrypoint sh api \
  -c "sqlite3 /data/backups/adcontrol-20260101-030000.db 'PRAGMA integrity_check;'"
```

A stronger drill restores into a separate staging directory and starts a second
compose project against it, then checks login and analytics. Do not point a
second writer at the production database file.

## RAID and storage redundancy

RAID is **not** a backup. RAID protects against a disk dying; it does not protect
against:

- accidental deletion or a bad migration (the damage is mirrored instantly),
- filesystem or SQLite corruption,
- ransomware or a compromised host,
- site loss (fire, theft, cloud account loss).

Recommended layering for a self-hosted deployment:

1. **Storage redundancy:** put `./data` on RAID 1 (mirror) or a ZFS/btrfs mirror.
   For SQLite, use local SSD/NVMe rather than NFS/SMB; network filesystems can
   break SQLite locking and WAL semantics.
2. **Filesystem snapshots:** LVM/ZFS/btrfs snapshots of `./data` on a schedule,
   preferably after `scripts/backup.sh` or with SQLite's backup API as the
   source of truth.
3. **Application backups:** keep `BACKUP_INTERVAL_HOURS` enabled. These are
   verified and independent of the storage layer.
4. **Offsite copies:** replicate encrypted backups to different media/accounts,
   with retention and restore tests.

RAID 5/6 with large disks has long rebuild windows; RAID 1 or RAID 10 is a
simpler choice for a small deployment. A UPS is also worthwhile: sudden power
loss is a common source of filesystem damage, although SQLite WAL with
`synchronous=NORMAL` tolerates it reasonably well.

If the host itself is a single point of failure, run the compose stack on a
second host from the same backup and DNS failover plan, rather than relying on
RAID alone.

## Retention and deletion

- Event retention: `EVENT_RETENTION_DAYS` (default 180). The maintenance thread
  purges older events hourly. Lower it if your privacy policy requires shorter
  analytics retention; backups taken before the purge still contain old events,
  so expire offsite backups on a matching schedule.
- Learning data is not covered by `EVENT_RETENTION_DAYS`. Learner account
  deletion removes the live row and cascades enrollments, progress, and
  attempts, but backups taken earlier may still contain them until those backups
  expire. Document this in the learner privacy notice and align backup retention
  with your deletion commitments.
- Deleting a site/placement/campaign that has analytics history is blocked or
  converted to an archive, so reports and backups stay internally consistent.
- To delete all data, stop the stack, remove `data/adcontrol.db`, `-wal`, `-shm`,
  and all backups. Rotate `SECRET_KEY`.

## What is not included

- No cloud backup provider, S3 bucket, or credentials ship with the project.
- No automatic restore: restore is deliberately a manual, confirmed operation.
- No RAID configuration is applied by these scripts; RAID/snapshots are a host
  or hypervisor responsibility.
