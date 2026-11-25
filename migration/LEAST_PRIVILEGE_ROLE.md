# VAULTX — Applying the Least-Privilege `vaultx_app` Database Role

## Why This Matters
Currently, the backend connects to Supabase as the built-in `postgres` role (`rolbypassrls = true`, `rolcreatedb = true`, `rolcreaterole = true`). Moving to `vaultx_app` guarantees that even if an application bug occurred, the backend database connection cannot `DROP` tables, `TRUNCATE` ledgers, `ALTER` schemas, create roles, or read backup schemas (`legacy_backup_*`).

## What [`create_least_privilege_role.sql`](file:///Users/aditya/first%20project/ATM-Transaction-Simulation/migration/create_least_privilege_role.sql) Grants
- **Role attributes:** `LOGIN`, `NOINHERIT`, `NOSUPERUSER`, `NOCREATEDB`, `NOCREATEROLE`, `NOREPLICATION`, `NOBYPASSRLS`.
- **Table privileges:** `SELECT, INSERT, UPDATE, DELETE` **only** on `public.users`, `public.accounts`, `public.transactions`, `public.login_attempts`.
- **Sequence privileges:** `USAGE, SELECT` **only** on `users_id_seq`, `accounts_id_seq`, `transactions_id_seq`.
- **RLS policies:** Explicit `FOR ALL TO vaultx_app` policies on the 4 production tables (`anon` and `authenticated` remain 100% denied).

## Safe Application Procedure (Manual — When Ready)
1. Generate a strong random 32+ character password locally (never reuse old passwords):
   ```zsh
   openssl rand -base64 24 | tr -d '=+/' | cut -c1-28
   ```
2. Open **Supabase Dashboard $\rightarrow$ SQL Editor**, paste [`migration/create_least_privilege_role.sql`](file:///Users/aditya/first%20project/ATM-Transaction-Simulation/migration/create_least_privilege_role.sql), replace `REPLACE_WITH_STRONG_GENERATED_PASSWORD` inside the SQL Editor only (never commit the password to Git), and run it.
3. Verify grants and RLS in SQL Editor:
   ```sql
   SELECT rolname, rolsuper, rolbypassrls, rolcreaterole, rolcreatedb
   FROM pg_roles
   WHERE rolname = 'vaultx_app';

   SELECT grantee, table_name, privilege_type
   FROM information_schema.role_table_grants
   WHERE table_schema = 'public' AND grantee = 'vaultx_app'
   ORDER BY table_name, privilege_type;
   ```
4. Test locally first with `DATABASE_URL` updated to the `vaultx_app` connection string (`postgresql://vaultx_app.<project-ref>:<PASSWORD>@aws-1-ap-southeast-2.pooler.supabase.com:5432/postgres?sslmode=require`, or verify Supabase Session Pooler vs Direct connection support for custom roles in Project Settings $\rightarrow$ Database).
5. Once confirmed working locally, update `DATABASE_URL` in **Render Dashboard $\rightarrow$ Environment**.
