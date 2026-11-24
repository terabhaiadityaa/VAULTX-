#!/usr/bin/env python3
"""
VAULTX — SQLite to Supabase PostgreSQL Migration Script
=======================================================
Safely migrates local SQLite data (users, accounts, transactions, login_attempts)
into Supabase PostgreSQL inside a single atomic transaction.

Safety Guarantees:
1. Opens local SQLite strictly read-only (?mode=ro) and checks SHA-256 before/after.
2. Pre-flight idempotency guard aborts before any DDL if production/non-empty tables exist.
3. Isolates any existing empty legacy tables (and their constraints, indexes, sequences,
   triggers, and RLS policies) into a unique timestamped backup schema (legacy_backup_<ts>).
4. Enables Row Level Security (RLS) and revokes anon/authenticated PostgREST privileges.
5. Preserves exact integer IDs, integer paise balances/amounts, PBKDF2 hashes, and UTC timestamps.
6. Synchronizes PostgreSQL identity sequences so future inserts never collide.
7. Performs 100% row-by-row verification of every column before COMMIT (full ROLLBACK on failure).

Never hardcodes database credentials; reads TARGET_DATABASE_URL from the environment.
"""

from __future__ import annotations

import hashlib
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
import sqlite3

try:
    import psycopg
    from psycopg import sql
except ImportError:
    print("[ERROR] Python package 'psycopg' (v3) is required.")
    print("        Install it with: pip install 'psycopg[binary]'")
    sys.exit(1)


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SQLITE_PATH = REPO_ROOT / "database" / "atm.db"

# Expected baseline invariants from the audited SQLite database
EXPECTED_USERS = 40
EXPECTED_ACCOUNTS = 40
EXPECTED_TRANSACTIONS = 132
EXPECTED_LOGIN_ATTEMPTS = 3
EXPECTED_BALANCE_CHECKSUM = 685_449_100
EXPECTED_AMOUNT_CHECKSUM = 702_263_500

LEGACY_TABLES = ("users", "accounts", "transactions")
LEGACY_SEQUENCES = ("users_id_seq", "accounts_id_seq", "transactions_id_seq")
PRODUCTION_INDEXES = (
    "idx_accounts_user_id",
    "idx_accounts_account_number",
    "idx_transactions_account_id",
    "idx_transactions_created_at",
    "idx_transactions_related_account_id",
)
PRODUCTION_CONSTRAINT_INDEXES = (
    "users_pkey",
    "users_email_key",
    "accounts_pkey",
    "accounts_account_number_key",
    "transactions_pkey",
    "login_attempts_pkey",
)


def compute_sha256(file_path: Path) -> str:
    """Compute SHA-256 hex digest of a file without modifying it."""
    digest = hashlib.sha256()
    with file_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_utc_timestamp(ts_value: str) -> str:
    """
    Convert SQLite CURRENT_TIMESTAMP text ('YYYY-MM-DD HH:MM:SS') into an explicit
    UTC ISO-8601 string ('YYYY-MM-DD HH:MM:SS+00:00') so TIMESTAMPTZ never shifts.
    """
    if not ts_value or not isinstance(ts_value, str):
        raise ValueError(f"Invalid timestamp value encountered: {ts_value!r}")
    cleaned = ts_value.strip()
    parsed = datetime.strptime(cleaned, "%Y-%m-%d %H:%M:%S")
    return parsed.strftime("%Y-%m-%d %H:%M:%S+00:00")


