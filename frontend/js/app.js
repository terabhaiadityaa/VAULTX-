/**
 * NOIR — ATM SIMULATION PLATFORM
 * Client-side Controller & Banking Logic
 */

'use strict';

// Dynamic API URL resolution:
// If frontend is served directly by the C++ backend (same origin), use window.location.origin.
// If served by VS Code Live Server (port 5500) or Vite (port 5173), fallback to localhost:8080.
const API_BASE_URL = (window.location.port === '5500' || window.location.port === '5173' || window.location.port === '3000')
  ? 'http://127.0.0.1:8080'
  : window.location.origin;

// State
let sessionToken = null;
let currentAccount = null;
let currentTransactions = [];
let currentFilter = 'all';

// DOM Elements
const authSection = document.getElementById('authSection');
const dashboardSection = document.getElementById('dashboardSection');
const userQuickInfo = document.getElementById('userQuickInfo');
const headerAccountNum = document.getElementById('headerAccountNum');
const copyAccountBtn = document.getElementById('copyAccountBtn');
const logoutBtn = document.getElementById('logoutBtn');

// Auth DOM
const tabLoginBtn = document.getElementById('tabLoginBtn');
const tabRegisterBtn = document.getElementById('tabRegisterBtn');
const loginForm = document.getElementById('loginForm');
const registerForm = document.getElementById('registerForm');
const loginIdentifier = document.getElementById('loginIdentifier');
const loginPin = document.getElementById('loginPin');
const loginSubmitBtn = document.getElementById('loginSubmitBtn');
const regFullName = document.getElementById('regFullName');
const regEmail = document.getElementById('regEmail');
const regPin = document.getElementById('regPin');
const regSubmitBtn = document.getElementById('regSubmitBtn');

// Dashboard DOM
const cardAccountNumber = document.getElementById('cardAccountNumber');
const cardHolderName = document.getElementById('cardHolderName');
const dashboardBalanceDisplay = document.getElementById('dashboardBalanceDisplay');
const statAccountNumber = document.getElementById('statAccountNumber');
const refreshBalanceBtn = document.getElementById('refreshBalanceBtn');

// Action Tabs DOM
const tabButtons = document.querySelectorAll('.nav-tabs .tab-btn');
const tabContents = document.querySelectorAll('.tab-content');

// Forms & Inputs
const withdrawForm = document.getElementById('withdrawForm');
const withdrawAmountInput = document.getElementById('withdrawAmount');
const withdrawSubmitBtn = document.getElementById('withdrawSubmitBtn');
const dispenserText = document.getElementById('dispenserText');
const depositForm = document.getElementById('depositForm');
const depositAmountInput = document.getElementById('depositAmount');
const depositSubmitBtn = document.getElementById('depositSubmitBtn');
const transferForm = document.getElementById('transferForm');
const transferTargetInput = document.getElementById('transferTarget');
const transferAmountInput = document.getElementById('transferAmount');
const transferSubmitBtn = document.getElementById('transferSubmitBtn');

// Passbook DOM
const transactionTableBody = document.getElementById('transactionTableBody');
const filterPills = document.querySelectorAll('.filter-pill');

// Receipt Modal DOM
const receiptModal = document.getElementById('receiptModal');
const rcptTxId = document.getElementById('rcptTxId');
const rcptDate = document.getElementById('rcptDate');
const rcptAccount = document.getElementById('rcptAccount');
const rcptType = document.getElementById('rcptType');
const rcptPartyRow = document.getElementById('rcptPartyRow');
const rcptParty = document.getElementById('rcptParty');
const rcptAmount = document.getElementById('rcptAmount');
const printReceiptBtn = document.getElementById('printReceiptBtn');
const closeReceiptBtn = document.getElementById('closeReceiptBtn');

// Toast Container
const toastContainer = document.getElementById('toast-container');

// ==========================================================================
// Utility Functions
// ==========================================================================

function formatRupees(amount) {
  const num = typeof amount === 'number' ? amount : Number(amount) || 0;
  return new Intl.NumberFormat('en-IN', {
    style: 'currency',
    currency: 'INR',
    minimumFractionDigits: 2,
    maximumFractionDigits: 2
  }).format(num);
}

function formatCardNumber(accNum) {
  if (!accNum) return '•••• •••• •••• ••••';
  const clean = String(accNum).replace(/\s+/g, '');
  if (clean.length <= 4) return `•••• •••• •••• ${clean}`;
  return clean.replace(/(.{4})/g, '$1 ').trim();
}

