-- VAULTX Banking Simulation database schema
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    full_name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    pin_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    account_number TEXT NOT NULL UNIQUE,
    balance_paise INTEGER NOT NULL DEFAULT 0
        CHECK (balance_paise >= 0),
    status TEXT NOT NULL DEFAULT 'active'
        CHECK (status IN ('active', 'blocked', 'closed')),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Each transfer is represented by two linked ledger entries:
-- transfer_out on the sender account and transfer_in on the receiver account.
-- Both entries are committed atomically with the balance changes.
CREATE TABLE IF NOT EXISTS transactions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    transaction_type TEXT NOT NULL
        CHECK (transaction_type IN (
            'deposit', 'withdrawal', 'transfer_in', 'transfer_out'
        )),
    amount_paise INTEGER NOT NULL CHECK (amount_paise > 0),
    related_account_id INTEGER,
    description TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id),
    FOREIGN KEY (related_account_id) REFERENCES accounts(id)
);

CREATE TABLE IF NOT EXISTS login_attempts (
    email TEXT PRIMARY KEY,
    failed_attempts INTEGER NOT NULL DEFAULT 0,
    locked_until INTEGER NOT NULL DEFAULT 0,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_accounts_user_id
    ON accounts(user_id);
CREATE INDEX IF NOT EXISTS idx_transactions_account_id
    ON transactions(account_id);
CREATE INDEX IF NOT EXISTS idx_transactions_created_at
    ON transactions(created_at);
CREATE INDEX IF NOT EXISTS idx_transactions_related_account_id
    ON transactions(related_account_id);