def load_sqlite_snapshot(sqlite_path: Path) -> dict:
    """Read and validate the entire SQLite database in strict read-only mode."""
    if not sqlite_path.exists():
        raise FileNotFoundError(f"SQLite database not found: {sqlite_path}")

    uri = f"file:{sqlite_path.resolve()}?mode=ro"
    conn = sqlite3.connect(uri, uri=True)
    try:
        cur = conn.cursor()

        users = cur.execute(
            "SELECT id, full_name, email, pin_hash, created_at FROM users ORDER BY id ASC;"
        ).fetchall()

        accounts = cur.execute(
            "SELECT id, user_id, account_number, balance_paise, status, created_at "
            "FROM accounts ORDER BY id ASC;"
        ).fetchall()

        transactions = cur.execute(
            "SELECT id, account_id, transaction_type, amount_paise, related_account_id, "
            "description, created_at FROM transactions ORDER BY id ASC;"
        ).fetchall()

        login_attempts = cur.execute(
            "SELECT email, failed_attempts, locked_until, updated_at "
            "FROM login_attempts ORDER BY email ASC;"
        ).fetchall()

        # Verify expected baseline counts & financial checksums
        assert len(users) == EXPECTED_USERS, (
            f"SQLite users count mismatch: expected {EXPECTED_USERS}, found {len(users)}"
        )
        assert len(accounts) == EXPECTED_ACCOUNTS, (
            f"SQLite accounts count mismatch: expected {EXPECTED_ACCOUNTS}, found {len(accounts)}"
        )
        assert len(transactions) == EXPECTED_TRANSACTIONS, (
            f"SQLite transactions count mismatch: expected {EXPECTED_TRANSACTIONS}, found {len(transactions)}"
        )
        assert len(login_attempts) == EXPECTED_LOGIN_ATTEMPTS, (
            f"SQLite login_attempts count mismatch: expected {EXPECTED_LOGIN_ATTEMPTS}, found {len(login_attempts)}"
        )

        balance_sum = sum(row[3] for row in accounts)
        amount_sum = sum(row[3] for row in transactions)
        assert balance_sum == EXPECTED_BALANCE_CHECKSUM, (
            f"SQLite balance checksum mismatch: expected {EXPECTED_BALANCE_CHECKSUM}, found {balance_sum}"
        )
        assert amount_sum == EXPECTED_AMOUNT_CHECKSUM, (
            f"SQLite amount checksum mismatch: expected {EXPECTED_AMOUNT_CHECKSUM}, found {amount_sum}"
        )

        # Verify zero orphan foreign keys in source
        user_ids = {row[0] for row in users}
        account_ids = {row[0] for row in accounts}
        for acc in accounts:
            assert acc[1] in user_ids, f"Orphan account {acc[0]} references missing user {acc[1]}"
        for tx in transactions:
            assert tx[1] in account_ids, f"Orphan tx {tx[0]} references missing account {tx[1]}"
            if tx[4] is not None:
                assert tx[4] in account_ids, f"Orphan tx {tx[0]} references missing related_account {tx[4]}"

        return {
            "users": users,
            "accounts": accounts,
            "transactions": transactions,
            "login_attempts": login_attempts,
            "balance_sum": balance_sum,
            "amount_sum": amount_sum,
        }
    finally:
        conn.close()