function showToast(message, type = 'info', duration = 4000) {
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  toast.innerHTML = `
    <span class="toast-message">${escapeHtml(message)}</span>
    <button class="toast-close" title="Dismiss">&times;</button>
  `;

  toast.querySelector('.toast-close').addEventListener('click', () => {
    toast.remove();
  });

  toastContainer.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(16px)';
    toast.style.transition = 'all 0.25s ease';
    setTimeout(() => toast.remove(), 250);
  }, duration);
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function calculateDenominations(amount) {
  if (!amount || amount <= 0 || amount % 100 !== 0) {
    return 'Dispenser ready: enter an amount in multiples of ₹100';
  }
  let remaining = amount;
  const count500 = Math.floor(remaining / 500);
  remaining %= 500;
  const count200 = Math.floor(remaining / 200);
  remaining %= 200;
  const count100 = Math.floor(remaining / 100);

  const parts = [];
  if (count500 > 0) parts.push(`${count500} × ₹500`);
  if (count200 > 0) parts.push(`${count200} × ₹200`);
  if (count100 > 0) parts.push(`${count100} × ₹100`);

  return `Dispenser notes: ${parts.join(', ')}`;
}

// ==========================================================================
// Authentication & Session Management
// ==========================================================================

function getStoredToken() {
  try {
    return sessionStorage.getItem('noir_token') || localStorage.getItem('atmSessionToken') || null;
  } catch (_) {
    return null;
  }
}

function storeToken(token) {
  sessionToken = token;
  try {
    sessionStorage.setItem('noir_token', token);
    localStorage.setItem('atmSessionToken', token);
  } catch (_) {}
}

function clearToken() {
  sessionToken = null;
  currentAccount = null;
  currentTransactions = [];
  try {
    sessionStorage.removeItem('noir_token');
    localStorage.removeItem('atmSessionToken');
  } catch (_) {}
}

async function checkExistingSession() {
  const token = getStoredToken();
  if (!token) {
    showAuthView();
    return;
  }

  sessionToken = token;
  try {
    const res = await fetch(`${API_BASE_URL}/api/account`, {
      headers: { 'Authorization': `Bearer ${sessionToken}` }
    });

    if (res.ok) {
      const data = await res.json();
      currentAccount = data;
      showDashboardView();
      await fetchTransactions();
    } else {
      clearToken();
      showAuthView();
    }
  } catch (err) {
    console.error('Session check error:', err);
    showAuthView();
  }
}

function showAuthView() {
  authSection.style.display = 'block';
  dashboardSection.style.display = 'none';
  userQuickInfo.style.display = 'none';
}

function showDashboardView() {
  authSection.style.display = 'none';
  dashboardSection.style.display = 'block';
  userQuickInfo.style.display = 'flex';
  updateDashboardUI();
}

function updateDashboardUI() {
  if (!currentAccount) return;

  const user = currentAccount.user || {};
  const acc = currentAccount.account || {};

  const accNum = acc.accountNumber || '—';
  const fullName = user.fullName || 'SIMULATED USER';
  const balance = acc.balance || 0;

  headerAccountNum.textContent = accNum;
  statAccountNumber.textContent = accNum;
  cardAccountNumber.textContent = formatCardNumber(accNum);
  cardHolderName.textContent = fullName.toUpperCase();
  dashboardBalanceDisplay.textContent = formatRupees(balance);
}

// ==========================================================================
// Event Listeners: Auth Tabs & PIN Visibility
// ==========================================================================

tabLoginBtn.addEventListener('click', () => {
  tabLoginBtn.classList.add('active');
  tabRegisterBtn.classList.remove('active');
  loginForm.style.display = 'block';
  registerForm.style.display = 'none';
});

tabRegisterBtn.addEventListener('click', () => {
  tabRegisterBtn.classList.add('active');
  tabLoginBtn.classList.remove('active');
  registerForm.style.display = 'block';
  loginForm.style.display = 'none';
});

document.querySelectorAll('.pin-toggle-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const targetId = btn.getAttribute('data-target');
    const input = document.getElementById(targetId);
    if (!input) return;
    if (input.type === 'password') {
      input.type = 'text';
      btn.style.color = 'var(--accent-blue)';
    } else {
      input.type = 'password';
      btn.style.color = 'var(--slate-400)';
    }
  });
});

copyAccountBtn.addEventListener('click', () => {
  if (currentAccount && currentAccount.account) {
    navigator.clipboard.writeText(currentAccount.account.accountNumber)
      .then(() => showToast('Account number copied to clipboard', 'success', 2500))
      .catch(() => showToast('Could not copy account number', 'error'));
  }
});

