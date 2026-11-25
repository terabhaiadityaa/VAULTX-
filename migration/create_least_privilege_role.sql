-- =============================================================================
-- VAULTX — Least-Privilege PostgreSQL Role Script (Supabase Reference SQL)
-- =============================================================================
-- Purpose: Creates a dedicated non-superuser application role (vaultx_app)
-- with ONLY the minimum DML permissions required by atm_backend.
--
-- IMPORTANT:
-- 1. Review and replace 'REPLACE_WITH_STRONG_GENERATED_PASSWORD' before running.
-- 2. Never commit the real password to Git.
-- 3. Do NOT drop/truncate/modify existing tables or financial data.
-- =============================================================================

BEGIN;

-- 1. Create dedicated non-privileged login role (cannot create DBs/roles or bypass RLS)
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'vaultx_app') THEN
        CREATE ROLE vaultx_app
            LOGIN
            NOINHERIT
            NOSUPERUSER
            NOCREATEDB
            NOCREATEROLE
            NOREPLICATION
            NOBYPASSRLS
            PASSWORD 'REPLACE_WITH_STRONG_GENERATED_PASSWORD';
    END IF;
END $$;

-- 2. Restrict schema usage strictly to public (no access to legacy_backup_* or auth internals)
GRANT USAGE ON SCHEMA public TO vaultx_app;

-- 3. Grant ONLY DML permissions required by Database.cpp & AuthService.cpp
--    (No TRUNCATE, REFERENCES, TRIGGER, DROP, or ALTER privileges)
GRANT SELECT, INSERT, UPDATE, DELETE
    ON TABLE public.users, public.accounts, public.transactions, public.login_attempts
    TO vaultx_app;

-- 4. Grant sequence usage for INSERT ... RETURNING id
GRANT USAGE, SELECT
    ON SEQUENCE public.users_id_seq, public.accounts_id_seq, public.transactions_id_seq
    TO vaultx_app;

-- 5. Grant explicit RLS policies scoped strictly TO vaultx_app
--    (anon and authenticated remain 100% blocked with zero policies and zero grants)
DROP POLICY IF EXISTS vaultx_app_users_policy ON public.users;
CREATE POLICY vaultx_app_users_policy
    ON public.users
    FOR ALL
    TO vaultx_app
    USING (true)
    WITH CHECK (true);

DROP POLICY IF EXISTS vaultx_app_accounts_policy ON public.accounts;
CREATE POLICY vaultx_app_accounts_policy
    ON public.accounts
    FOR ALL
    TO vaultx_app
    USING (true)
    WITH CHECK (true);

DROP POLICY IF EXISTS vaultx_app_transactions_policy ON public.transactions;
CREATE POLICY vaultx_app_transactions_policy
    ON public.transactions
    FOR ALL
    TO vaultx_app
    USING (true)
    WITH CHECK (true);

DROP POLICY IF EXISTS vaultx_app_login_attempts_policy ON public.login_attempts;
CREATE POLICY vaultx_app_login_attempts_policy
    ON public.login_attempts
    FOR ALL
    TO vaultx_app
    USING (true)
    WITH CHECK (true);

COMMIT;