def preflight_guard(pg_cur: psycopg.Cursor) -> list[str]:
    """
    Verify target Supabase database is safe for initial migration.
    Aborts if the migration appears already completed, if non-empty tables exist,
    or if external Views/FKs depend on the legacy tables.
    Returns list of existing empty legacy tables in public schema to isolate.
    """
    pg_cur.execute(
        """
        SELECT table_name
        FROM information_schema.tables
        WHERE table_schema = 'public'
          AND table_name IN ('users', 'accounts', 'transactions', 'login_attempts')
        ORDER BY table_name;
        """
    )
    existing_tables = [row[0] for row in pg_cur.fetchall()]

    if "login_attempts" in existing_tables:
        raise RuntimeError(
            "Idempotency guard triggered: public.login_attempts already exists. "
            "Database appears already migrated. Aborting without changes."
        )

    # Check if public.accounts already matches target schema
    if "accounts" in existing_tables:
        pg_cur.execute(
            """
            SELECT 1
            FROM information_schema.columns
            WHERE table_schema = 'public'
              AND table_name = 'accounts'
              AND column_name = 'balance_paise';
            """
        )
        if pg_cur.fetchone() is not None:
            raise RuntimeError(
                "Idempotency guard triggered: public.accounts already has column 'balance_paise'. "
                "Refusing to re-run or overwrite migrated schema."
            )

    # Ensure no existing legacy table contains rows
    for tbl in existing_tables:
        pg_cur.execute(sql.SQL("SELECT count(*) FROM public.{};").format(sql.Identifier(tbl)))
        row_count = pg_cur.fetchone()[0]
        if row_count > 0:
            raise RuntimeError(
                f"Safety guard triggered: public.{tbl} is NOT empty ({row_count} rows). "
                "Refusing to move or overwrite non-empty tables."
            )

    # Ensure no Views reference legacy tables
    if existing_tables:
        pg_cur.execute(
            """
            SELECT view_schema, view_name, table_name
            FROM information_schema.view_table_usage
            WHERE table_schema = 'public'
              AND table_name = ANY(%s);
            """,
            (existing_tables,),
        )
        views = pg_cur.fetchall()
        if views:
            raise RuntimeError(
                f"Safety guard triggered: existing views reference legacy tables: {views}. "
                "Inspect and resolve dependencies before migrating."
            )

        # Ensure no external table outside LEGACY_TABLES has a foreign key referencing them
        pg_cur.execute(
            """
            SELECT conrelid::regclass::text AS referencing_table, conname
            FROM pg_constraint
            WHERE contype = 'f'
              AND confrelid IN (
                  SELECT oid FROM pg_class
                  WHERE relnamespace = 'public'::regnamespace
                    AND relname = ANY(%s)
              )
              AND conrelid NOT IN (
                  SELECT oid FROM pg_class
                  WHERE relnamespace = 'public'::regnamespace
                    AND relname = ANY(%s)
              );
            """,
            (existing_tables, existing_tables),
        )
        ext_fks = pg_cur.fetchall()
        if ext_fks:
            raise RuntimeError(
                f"Safety guard triggered: external foreign keys reference legacy tables: {ext_fks}"
            )

    # Check for leftover indexes (custom or constraint-backed) on unrelated tables in public.
    # Indexes belonging to empty legacy tables in existing_tables will be moved by SET SCHEMA.
    all_target_indexes = list(PRODUCTION_INDEXES) + list(PRODUCTION_CONSTRAINT_INDEXES)
    pg_cur.execute(
        """
        SELECT indexname, tablename
        FROM pg_indexes
        WHERE schemaname = 'public'
          AND indexname = ANY(%s::text[])
          AND NOT (tablename = ANY(%s::text[]));
        """,
        (all_target_indexes, existing_tables),
    )
    colliding_indexes = pg_cur.fetchall()
    if colliding_indexes:
        raise RuntimeError(
            f"Index/constraint collision detected in public schema on unrelated objects: "
            f"{colliding_indexes}. Resolve before creating production tables and indexes."
        )

    return existing_tables


def isolate_legacy_objects(pg_cur: psycopg.Cursor, existing_tables: list[str]) -> str | None:
    """
    Move any empty legacy tables and any unowned stray sequences out of public
    into a fresh timestamped backup schema (legacy_backup_YYYYMMDD_HHMMSS).
    Preserves all constraints, indexes, triggers, and RLS policies without collision.
    """
    # Also check for any stray sequences in public matching our target identity sequences
    pg_cur.execute(
        """
        SELECT relname
        FROM pg_class
        WHERE relnamespace = 'public'::regnamespace
          AND relkind = 'S'
          AND relname = ANY(%s);
        """,
        (list(LEGACY_SEQUENCES),),
    )
    stray_sequences = [row[0] for row in pg_cur.fetchall()]

    if not existing_tables and not stray_sequences:
        return None

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    backup_schema = f"legacy_backup_{timestamp}"

    pg_cur.execute("SELECT 1 FROM pg_namespace WHERE nspname = %s;", (backup_schema,))
    if pg_cur.fetchone() is not None:
        raise RuntimeError(
            f"Backup schema {backup_schema} already exists; refusing to overwrite."
        )

    pg_cur.execute(sql.SQL("CREATE SCHEMA {};").format(sql.Identifier(backup_schema)))

    # Move tables in reverse dependency order (transactions -> accounts -> users)
    for tbl in ("transactions", "accounts", "users"):
        if tbl in existing_tables:
            pg_cur.execute(
                sql.SQL("ALTER TABLE public.{} SET SCHEMA {};").format(
                    sql.Identifier(tbl), sql.Identifier(backup_schema)
                )
            )

    # Move any remaining unowned sequences if they did not move automatically with a table
    pg_cur.execute(
        """
        SELECT relname
        FROM pg_class
        WHERE relnamespace = 'public'::regnamespace
          AND relkind = 'S'
          AND relname = ANY(%s);
        """,
        (list(LEGACY_SEQUENCES),),
    )
    remaining_sequences = [row[0] for row in pg_cur.fetchall()]
    for seq_name in remaining_sequences:
        pg_cur.execute(
            sql.SQL("ALTER SEQUENCE public.{} SET SCHEMA {};").format(
                sql.Identifier(seq_name), sql.Identifier(backup_schema)
            )
        )

    return backup_schema


