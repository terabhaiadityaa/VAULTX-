# VAULTX — SQLite to Supabase PostgreSQL Migration Runbook

> **Status:** Prepared locally (Not yet executed).  
> **Scope:** Database migration only. The C++17 backend (`backend/`) and local `database/atm.db` remain completely untouched.

---

## 1. Prerequisites & Required Environment Variables

| Variable | Required? | Where to Obtain | Example Format |
|---|---|---|---|
| `TARGET_DATABASE_URL` | **Yes** | Supabase Dashboard $\rightarrow$ **Project Settings** $\rightarrow$ **Database** $\rightarrow$ **Connection String (URI)** | `postgresql://postgres.[ref]:[YOUR-PASSWORD]@aws-0-[region].pooler.supabase.com:5432/postgres?sslmode=require` |
| `SOURCE_SQLITE_PATH` | Optional | Defaults automatically to `database/atm.db` | `database/atm.db` |

> [!CAUTION]
> **Never commit `TARGET_DATABASE_URL` or your database password to Git.** Pass it only in your terminal session environment when executing the migration.

### Python Runtime Dependencies (Already Verified Locally)
- **Python**: `3.12.14` (installed)
- **PostgreSQL Driver**: `psycopg 3.3.8` (already installed locally)

---

## 2. Exact Commands to Execute When Approved (Approval Gate B)

### Step A: Stop Local Backend & Create Verified Backup Outside Git
Before taking the backup, stop any locally running `atm_backend` process so `database/atm.db` stays completely idle at **40 users, 40 accounts, 132 transactions, and 3 login attempts**.

Backups are stored **outside** the Git repository (`$HOME/vaultx_backups`) so sensitive `.sql` dumps (containing cardholder emails and PIN hashes) can never accidentally be committed to Git.

Run in macOS Zsh:

```zsh
# 1. Stop local atm_backend if running (ignore if not running)
pkill -x atm_backend 2>/dev/null || true

# 2. Prepare external backup folder outside Git repository
BACKUP_DIR="$HOME/vaultx_backups"
mkdir -p "$BACKUP_DIR"

# 3. Capture one single timestamp so .db and .sql filenames always match
cd "/Users/aditya/first project/ATM-Transaction-Simulation"
TS="$(date +%Y%m%d_%H%M%S)"

# 4. Create consistent SQLite online backup (.backup) and textual SQL dump (.dump)
sqlite3 "database/atm.db" ".backup '${BACKUP_DIR}/atm_pre_supabase_${TS}.db'"
sqlite3 "database/atm.db" ".dump" > "${BACKUP_DIR}/atm_pre_supabase_${TS}.sql"

# 5. Verify both backup files exist, pass SQLite integrity check, and record SHA-256
ls -lh "${BACKUP_DIR}/atm_pre_supabase_${TS}."*
sqlite3 "${BACKUP_DIR}/atm_pre_supabase_${TS}.db" "PRAGMA integrity_check;"
tail -n 1 "${BACKUP_DIR}/atm_pre_supabase_${TS}.sql"
shasum -a 256 "database/atm.db"
```
*(Expected verification output: `ok` from `PRAGMA integrity_check;` and `COMMIT;` on the last line of the `.sql` dump.)*

### Step B: Read-Only Pre-Check in Supabase SQL Editor
Verify in the Supabase Dashboard **SQL Editor** that the 3 existing tables are indeed empty (`0` rows):

```sql
SELECT 'users' AS tbl, count(*) FROM public.users
UNION ALL
SELECT 'accounts', count(*) FROM public.accounts
UNION ALL
SELECT 'transactions', count(*) FROM public.transactions;
```

### Step C: Run the Migration Script (macOS Zsh Compatible)
Enter your Supabase URI securely in your terminal without printing it or saving it to shell history, then run the migration script:

```zsh
cd "/Users/aditya/first project/ATM-Transaction-Simulation"
printf "Paste Supabase URI: " && IFS= read -rs TARGET_DATABASE_URL && echo
export TARGET_DATABASE_URL
python3 migration/migrate_sqlite_to_supabase.py
unset TARGET_DATABASE_URL
```

### Step D: Post-Migration Verification in Supabase
Paste the contents of [`migration/verify_supabase.sql`](file:///Users/aditya/first%20project/ATM-Transaction-Simulation/migration/verify_supabase.sql) into the Supabase **SQL Editor** and run it to confirm:
- `users`: 40 rows
- `accounts`: 40 rows (`685449100` paise total)
- `transactions`: 132 rows (`702263500` paise total)
- `login_attempts`: 3 rows
- `unpaired_transfers`: 0
- `rowsecurity`: `true` on all 4 tables
- `legacy_backup_*` schema contains the 3 preserved original empty tables
