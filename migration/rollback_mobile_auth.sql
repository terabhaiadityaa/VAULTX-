-- =============================================================================
-- VAULTX — Rollback Script for Mobile Number Authentication Migration
-- =============================================================================
-- Safely removes only the additive mobile-auth columns, constraint, and indexes.
-- Existing users, accounts, balances, PIN hashes, and transactions stay intact.
-- =============================================================================

BEGIN;

DROP INDEX IF EXISTS public.idx_users_mobile_number;
DROP INDEX IF EXISTS public.uq_users_mobile_number_not_null;

ALTER TABLE public.users
    DROP CONSTRAINT IF EXISTS chk_users_mobile_e164,
    DROP COLUMN IF EXISTS mobile_linked_at,
    DROP COLUMN IF EXISTS mobile_verified,
    DROP COLUMN IF EXISTS mobile_number;

COMMIT;