def create_production_schema(pg_cur: psycopg.Cursor) -> None:
    """Create production tables, constraints, indexes, and RLS lockdown individually."""
    ddl_statements = [
        """
        CREATE TABLE public.users (
            id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
            full_name TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE,
            pin_hash TEXT NOT NULL,
            created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        """,
        """
        CREATE TABLE public.accounts (
            id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
            user_id BIGINT NOT NULL,
            account_number VARCHAR(32) NOT NULL UNIQUE,
            balance_paise BIGINT NOT NULL DEFAULT 0,
            status VARCHAR(16) NOT NULL DEFAULT 'active',
            created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

            CONSTRAINT fk_accounts_user
                FOREIGN KEY (user_id) REFERENCES public.users (id) ON DELETE RESTRICT,
            CONSTRAINT chk_accounts_balance_non_negative
                CHECK (balance_paise >= 0),
            CONSTRAINT chk_accounts_status_valid
                CHECK (status IN ('active', 'blocked', 'closed'))
        );
        """,
        """
        CREATE TABLE public.transactions (
            id BIGINT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
            account_id BIGINT NOT NULL,
            transaction_type VARCHAR(16) NOT NULL,
            amount_paise BIGINT NOT NULL,
            related_account_id BIGINT,
            description TEXT,
            created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

            CONSTRAINT fk_transactions_account
                FOREIGN KEY (account_id) REFERENCES public.accounts (id) ON DELETE RESTRICT,
            CONSTRAINT fk_transactions_related_account
                FOREIGN KEY (related_account_id) REFERENCES public.accounts (id) ON DELETE RESTRICT,
            CONSTRAINT chk_transactions_amount_positive
                CHECK (amount_paise > 0),
            CONSTRAINT chk_transactions_type_valid
                CHECK (transaction_type IN ('deposit', 'withdrawal', 'transfer_in', 'transfer_out'))
        );
        """,
        """
        CREATE TABLE public.login_attempts (
            email TEXT PRIMARY KEY,
            failed_attempts INTEGER NOT NULL DEFAULT 0,
            locked_until BIGINT NOT NULL DEFAULT 0,
            updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
        );
        """,
        "CREATE INDEX idx_accounts_user_id ON public.accounts (user_id);",
        "CREATE INDEX idx_accounts_account_number ON public.accounts (account_number);",
        "CREATE INDEX idx_transactions_account_id ON public.transactions (account_id);",
        "CREATE INDEX idx_transactions_created_at ON public.transactions (created_at DESC);",
        "CREATE INDEX idx_transactions_related_account_id ON public.transactions (related_account_id);",
        "ALTER TABLE public.users ENABLE ROW LEVEL SECURITY;",
        "ALTER TABLE public.accounts ENABLE ROW LEVEL SECURITY;",
        "ALTER TABLE public.transactions ENABLE ROW LEVEL SECURITY;",
        "ALTER TABLE public.login_attempts ENABLE ROW LEVEL SECURITY;",
        """
        REVOKE ALL ON TABLE public.users, public.accounts, public.transactions, public.login_attempts
        FROM anon, authenticated;
        """,
    ]
    for statement in ddl_statements:
        pg_cur.execute(statement)


