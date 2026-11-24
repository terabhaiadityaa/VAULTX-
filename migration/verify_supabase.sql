-- =============================================================================
-- VAULTX — Post-Migration Read-Only Verification Queries (Supabase SQL Editor)
-- Run these only after migrate_sqlite_to_supabase.py reports [SUCCESS].
-- All queries below are read-only.
-- =============================================================================

-- 1. Verify Record Counts
-- Expected: users=40, accounts=40, transactions=132, login_attempts=3
SELECT 'users' AS table_name, count(*) AS row_count FROM public.users
UNION ALL
SELECT 'accounts', count(*) FROM public.accounts
UNION ALL
SELECT 'transactions', count(*) FROM public.transactions
UNION ALL
SELECT 'login_attempts', count(*) FROM public.login_attempts;

-- 2. Verify Financial Balance Checksum
-- Expected: 685449100 paise (INR 6,854,491.00)
SELECT
    count(*) AS account_count,
    sum(balance_paise) AS total_balance_paise,
    (sum(balance_paise)::numeric / 100.0) AS total_balance_inr
FROM public.accounts;

-- 3. Verify Transaction Volume Breakdown
-- Expected: deposit=64 (688099100), withdrawal=20 (6600000),
--           transfer_in=24 (3782200), transfer_out=24 (3782200)
SELECT
    transaction_type,
    count(*) AS tx_count,
    sum(amount_paise) AS total_amount_paise
FROM public.transactions
GROUP BY transaction_type
ORDER BY transaction_type;

-- 4. Verify transfer pairing in BOTH directions.
-- Expected: both counts are 0. This checks counterpart existence, not unique pairing.
SELECT 'unpaired_transfer_out' AS check_name, count(*) AS failures
FROM public.transactions t_out
WHERE t_out.transaction_type = 'transfer_out'
  AND NOT EXISTS (
      SELECT 1
      FROM public.transactions t_in
      WHERE t_in.transaction_type = 'transfer_in'
        AND t_in.account_id = t_out.related_account_id
        AND t_in.related_account_id = t_out.account_id
        AND t_in.amount_paise = t_out.amount_paise
  )
UNION ALL
SELECT 'unpaired_transfer_in', count(*)
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

-- 5. Inspect identity sequence positions (expected last_value: 40, 40, 132).
-- pg_sequences does not expose is_called; the migration script checks that internally.
SELECT schemaname, sequencename, last_value
FROM pg_sequences
WHERE schemaname = 'public'
  AND sequencename IN ('users_id_seq', 'accounts_id_seq', 'transactions_id_seq')
ORDER BY sequencename;

-- 6. Verify Row Level Security is enabled on all four production tables.
-- Expected: rowsecurity = true for all 4 tables.
SELECT tablename, rowsecurity
FROM pg_tables
WHERE schemaname = 'public'
  AND tablename IN ('users', 'accounts', 'transactions', 'login_attempts')
ORDER BY tablename;

-- 7. Verify legacy empty tables were preserved in a timestamped backup schema.
SELECT table_schema, table_name
FROM information_schema.tables
WHERE table_schema LIKE 'legacy_backup_%'
ORDER BY table_schema, table_name;