logoutBtn.addEventListener('click', async () => {
  if (sessionToken) {
    try {
      await fetch(`${API_BASE_URL}/api/auth/logout`, {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${sessionToken}` }
      });
    } catch (_) {}
  }
  clearToken();
  showAuthView();
  showToast('Logged out securely.', 'info');
});

// ==========================================================================
// Form Submission: Login
// ==========================================================================

loginForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const emailOrAcc = loginIdentifier.value.trim();
  const pin = loginPin.value.trim();

  if (!emailOrAcc) {
    showToast('Please enter your email or account number.', 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }

  loginSubmitBtn.disabled = true;
  loginSubmitBtn.querySelector('span').textContent = 'Authenticating…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email: emailOrAcc, pin: pin })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Login failed. Verify credentials.', 'error');
      return;
    }

    storeToken(data.sessionToken);
    showToast('Authentication successful! Welcome to NOIR ATM.', 'success');

    // Fetch full account info
    const accRes = await fetch(`${API_BASE_URL}/api/account`, {
      headers: { 'Authorization': `Bearer ${sessionToken}` }
    });

    if (accRes.ok) {
      currentAccount = await accRes.json();
    } else {
      currentAccount = {
        user: { fullName: data.fullName, email: data.email },
        account: { accountNumber: data.accountNumber, balance: data.balance, balancePaise: data.balancePaise }
      };
    }

    loginForm.reset();
    showDashboardView();
    await fetchTransactions();
  } catch (err) {
    console.error('Login error:', err);
    showToast('Connection error. Could not reach backend server.', 'error');
  } finally {
    loginSubmitBtn.disabled = false;
    loginSubmitBtn.querySelector('span').textContent = 'Authenticate & Enter ATM';
  }
});

// ==========================================================================
// Form Submission: Registration
// ==========================================================================

registerForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const fullName = regFullName.value.trim();
  const email = regEmail.value.trim();
  const pin = regPin.value.trim();

  if (!fullName || fullName.length < 2) {
    showToast('Please enter your full legal name.', 'error');
    return;
  }
  if (!email || !email.includes('@')) {
    showToast('Please enter a valid email address.', 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }

  regSubmitBtn.disabled = true;
  regSubmitBtn.querySelector('span').textContent = 'Creating Account…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ fullName, email, pin })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Registration failed.', 'error');
      return;
    }

    showToast(`Account created! Allocated Account No: ${data.accountNumber}. Auto-logging in...`, 'success', 5000);

    // Auto-login newly registered user
    const loginRes = await fetch(`${API_BASE_URL}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ email: email, pin: pin })
    });

    if (loginRes.ok) {
      const loginData = await loginRes.json();
      storeToken(loginData.sessionToken);

      const accRes = await fetch(`${API_BASE_URL}/api/account`, {
        headers: { 'Authorization': `Bearer ${sessionToken}` }
      });
      if (accRes.ok) currentAccount = await accRes.json();

      registerForm.reset();
      showDashboardView();
      await fetchTransactions();
    } else {
      tabLoginBtn.click();
      loginIdentifier.value = email;
    }
  } catch (err) {
    console.error('Registration error:', err);
    showToast('Could not connect to ATM backend.', 'error');
  } finally {
    regSubmitBtn.disabled = false;
    regSubmitBtn.querySelector('span').textContent = 'Create Account & Claim ₹10,000';
  }
});

// ==========================================================================
// Operations: Tab Navigation
// ==========================================================================

tabButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    tabButtons.forEach(b => b.classList.remove('active'));
    tabContents.forEach(c => c.style.display = 'none');

    btn.classList.add('active');
    const targetId = btn.getAttribute('data-tab');
    const targetContent = document.getElementById(targetId);
    if (targetContent) targetContent.style.display = 'block';

    if (targetId === 'tab-history') {
      fetchTransactions();
    }
  });
});

refreshBalanceBtn.addEventListener('click', async () => {
  refreshBalanceBtn.disabled = true;
  await refreshAccountBalance();
  refreshBalanceBtn.disabled = false;
  showToast('Account balance refreshed.', 'info', 2000);
});

async function refreshAccountBalance() {
  if (!sessionToken) return;
  try {
    const res = await fetch(`${API_BASE_URL}/api/account`, {
      headers: { 'Authorization': `Bearer ${sessionToken}` }
    });
    if (res.ok) {
      currentAccount = await res.json();
      updateDashboardUI();
    }
  } catch (err) {
    console.error('Refresh balance error:', err);
  }
}