def import_data(pg_cur: psycopg.Cursor, snapshot: dict) -> None:
    """Import SQLite records into PostgreSQL preserving exact IDs and UTC timestamps."""
    user_rows = [
        (r[0], r[1], r[2], r[3], normalize_utc_timestamp(r[4]))
        for r in snapshot["users"]
    ]
    pg_cur.executemany(
        "INSERT INTO public.users (id, full_name, email, pin_hash, created_at) "
        "VALUES (%s, %s, %s, %s, %s);",
        user_rows,
    )

    account_rows = [
        (r[0], r[1], r[2], r[3], r[4], normalize_utc_timestamp(r[5]))
        for r in snapshot["accounts"]
    ]
    pg_cur.executemany(
        "INSERT INTO public.accounts (id, user_id, account_number, balance_paise, status, created_at) "
        "VALUES (%s, %s, %s, %s, %s, %s);",
        account_rows,
    )

    tx_rows = [
        (r[0], r[1], r[2], r[3], r[4], r[5], normalize_utc_timestamp(r[6]))
        for r in snapshot["transactions"]
    ]
    pg_cur.executemany(
        "INSERT INTO public.transactions "
        "(id, account_id, transaction_type, amount_paise, related_account_id, description, created_at) "
        "VALUES (%s, %s, %s, %s, %s, %s, %s);",
        tx_rows,
    )

    login_rows = [
        (r[0], r[1], r[2], normalize_utc_timestamp(r[3]))
        for r in snapshot["login_attempts"]
    ]
    pg_cur.executemany(
        "INSERT INTO public.login_attempts (email, failed_attempts, locked_until, updated_at) "
        "VALUES (%s, %s, %s, %s);",
        login_rows,
    )


def sync_sequences(pg_cur: psycopg.Cursor) -> None:
    """Synchronize PostgreSQL identity sequences safely and confirm next ID values."""
    expected_max_ids = {
        "users": EXPECTED_USERS,
        "accounts": EXPECTED_ACCOUNTS,
        "transactions": EXPECTED_TRANSACTIONS,
    }
    for tbl, expected_max in expected_max_ids.items():
        pg_cur.execute(
            sql.SQL(
                "SELECT setval("
                "pg_get_serial_sequence({tbl_lit}, 'id'), "
                "COALESCE((SELECT MAX(id) FROM public.{tbl_ident}), 1), "
                "(SELECT MAX(id) IS NOT NULL FROM public.{tbl_ident})"
                ");"
            ).format(
                tbl_lit=sql.Literal(f"public.{tbl}"),
                tbl_ident=sql.Identifier(tbl),
            )
        )
        pg_cur.execute(
            """
            SELECT n.nspname, c.relname
            FROM pg_class c
            JOIN pg_namespace n ON n.oid = c.relnamespace
            WHERE c.relkind = 'S'
              AND c.oid = pg_get_serial_sequence(%s, 'id')::regclass;
            """,
            (f"public.{tbl}",),
        )
        seq_meta = pg_cur.fetchone()
        assert seq_meta is not None, f"Missing identity sequence for public.{tbl}.id"
        seq_schema, seq_name = seq_meta

        pg_cur.execute(
            sql.SQL("SELECT last_value, is_called FROM {}.{};").format(
                sql.Identifier(seq_schema),
                sql.Identifier(seq_name),
            )
        )
        seq_state = pg_cur.fetchone()
        assert seq_state is not None, f"Could not read sequence state for {seq_schema}.{seq_name}"
        last_value, is_called = seq_state
        assert last_value == expected_max and is_called is True, (
            f"Sequence {seq_schema}.{seq_name} misaligned: "
            f"last_value={last_value}, is_called={is_called} (expected ({expected_max}, True))"
        )


def assert_rows_equal(table_name: str, pg_rows: list[tuple], sqlite_rows: list[tuple]) -> None:
    """Compare every row between PostgreSQL and SQLite, reporting exact key/index on mismatch."""
    assert len(pg_rows) == len(sqlite_rows), (
        f"Row count mismatch on table '{table_name}': "
        f"PostgreSQL={len(pg_rows)}, SQLite={len(sqlite_rows)}"
    )
    for idx, (pg_row, sq_row) in enumerate(zip(pg_rows, sqlite_rows)):
        if pg_row != sq_row:
            raise AssertionError(
                f"Row mismatch in '{table_name}' at index {idx} (key={sq_row[0]!r}): "
                f"PostgreSQL={pg_row!r} != SQLite={sq_row!r}"
            )


