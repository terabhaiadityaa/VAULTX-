-- =============================================================================
-- VAULTX — Safe Reversible Mobile Number Authentication Migration (Additive)
-- =============================================================================
-- Preserves 100% of existing users, accounts, balances, PIN hashes, and
-- transaction ledger records. Existing rows keep mobile_number = NULL until
-- linked safely via /api/auth/link-mobile (zero fake numbers generated).
-- =============================================================================

BEGIN;

ALTER TABLE public.users
    ADD COLUMN IF NOT EXISTS mobile_number VARCHAR(20),
    ADD COLUMN IF NOT EXISTS mobile_verified BOOLEAN NOT NULL DEFAULT false,
    ADD COLUMN IF NOT EXISTS mobile_linked_at TIMESTAMPTZ;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'chk_users_mobile_e164'
          AND conrelid = 'public.users'::regclass
    ) THEN
        ALTER TABLE public.users
            ADD CONSTRAINT chk_users_mobile_e164
            CHECK (mobile_number IS NULL OR mobile_number ~ '^\+[1-9][0-9]{7,14}$');
    END IF;
END $$;

CREATE UNIQUE INDEX IF NOT EXISTS uq_users_mobile_number_not_null
    ON public.users (mobile_number)
    WHERE mobile_number IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_users_mobile_number
    ON public.users (mobile_number);

COMMENT ON COLUMN public.users.mobile_number IS 'Canonical E.164 mobile login identifier (e.g. +919876543210). Nullable for existing accounts prior to linking.';
COMMENT ON COLUMN public.users.mobile_verified IS 'True only after OTP ownership verification via a real SMS provider.';
COMMENT ON COLUMN public.users.mobile_linked_at IS 'UTC timestamp when mobile_number was registered or linked.';

COMMIT;