// ==========================================================================
// Operations: Cash Withdrawal
// ==========================================================================

document.querySelectorAll('.withdraw-preset').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.preventDefault();
    const amount = Number(btn.getAttribute('data-amount'));
    withdrawAmountInput.value = amount;
    dispenserText.textContent = calculateDenominations(amount);
  });
});

withdrawAmountInput.addEventListener('input', () => {
  const amount = Number(withdrawAmountInput.value);
  dispenserText.textContent = calculateDenominations(amount);
});

withdrawForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const amount = Number(withdrawAmountInput.value);

  if (!amount || amount <= 0 || amount % 100 !== 0) {
    showToast('Withdrawal amount must be in multiples of ₹100.', 'error');
    return;
  }
  if (amount > 50000) {
    showToast('Maximum withdrawal limit per transaction is ₹50,000.', 'error');
    return;
  }
  if (currentAccount && currentAccount.account && amount > currentAccount.account.balance) {
    showToast('Insufficient funds for this withdrawal amount.', 'error');
    return;
  }

  withdrawSubmitBtn.disabled = true;
  withdrawSubmitBtn.querySelector('span').textContent = 'Dispensing Notes…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/withdraw`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${sessionToken}`
      },
      body: JSON.stringify({ amount: amount })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Withdrawal failed.', 'error');
      return;
    }

    showToast(`Please collect your simulated cash: ${formatRupees(amount)}`, 'success', 5000);
    withdrawAmountInput.value = '';
    dispenserText.textContent = 'Dispenser ready: select or enter withdrawal amount';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Withdrawal error:', err);
    showToast('Failed to communicate with ATM backend.', 'error');
  } finally {
    withdrawSubmitBtn.disabled = false;
    withdrawSubmitBtn.querySelector('span').textContent = 'Dispense Simulated Cash';
  }
});

// ==========================================================================
// Operations: Cash Deposit
// ==========================================================================

document.querySelectorAll('.deposit-preset').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.preventDefault();
    depositAmountInput.value = Number(btn.getAttribute('data-amount'));
  });
});

depositForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const amount = Number(depositAmountInput.value);

  if (!amount || amount <= 0) {
    showToast('Deposit amount must be positive.', 'error');
    return;
  }
  if (Math.round(amount * 100) !== amount * 100) {
    showToast('Deposit amount cannot exceed two decimal places.', 'error');
    return;
  }

  depositSubmitBtn.disabled = true;
  depositSubmitBtn.querySelector('span').textContent = 'Processing Deposit…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/deposit`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${sessionToken}`
      },
      body: JSON.stringify({ amount: amount })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Deposit failed.', 'error');
      return;
    }

    showToast(`Deposit of ${formatRupees(amount)} credited successfully!`, 'success');
    depositAmountInput.value = '';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Deposit error:', err);
    showToast('Failed to communicate with ATM backend.', 'error');
  } finally {
    depositSubmitBtn.disabled = false;
    depositSubmitBtn.querySelector('span').textContent = 'Confirm Cash Deposit';
  }
});

// ==========================================================================
// Operations: Fund Transfer
// ==========================================================================

transferForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const target = transferTargetInput.value.trim();
  const amount = Number(transferAmountInput.value);

  if (!target || !/^\d+$/.test(target)) {
    showToast('Please enter a valid numeric recipient account number.', 'error');
    return;
  }
  if (currentAccount && currentAccount.account && target === currentAccount.account.accountNumber) {
    showToast('Self-transfer is not permitted. Please enter a different recipient account.', 'error');
    return;
  }
  if (!amount || amount <= 0 || Math.round(amount * 100) !== amount * 100) {
    showToast('Amount must be positive with at most 2 decimal places.', 'error');
    return;
  }
  if (currentAccount && currentAccount.account && amount > currentAccount.account.balance) {
    showToast('Insufficient balance for this transfer.', 'error');
    return;
  }

  transferSubmitBtn.disabled = true;
  transferSubmitBtn.querySelector('span').textContent = 'Executing Transfer…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/transfer`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${sessionToken}`
      },
      body: JSON.stringify({ targetAccount: target, amount: amount })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Transfer failed.', 'error');
      return;
    }

    showToast(`Transferred ${formatRupees(amount)} to Account #${target} successfully!`, 'success');
    transferTargetInput.value = '';
    transferAmountInput.value = '';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Transfer error:', err);
    showToast('Failed to communicate with ATM backend.', 'error');
  } finally {
    transferSubmitBtn.disabled = false;
    transferSubmitBtn.querySelector('span').textContent = 'Execute Secure Transfer';
  }
});