def verify_row_by_row(pg_cur: psycopg.Cursor, snapshot: dict) -> None:
    """
    Perform exact 1-to-1 field comparison between SQLite snapshot and PostgreSQL
    prior to COMMIT. Any discrepancy raises AssertionError and triggers ROLLBACK.
    """
    # 1. Users (40 rows, including pin_hash and UTC timestamp)
    pg_cur.execute(
        "SELECT id, full_name, email, pin_hash, "
        "TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS') "
        "FROM public.users ORDER BY id ASC;"
    )
    assert_rows_equal("users", pg_cur.fetchall(), snapshot["users"])

    # 2. Accounts (40 rows, including individual balance_paise and user_id)
    pg_cur.execute(
        "SELECT id, user_id, account_number, balance_paise, status, "
        "TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS') "
        "FROM public.accounts ORDER BY id ASC;"
    )
    assert_rows_equal("accounts", pg_cur.fetchall(), snapshot["accounts"])

    # 3. Transactions (132 rows, including related_account_id and description)
    pg_cur.execute(
        "SELECT id, account_id, transaction_type, amount_paise, related_account_id, description, "
        "TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS') "
        "FROM public.transactions ORDER BY id ASC;"
    )
    assert_rows_equal("transactions", pg_cur.fetchall(), snapshot["transactions"])

    # 4. Login attempts (3 rows)
    pg_cur.execute(
        "SELECT email, failed_attempts, locked_until, "
        "TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI:SS') "
        "FROM public.login_attempts ORDER BY email ASC;"
    )
    assert_rows_equal("login_attempts", pg_cur.fetchall(), snapshot["login_attempts"])

    # 5. Aggregate financial checksums & bidirectional transfer pairing invariant
    pg_cur.execute("SELECT sum(balance_paise)::bigint FROM public.accounts;")
    pg_balance_sum = pg_cur.fetchone()[0]
    assert pg_balance_sum == snapshot["balance_sum"], (
        f"PostgreSQL balance checksum mismatch: {pg_balance_sum}"
    )

    pg_cur.execute("SELECT sum(amount_paise)::bigint FROM public.transactions;")
    pg_amount_sum = pg_cur.fetchone()[0]
    assert pg_amount_sum == snapshot["amount_sum"], (
        f"PostgreSQL amount checksum mismatch: {pg_amount_sum}"
    )

    pg_cur.execute(
        """
        SELECT count(*)
        FROM public.transactions t_out
        WHERE t_out.transaction_type = 'transfer_out'
          AND NOT EXISTS (
              SELECT 1
              FROM public.transactions t_in
              WHERE t_in.transaction_type = 'transfer_in'
                AND t_in.account_id = t_out.related_account_id
                AND t_in.related_account_id = t_out.account_id
                AND t_in.amount_paise = t_out.amount_paise
          );
        """
    )
    unpaired_out = pg_cur.fetchone()[0]
    assert unpaired_out == 0, f"Detected {unpaired_out} unpaired transfer_out record(s)"

    pg_cur.execute(
        """
        SELECT count(*)
        FROM public.transactions t_in
        WHERE t_in.transaction_type = 'transfer_in'
          AND NOT EXISTS (
              SELECT 1
              FROM public.transactions t_out
              WHERE t_out.transaction_type = 'transfer_out'
                AND t_out.account_id = t_in.related_account_id
                AND t_out.related_account_id = t_in.account_id
                AND t_out.amount_paise = t_in.amount_paise
          );
        """
    )
    unpaired_in = pg_cur.fetchone()[0]
    assert unpaired_in == 0, f"Detected {unpaired_in} unpaired transfer_in record(s)"

    pg_cur.execute(
        """
        SELECT
            count(*) FILTER (WHERE transaction_type = 'transfer_out'),
            count(*) FILTER (WHERE transaction_type = 'transfer_in'),
            COALESCE(sum(amount_paise) FILTER (WHERE transaction_type = 'transfer_out'), 0)::bigint,
            COALESCE(sum(amount_paise) FILTER (WHERE transaction_type = 'transfer_in'), 0)::bigint
        FROM public.transactions;
        """
    )
    out_cnt, in_cnt, out_sum, in_sum = pg_cur.fetchone()
    assert out_cnt == in_cnt and out_sum == in_sum, (
        f"Transfer symmetry mismatch: out=({out_cnt}, {out_sum}) vs in=({in_cnt}, {in_sum})"
    )

    # 6. Verify RLS is active on all 4 production tables
    pg_cur.execute(
        """
        SELECT count(*)
        FROM pg_tables
        WHERE schemaname = 'public'
          AND tablename IN ('users', 'accounts', 'transactions', 'login_attempts')
          AND rowsecurity = true;
        """
    )
    rls_count = pg_cur.fetchone()[0]
    assert rls_count == 4, f"RLS check failed: expected 4 secured tables, got {rls_count}"


