/**
 * NOIR — ATM SIMULATION PLATFORM
 * Client-side Controller & Banking Logic (Account Number & ATM Card Auth)
 */

'use strict';

// Dynamic API URL resolution
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
const loginView = document.getElementById('loginView');
const registerView = document.getElementById('registerView');
const loginForm = document.getElementById('loginForm');
const registerForm = document.getElementById('registerForm');
const loginIdentifier = document.getElementById('loginIdentifier');
const loginPin = document.getElementById('loginPin');
const loginSubmitBtn = document.getElementById('loginSubmitBtn');
const regFullName = document.getElementById('regFullName');
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
// Formatting & Helpers
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

function formatCardDisplay(accNum) {
  if (!accNum) return '•••• •••• •••• ••••';
  const clean = String(accNum).replace(/\D/g, '');
  if (clean.length === 12) {
    return clean.replace(/(\d{4})(\d{4})(\d{4})/, '$1 $2 $3');
  }
  return clean.replace(/(\d{4})/g, '$1 ').trim();
}

function showToast(message, type = 'info', duration = 4000) {
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;

  const messageSpan = document.createElement('span');
  messageSpan.className = 'toast-message';
  messageSpan.textContent = String(message);

  const closeBtn = document.createElement('button');
  closeBtn.className = 'toast-close';
  closeBtn.title = 'Dismiss';
  closeBtn.textContent = '×';
  closeBtn.addEventListener('click', () => {
    toast.remove();
  });

  toast.appendChild(messageSpan);
  toast.appendChild(closeBtn);
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
    return 'Dispenser ready: select or enter withdrawal amount in multiples of ₹100';
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

  return `Dispensing notes: ${parts.join(', ')}`;
}

// Auto-format card numbers with spaces as typed
function attachCardNumberFormatter(input) {
  input.addEventListener('input', (e) => {
    let value = e.target.value.replace(/\D/g, '');
    if (value.length > 16) value = value.substring(0, 16);
    const parts = value.match(/.{1,4}/g);
    e.target.value = parts ? parts.join(' ') : value;
  });
}

attachCardNumberFormatter(loginIdentifier);
attachCardNumberFormatter(transferTargetInput);



// ==========================================================================
// Authentication & Session
// ==========================================================================

function getStoredToken() {
  try {
    return sessionStorage.getItem('noir_token') || null;
  } catch (_) {
    return null;
  }
}

function storeToken(token) {
  sessionToken = token;
  try {
    sessionStorage.setItem('noir_token', token);
  } catch (_) {}
}

function clearToken() {
  sessionToken = null;
  currentAccount = null;
  currentTransactions = [];
  try {
    sessionStorage.removeItem('noir_token');
    localStorage.removeItem('atmSessionToken'); // Purge any legacy token
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
      currentAccount = await res.json();
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
  const fullName = user.fullName || 'SIMULATED CARDHOLDER';
  const balance = acc.balance || 0;

  headerAccountNum.textContent = formatCardDisplay(accNum);
  statAccountNumber.textContent = formatCardDisplay(accNum);
  cardAccountNumber.textContent = formatCardDisplay(accNum);
  cardHolderName.textContent = fullName.toUpperCase();
  dashboardBalanceDisplay.textContent = formatRupees(balance);
}

// ==========================================================================
// Event Listeners: Auth Tabs & PIN Visibility
// ==========================================================================

tabLoginBtn.addEventListener('click', () => {
  tabLoginBtn.classList.add('active');
  tabRegisterBtn.classList.remove('active');
  loginView.style.display = 'block';
  registerView.style.display = 'none';
});

tabRegisterBtn.addEventListener('click', () => {
  tabRegisterBtn.classList.add('active');
  tabLoginBtn.classList.remove('active');
  registerView.style.display = 'block';
  loginView.style.display = 'none';
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
      .then(() => showToast('ATM Card Number copied to clipboard', 'success', 2500))
      .catch(() => showToast('Could not copy card number', 'error'));
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
  showToast('ATM Card ejected safely.', 'info');
});

// ==========================================================================
// Form Submission: Login via Card Number & PIN
// ==========================================================================

loginForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const rawAcc = loginIdentifier.value.replace(/\s+/g, '').replace(/-/g, '').trim();
  const pin = loginPin.value.trim();

  if (!rawAcc || rawAcc.length < 8) {
    showToast('Please enter a valid ATM Card / Account Number.', 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }

  loginSubmitBtn.disabled = true;
  loginSubmitBtn.querySelector('span').textContent = 'Reading Card…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ accountNumber: rawAcc, pin: pin })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Authentication failed. Check card number or PIN.', 'error');
      return;
    }

    storeToken(data.sessionToken);
    showToast(`Card Authenticated! Welcome, ${data.fullName || 'Cardholder'}.`, 'success');

    // Fetch complete account info
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
    showToast('Communication error with ATM terminal.', 'error');
  } finally {
    loginSubmitBtn.disabled = false;
    loginSubmitBtn.querySelector('span').textContent = 'Insert Card & Authenticate';
  }
});

// ==========================================================================
// Form Submission: Issue New Card (Registration)
// ==========================================================================

registerForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const fullName = regFullName.value.trim();
  const pin = regPin.value.trim();

  if (!fullName || fullName.length < 2) {
    showToast('Please enter your full legal cardholder name.', 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }

  regSubmitBtn.disabled = true;
  regSubmitBtn.querySelector('span').textContent = 'Embossing Card…';

  try {
    const res = await fetch(`${API_BASE_URL}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ fullName, pin })
    });

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
      showToast(data.message || 'Card issuance failed.', 'error');
      return;
    }

    const newAccNum = data.accountNumber;
    showToast(`New Card Issued: ${formatCardDisplay(newAccNum)} with ₹10,000 balance!`, 'success', 5000);

    // Auto-login with the newly issued card
    const loginRes = await fetch(`${API_BASE_URL}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ accountNumber: newAccNum, pin: pin })
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
      loginIdentifier.value = formatCardDisplay(newAccNum);
      loginPin.value = pin;
    }
  } catch (err) {
    console.error('Registration error:', err);
    showToast('Failed to communicate with ATM backend.', 'error');
  } finally {
    regSubmitBtn.disabled = false;
    regSubmitBtn.querySelector('span').textContent = 'Issue ATM Card & Claim ₹10,000';
  }
});

// ==========================================================================
// Operations: Tabs
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
  showToast('Available balance updated.', 'info', 2000);
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
    showToast('Maximum ATM cash withdrawal per transaction is ₹50,000.', 'error');
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
    dispenserText.textContent = 'Dispenser ready: select or enter withdrawal amount in multiples of ₹100';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Withdrawal error:', err);
    showToast('ATM communication error.', 'error');
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
    showToast('ATM communication error.', 'error');
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
  const target = transferTargetInput.value.replace(/\s+/g, '').replace(/-/g, '').trim();
  const amount = Number(transferAmountInput.value);

  if (!target || !/^\d+$/.test(target)) {
    showToast('Please enter a valid numeric 12-digit recipient account number.', 'error');
    return;
  }
  if (currentAccount && currentAccount.account && target === currentAccount.account.accountNumber) {
    showToast('Self-transfer is not permitted. Please enter a different recipient card number.', 'error');
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

    showToast(`Transferred ${formatRupees(amount)} to Account #${formatCardDisplay(target)} successfully!`, 'success');
    transferTargetInput.value = '';
    transferAmountInput.value = '';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Transfer error:', err);
    showToast('ATM communication error.', 'error');
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

  transactionTableBody.textContent = '';
  filtered.forEach(tx => {
    const isCredit = tx.direction === 'credit';
    const badgeClass = isCredit ? 'badge-credit' : 'badge-debit';
    const amountClass = isCredit ? 'amount-credit' : 'amount-debit';
    const sign = isCredit ? '+' : '-';
    const party = tx.relatedAccount ? `A/C #${formatCardDisplay(tx.relatedAccount)}` : 'Self / ATM Dispenser';

    const tr = document.createElement('tr');

    // 1. Timestamp
    const tdTime = document.createElement('td');
    tdTime.textContent = tx.createdAt || 'Just now';
    tr.appendChild(tdTime);

    // 2. Type badge and description
    const tdDesc = document.createElement('td');
    const badge = document.createElement('span');
    badge.className = `tx-badge ${badgeClass}`;
    badge.textContent = tx.type || 'TX';
    const descText = document.createElement('span');
    descText.style.marginLeft = '8px';
    descText.textContent = tx.description || '';
    tdDesc.appendChild(badge);
    tdDesc.appendChild(descText);
    tr.appendChild(tdDesc);

    // 3. Related Account / Party
    const tdParty = document.createElement('td');
    tdParty.textContent = party;
    tr.appendChild(tdParty);

    // 4. Formatted Amount
    const tdAmount = document.createElement('td');
    tdAmount.className = amountClass;
    tdAmount.textContent = `${sign} ${formatRupees(tx.amount)}`;
    tr.appendChild(tdAmount);

    // 5. Thermal Receipt Button
    const tdAction = document.createElement('td');
    const btn = document.createElement('button');
    btn.className = 'action-icon-btn view-rcpt-btn';
    btn.textContent = 'Receipt';
    btn.addEventListener('click', () => openReceiptModal(tx));
    tdAction.appendChild(btn);
    tr.appendChild(tdAction);

    transactionTableBody.appendChild(tr);
  });
}

// ==========================================================================
// Receipt Modal
// ==========================================================================

function openReceiptModal(tx) {
  rcptTxId.textContent = `#TXN-${String(tx.id).padStart(6, '0')}`;
  rcptDate.textContent = tx.createdAt || new Date().toLocaleString();
  rcptAccount.textContent = currentAccount && currentAccount.account ? formatCardDisplay(currentAccount.account.accountNumber) : '—';
  rcptType.textContent = (tx.type || 'TRANSACTION').toUpperCase();

  if (tx.relatedAccount) {
    rcptPartyRow.style.display = 'flex';
    rcptParty.textContent = `#${formatCardDisplay(tx.relatedAccount)}`;
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

// Initialize on DOM load
document.addEventListener('DOMContentLoaded', () => {
  checkExistingSession();
});