// ==========================================================================
// Passbook Ledger & History
// ==========================================================================

async function fetchTransactions() {
  if (!sessionToken) return;
  try {
    const res = await fetch(`${API_BASE_URL}/api/transactions?limit=50`, {
      headers: { 'Authorization': `Bearer ${sessionToken}` }
    });

    if (res.ok) {
      const data = await res.json();
      currentTransactions = data.transactions || [];
      renderTransactions();
    }
  } catch (err) {
    console.error('Fetch transactions error:', err);
  }
}

filterPills.forEach(pill => {
  pill.addEventListener('click', () => {
    filterPills.forEach(p => p.classList.remove('active'));
    pill.classList.add('active');
    currentFilter = pill.getAttribute('data-filter');
    renderTransactions();
  });
});

function renderTransactions() {
  if (!currentTransactions || currentTransactions.length === 0) {
    transactionTableBody.innerHTML = `
      <tr>
        <td colspan="5" class="empty-state">
          <svg width="40" height="40" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
            <rect x="2" y="5" width="20" height="14" rx="2"></rect>
            <line x1="2" y1="10" x2="22" y2="10"></line>
          </svg>
          <p>No transaction history recorded for this account yet.</p>
        </td>
      </tr>
    `;
    return;
  }

  const filtered = currentTransactions.filter(item => {
    if (currentFilter === 'all') return true;
    return item.direction === currentFilter;
  });

  if (filtered.length === 0) {
    transactionTableBody.innerHTML = `
      <tr>
        <td colspan="5" class="empty-state">
          <p>No transactions matching filter "${currentFilter}".</p>
        </td>
      </tr>
    `;
    return;
  }

  let html = '';
  filtered.forEach(tx => {
    const isCredit = tx.direction === 'credit';
    const badgeClass = isCredit ? 'badge-credit' : 'badge-debit';
    const amountClass = isCredit ? 'amount-credit' : 'amount-debit';
    const sign = isCredit ? '+' : '-';
    const party = tx.relatedAccount ? `A/C #${tx.relatedAccount}` : 'Self / ATM Terminal';

    html += `
      <tr>
        <td>${escapeHtml(tx.createdAt || 'Just now')}</td>
        <td>
          <span class="tx-badge ${badgeClass}">${escapeHtml(tx.type || 'TX')}</span>
          <span style="margin-left: 8px;">${escapeHtml(tx.description || '')}</span>
        </td>
        <td>${escapeHtml(party)}</td>
        <td class="${amountClass}">${sign} ${formatRupees(tx.amount)}</td>
        <td>
          <button class="action-icon-btn view-rcpt-btn" data-id="${tx.id}">
            Receipt
          </button>
        </td>
      </tr>
    `;
  });

  transactionTableBody.innerHTML = html;

  // Attach receipt button click handlers
  transactionTableBody.querySelectorAll('.view-rcpt-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const id = Number(btn.getAttribute('data-id'));
      const tx = currentTransactions.find(t => t.id === id);
      if (tx) openReceiptModal(tx);
    });
  });
}

// ==========================================================================
// Receipt Modal Handling
// ==========================================================================

function openReceiptModal(tx) {
  rcptTxId.textContent = `#TXN-${String(tx.id).padStart(6, '0')}`;
  rcptDate.textContent = tx.createdAt || new Date().toLocaleString();
  rcptAccount.textContent = currentAccount && currentAccount.account ? currentAccount.account.accountNumber : '—';
  rcptType.textContent = (tx.type || 'TRANSACTION').toUpperCase();

  if (tx.relatedAccount) {
    rcptPartyRow.style.display = 'flex';
    rcptParty.textContent = `#${tx.relatedAccount}`;
  } else {
    rcptPartyRow.style.display = 'none';
  }

  const sign = tx.direction === 'credit' ? '+' : '-';
  rcptAmount.textContent = `${sign} ${formatRupees(tx.amount)}`;

  receiptModal.style.display = 'flex';
}

closeReceiptBtn.addEventListener('click', () => {
  receiptModal.style.display = 'none';
});

receiptModal.addEventListener('click', (e) => {
  if (e.target === receiptModal) receiptModal.style.display = 'none';
});

printReceiptBtn.addEventListener('click', () => {
  window.print();
});

// Initialize on load
document.addEventListener('DOMContentLoaded', () => {
  checkExistingSession();
});