def main() -> None:
    target_url = os.environ.get("TARGET_DATABASE_URL", "").strip()
    if not target_url:
        print("[ERROR] TARGET_DATABASE_URL environment variable is not set.")
        print("        Export your Supabase PostgreSQL URI (without committing it) and retry.")
        sys.exit(1)

    sqlite_path = Path(os.environ.get("SOURCE_SQLITE_PATH", str(DEFAULT_SQLITE_PATH)))

    print(f"[1/7] Computing pre-flight SHA-256 of SQLite source: {sqlite_path}")
    pre_hash = compute_sha256(sqlite_path)
    print(f"      SHA-256: {pre_hash}")

    print("[2/7] Reading SQLite snapshot in strict read-only mode...")
    snapshot = load_sqlite_snapshot(sqlite_path)
    print(
        f"      Loaded {len(snapshot['users'])} users, {len(snapshot['accounts'])} accounts "
        f"({snapshot['balance_sum']} paise), {len(snapshot['transactions'])} transactions "
        f"({snapshot['amount_sum']} paise), {len(snapshot['login_attempts'])} login_attempts."
    )

    print("[3/7] Connecting to Supabase PostgreSQL (SSL required, autocommit=False)...")
    # prepare_threshold=None ensures compatibility with both Session (5432) and Transaction (6543) poolers
    pg_conn = psycopg.connect(
        target_url,
        autocommit=False,
        prepare_threshold=None,
        connect_timeout=15,
    )

    try:
        with pg_conn.cursor() as pg_cur:
            pg_cur.execute("SET TIME ZONE 'UTC';")

            print("[4/7] Running pre-flight idempotency and dependency safety guards...")
            existing_legacy = preflight_guard(pg_cur)
            print(f"      Existing empty tables to isolate: {existing_legacy or 'none'}")

            backup_schema = isolate_legacy_objects(pg_cur, existing_legacy)
            if backup_schema:
                print(f"      Isolated legacy objects cleanly into schema: {backup_schema}")

            print("[5/7] Creating production schema, indexes, and RLS policies...")
            create_production_schema(pg_cur)

            print("[6/7] Importing users, accounts, transactions, and login_attempts...")
            import_data(pg_cur, snapshot)
            sync_sequences(pg_cur)

            print("[7/7] Executing row-by-row, RLS, sequence, and SQLite hash verification before COMMIT...")
            verify_row_by_row(pg_cur, snapshot)

        post_hash = compute_sha256(sqlite_path)
        if post_hash != pre_hash:
            raise RuntimeError(
                f"CRITICAL: SQLite source file hash changed during migration "
                f"(before={pre_hash}, after={post_hash}). Aborting before COMMIT."
            )
        print(f"      SQLite source SHA-256 unchanged: {post_hash}")

        pg_conn.commit()
        print(
            "\n[SUCCESS] PostgreSQL transaction COMMITTED after passing all checks "
            "(40 users, 40 accounts, 132 transactions, 3 login_attempts, RLS, sequences, and SQLite SHA-256)."
        )

    except Exception as exc:
        pg_conn.rollback()
        print(f"\n[ABORTED] Migration error: {exc}")
        print("          PostgreSQL transaction completely ROLLED BACK. Zero changes applied to Supabase.")
        sys.exit(1)
    finally:
        pg_conn.close()


if __name__ == "__main__":
    main()
