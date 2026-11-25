/**
 * VAULTX — Secure Access. Smarter Banking.
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
let isAuthSubmitting = false;
let isTxSubmitting = false;

// DOM Elements
const authSection = document.getElementById('authSection');
const dashboardSection = document.getElementById('dashboardSection');
const userQuickInfo = document.getElementById('userQuickInfo');
const headerAccountNum = document.getElementById('headerAccountNum');
const copyAccountBtn = document.getElementById('copyAccountBtn');
const logoutBtn = document.getElementById('logoutBtn');
const themeToggleBtn = document.getElementById('themeToggleBtn');

// Auth DOM
const tabLoginBtn = document.getElementById('tabLoginBtn');
const tabRegisterBtn = document.getElementById('tabRegisterBtn');
const loginView = document.getElementById('loginView');
const registerView = document.getElementById('registerView');
const linkMobileView = document.getElementById('linkMobileView');
const loginForm = document.getElementById('loginForm');
const registerForm = document.getElementById('registerForm');
const linkMobileForm = document.getElementById('linkMobileForm');
const loginCountryCode = document.getElementById('loginCountryCode');
const loginMobile = document.getElementById('loginMobile');
const loginPin = document.getElementById('loginPin');
const loginSubmitBtn = document.getElementById('loginSubmitBtn');
const goToRegisterBtn = document.getElementById('goToRegisterBtn');
const goToLinkMobileBtn = document.getElementById('goToLinkMobileBtn');
const backToLoginBtn = document.getElementById('backToLoginBtn');
const regFullName = document.getElementById('regFullName');
const regCountryCode = document.getElementById('regCountryCode');
const regMobile = document.getElementById('regMobile');
const regPin = document.getElementById('regPin');
const regConfirmPin = document.getElementById('regConfirmPin');
const regSubmitBtn = document.getElementById('regSubmitBtn');
const linkLegacyId = document.getElementById('linkLegacyId');
const linkPin = document.getElementById('linkPin');
const linkCountryCode = document.getElementById('linkCountryCode');
const linkMobile = document.getElementById('linkMobile');
const linkSubmitBtn = document.getElementById('linkSubmitBtn');

// Dashboard DOM
const cardAccountNumber = document.getElementById('cardAccountNumber');
const cardHolderName = document.getElementById('cardHolderName');
const dashboardBalanceDisplay = document.getElementById('dashboardBalanceDisplay');
const statAccountNumber = document.getElementById('statAccountNumber');
const statMobileNumber = document.getElementById('statMobileNumber');
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

// Country validation rules (mirrors backend PhoneUtils.cpp; backend enforces independently)
const COUNTRY_PHONE_RULES = {
  '+91':  { min: 10, max: 10, regex: /^[6-9]\d{9}$/, label: 'Enter a valid 10-digit Indian mobile number (starts with 6-9).' },
  '+1':   { min: 10, max: 10, regex: /^[2-9]\d{9}$/, label: 'Enter a valid 10-digit US/Canada mobile number.' },
  '+44':  { min: 10, max: 10, regex: /^[1-9]\d{9}$/, label: 'Enter a valid 10-digit UK mobile number.' },
  '+61':  { min: 9,  max: 9,  regex: /^[2-9]\d{8}$/, label: 'Enter a valid 9-digit Australian mobile number.' },
  '+65':  { min: 8,  max: 8,  regex: /^[3689]\d{7}$/, label: 'Enter a valid 8-digit Singapore mobile number.' },
  '+971': { min: 9,  max: 9,  regex: /^[2-9]\d{8}$/, label: 'Enter a valid 9-digit UAE mobile number.' },
  '+49':  { min: 10, max: 11, regex: /^[1-9]\d{9,10}$/, label: 'Enter a valid 10-11 digit German mobile number.' },
  '+33':  { min: 9,  max: 9,  regex: /^[1-9]\d{8}$/, label: 'Enter a valid 9-digit French mobile number.' },
  '+81':  { min: 10, max: 10, regex: /^[1-9]\d{9}$/, label: 'Enter a valid 10-digit Japanese mobile number.' }
};

function normalizeMobileInputClient(countryCode, rawNumber) {
  const cc = String(countryCode || '').trim();
  const rule = COUNTRY_PHONE_RULES[cc];
  if (!rule) {
    return { valid: false, error: 'Please select a valid country code.' };
  }

  let cleaned = String(rawNumber || '').replace(/\s+/g, '');
  if (!cleaned) {
    return { valid: false, error: 'Mobile number is required.' };
  }
  if (/[^0-9+\-()]/.test(cleaned)) {
    return { valid: false, error: 'Mobile number may contain digits only (no letters or symbols).' };
  }

  const ccDigits = cc.slice(1);
  let digits = cleaned.replace(/\D/g, '');
  if (digits.length > rule.max && digits.startsWith(ccDigits)) {
    const stripped = digits.slice(ccDigits.length);
    if (stripped.length >= rule.min && stripped.length <= rule.max + 1) {
      digits = stripped;
    }
  }
  if (digits.length === rule.max + 1 && digits.startsWith('0')) {
    digits = digits.slice(1);
  }
  if (!rule.regex.test(digits)) {
    return { valid: false, error: rule.label };
  }
  return { valid: true, countryCode: cc, nationalNumber: digits, e164: `${cc}${digits}` };
}

function attachPhoneDigitFilter(input) {
  if (!input) return;
  input.addEventListener('input', (e) => {
    const digitsOnly = e.target.value.replace(/\D/g, '').slice(0, 15);
    if (e.target.value !== digitsOnly) {
      e.target.value = digitsOnly;
    }
  });
}

// Auto-format card numbers / sanitize phone inputs
function attachCardNumberFormatter(input) {
  if (!input) return;
  input.addEventListener('input', (e) => {
    let value = e.target.value.replace(/\D/g, '');
    if (value.length > 16) value = value.substring(0, 16);
    const parts = value.match(/.{1,4}/g);
    e.target.value = parts ? parts.join(' ') : value;
  });
}

attachPhoneDigitFilter(loginMobile);
attachPhoneDigitFilter(regMobile);
attachPhoneDigitFilter(linkMobile);
attachCardNumberFormatter(transferTargetInput);



// ==========================================================================
// Authentication & Session
// ==========================================================================

function getStoredToken() {
  try {
    return sessionStorage.getItem('vaultx_token') || null;
  } catch (_) {
    return null;
  }
}

function storeToken(token) {
  sessionToken = token;
  try {
    sessionStorage.setItem('vaultx_token', token);
  } catch (_) {}
}

function clearToken() {
  sessionToken = null;
  currentAccount = null;
  currentTransactions = [];
  try {
    sessionStorage.removeItem('vaultx_token');
    localStorage.removeItem('atmSessionToken'); // Purge any legacy token
  } catch (_) {}
}

function handleSessionExpired(message) {
  clearToken();
  showAuthView();
  showToast(message || 'Your session expired. Please re-insert your VAULTX card.', 'error', 5000);
}

async function checkExistingSession() {
  const token = getStoredToken();
  if (!token) {
    showAuthView();
    return;
  }

  sessionToken = token;
  try {
    const accRes = await apiRequest('/api/account', {
      headers: { 'Authorization': `Bearer ${sessionToken}` }
    }, 'Could not verify active session');

    if (accRes.ok) {
      currentAccount = accRes.data;
      showDashboardView();
      await fetchTransactions();
    } else if (accRes.status === 401) {
      clearToken();
      showAuthView();
    } else {
      showAuthView();
      showToast(accRes.errorMessage, 'error');
    }
  } catch (err) {
    console.error('Session check error:', err);
    showAuthView();
    showToast('VAULTX backend is currently unreachable. Please retry shortly.', 'error');
  }
}

async function apiRequest(path, options = {}, fallbackErrorMessage = 'Request failed.') {
  const res = await fetch(`${API_BASE_URL}${path}`, options);
  const rawText = await res.text();
  let data = null;

  if (rawText) {
    try {
      data = JSON.parse(rawText);
    } catch (_) {
      data = null;
    }
  }

  if (res.ok) {
    return { ok: true, status: res.status, data: data || {}, errorMessage: '' };
  }

  let errorMessage = (data && typeof data.message === 'string' && data.message.trim())
    ? data.message.trim()
    : '';

  if (!errorMessage) {
    if (res.status === 404) {
      errorMessage = 'VAULTX backend API is currently unreachable (HTTP 404). Please try again shortly.';
    } else if (res.status === 429) {
      errorMessage = 'Too many requests. Please wait a moment and try again.';
    } else if (res.status === 502 || res.status === 503 || res.status === 504) {
      errorMessage = `VAULTX backend service is temporarily unavailable (HTTP ${res.status}). Please retry in a few seconds.`;
    } else {
      errorMessage = `${fallbackErrorMessage} (HTTP ${res.status}).`;
    }
  }

  return { ok: false, status: res.status, data: data || {}, errorMessage };
}

// ==========================================================================
// Theme Management (Light / Dark Mode)
// ==========================================================================

const THEME_STORAGE_KEY = 'vaultx_theme';

function getSavedTheme() {
  try {
    const saved = localStorage.getItem(THEME_STORAGE_KEY);
    return (saved === 'dark' || saved === 'light') ? saved : null;
  } catch (_) {
    return null;
  }
}

function getSystemTheme() {
  if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
    return 'dark';
  }
  return 'light';
}

function syncThemeToggleUI(theme) {
  if (!themeToggleBtn) return;
  const isDark = theme === 'dark';
  const nextLabel = isDark ? 'Switch to Light Mode' : 'Switch to Dark Mode';
  themeToggleBtn.setAttribute('aria-pressed', String(isDark));
  themeToggleBtn.setAttribute('aria-label', nextLabel);
  themeToggleBtn.setAttribute('title', nextLabel);
}

function applyTheme(theme, animate = false) {
  const normalized = theme === 'dark' ? 'dark' : 'light';
  const root = document.documentElement;
  const prefersReducedMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  if (animate && !prefersReducedMotion) {
    root.classList.add('theme-transition');
    window.clearTimeout(applyTheme._timer);
    applyTheme._timer = window.setTimeout(() => {
      root.classList.remove('theme-transition');
    }, 280);
  }

  root.setAttribute('data-theme', normalized);
  root.style.colorScheme = normalized;
  syncThemeToggleUI(normalized);
}

function initTheme() {
  const initialTheme = getSavedTheme() || document.documentElement.getAttribute('data-theme') || getSystemTheme();
  applyTheme(initialTheme, false);

  if (themeToggleBtn) {
    themeToggleBtn.addEventListener('click', () => {
      const current = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
      const next = current === 'dark' ? 'light' : 'dark';
      try {
        localStorage.setItem(THEME_STORAGE_KEY, next);
      } catch (_) {}
      applyTheme(next, true);
    });
  }

  if (window.matchMedia) {
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const onSystemChange = (e) => {
      // Manual user selection always takes priority over OS theme changes
      if (!getSavedTheme()) {
        applyTheme(e.matches ? 'dark' : 'light', true);
      }
    };
    if (typeof mediaQuery.addEventListener === 'function') {
      mediaQuery.addEventListener('change', onSystemChange);
    } else if (typeof mediaQuery.addListener === 'function') {
      mediaQuery.addListener(onSystemChange);
    }
  }
}

function resetPinVisibility() {
  document.querySelectorAll('.pin-toggle-btn').forEach(btn => {
    const targetId = btn.getAttribute('data-target');
    const input = targetId ? document.getElementById(targetId) : null;
    if (input) input.type = 'password';
    btn.classList.remove('is-visible');
    btn.style.removeProperty('color');
  });
}

function clearDashboardUI() {
  headerAccountNum.textContent = '—';
  statAccountNumber.textContent = '—';
  if (statMobileNumber) statMobileNumber.textContent = '—';
  cardAccountNumber.textContent = '•••• •••• •••• ••••';
  cardHolderName.textContent = 'SIMULATED USER';
  dashboardBalanceDisplay.textContent = '₹ 0.00';
  dispenserText.textContent = 'Dispenser ready: select or enter withdrawal amount';
  withdrawForm.reset();
  depositForm.reset();
  transferForm.reset();
  if (linkMobileForm) linkMobileForm.reset();
  receiptModal.style.display = 'none';
  currentFilter = 'all';
  filterPills.forEach(p => {
    p.classList.toggle('active', p.getAttribute('data-filter') === 'all');
  });
  transactionTableBody.innerHTML = '<tr><td colspan="5" class="empty-state">Loading transaction records...</td></tr>';
}

function showAuthPanel(panel) {
  if (loginView) loginView.style.display = panel === 'login' ? 'block' : 'none';
  if (registerView) registerView.style.display = panel === 'register' ? 'block' : 'none';
  if (linkMobileView) linkMobileView.style.display = panel === 'link' ? 'block' : 'none';
  tabLoginBtn.classList.toggle('active', panel !== 'register');
  tabRegisterBtn.classList.toggle('active', panel === 'register');
  resetPinVisibility();
}

function showAuthView() {
  authSection.style.display = 'block';
  dashboardSection.style.display = 'none';
  userQuickInfo.style.display = 'none';
  clearDashboardUI();
  showAuthPanel('login');
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
  const mobile = user.mobileNumber || 'Not linked';
  const balance = acc.balance || 0;

  headerAccountNum.textContent = formatCardDisplay(accNum);
  statAccountNumber.textContent = formatCardDisplay(accNum);
  if (statMobileNumber) statMobileNumber.textContent = mobile;
  cardAccountNumber.textContent = formatCardDisplay(accNum);
  cardHolderName.textContent = fullName.toUpperCase();
  dashboardBalanceDisplay.textContent = formatRupees(balance);
}

// ==========================================================================
// Event Listeners: Auth Tabs & PIN Visibility
// ==========================================================================

tabLoginBtn.addEventListener('click', () => {
  if (isAuthSubmitting) return;
  showAuthPanel('login');
});

tabRegisterBtn.addEventListener('click', () => {
  if (isAuthSubmitting) return;
  showAuthPanel('register');
});

if (goToRegisterBtn) {
  goToRegisterBtn.addEventListener('click', () => {
    if (isAuthSubmitting) return;
    showAuthPanel('register');
  });
}

if (goToLinkMobileBtn) {
  goToLinkMobileBtn.addEventListener('click', () => {
    if (isAuthSubmitting) return;
    showAuthPanel('link');
  });
}

if (backToLoginBtn) {
  backToLoginBtn.addEventListener('click', () => {
    if (isAuthSubmitting) return;
    showAuthPanel('login');
  });
}

document.querySelectorAll('.pin-toggle-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const targetId = btn.getAttribute('data-target');
    const input = document.getElementById(targetId);
    if (!input) return;
    if (input.type === 'password') {
      input.type = 'text';
      btn.classList.add('is-visible');
    } else {
      input.type = 'password';
      btn.classList.remove('is-visible');
    }
  });
});

copyAccountBtn.addEventListener('click', () => {
  if (currentAccount && currentAccount.account) {
    navigator.clipboard.writeText(currentAccount.account.accountNumber)
      .then(() => showToast('VAULTX Card Number copied to clipboard', 'success', 2500))
      .catch(() => showToast('Could not copy card number', 'error'));
  }
});

logoutBtn.addEventListener('click', async () => {
  if (logoutBtn.disabled) return;
  logoutBtn.disabled = true;
  try {
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
    showToast('VAULTX Card ejected safely.', 'info');
  } finally {
    logoutBtn.disabled = false;
  }
});

// ==========================================================================
// Form Submission: Login via Mobile Number & 6-Digit PIN
// ==========================================================================

async function applyAuthenticatedPayload(data, fallbackName = 'Cardholder', fallbackMobile = '') {
  storeToken(data.sessionToken);

  const accRes = await apiRequest('/api/account', {
    headers: { 'Authorization': `Bearer ${sessionToken}` }
  });

  if (accRes.ok) {
    currentAccount = accRes.data;
  } else {
    currentAccount = {
      user: {
        fullName: data.fullName || fallbackName,
        email: data.email || '',
        mobileNumber: data.mobileNumber || fallbackMobile
      },
      account: {
        accountNumber: data.accountNumber,
        balance: typeof data.balance === 'number' ? data.balance : 10000.0,
        balancePaise: typeof data.balancePaise === 'number' ? data.balancePaise : 1000000
      }
    };
  }

  showDashboardView();
  await fetchTransactions();
}

loginForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  if (isAuthSubmitting || loginSubmitBtn.disabled) return;

  const phoneCheck = normalizeMobileInputClient(loginCountryCode.value, loginMobile.value);
  const pin = loginPin.value.trim();

  if (!phoneCheck.valid) {
    showToast(phoneCheck.error, 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }

  isAuthSubmitting = true;
  loginSubmitBtn.disabled = true;
  loginSubmitBtn.querySelector('span').textContent = 'Authenticating…';

  try {
    const authRes = await apiRequest(
      '/api/auth/login',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          countryCode: phoneCheck.countryCode,
          mobileNumber: phoneCheck.nationalNumber,
          mobile_number: phoneCheck.e164,
          pin: pin
        })
      },
      'Authentication failed. Check mobile number or PIN.'
    );

    if (!authRes.ok) {
      showToast(authRes.errorMessage, 'error');
      return;
    }

    const data = authRes.data;
    loginForm.reset();
    resetPinVisibility();
    showToast(`Authenticated! Welcome, ${data.fullName || 'Cardholder'}.`, 'success');
    await applyAuthenticatedPayload(data, 'Cardholder', phoneCheck.e164);
  } catch (err) {
    console.error('Login error:', err);
    showToast('Communication error with VAULTX terminal.', 'error');
  } finally {
    isAuthSubmitting = false;
    loginSubmitBtn.disabled = false;
    loginSubmitBtn.querySelector('span').textContent = 'Login with Mobile & PIN';
  }
});

// ==========================================================================
// Form Submission: Issue New Card (Registration with Mobile & Confirm PIN)
// ==========================================================================

registerForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  if (isAuthSubmitting || regSubmitBtn.disabled) return;

  const fullName = regFullName.value.trim();
  const phoneCheck = normalizeMobileInputClient(regCountryCode.value, regMobile.value);
  const pin = regPin.value.trim();
  const confirmPin = regConfirmPin.value.trim();

  if (!fullName || fullName.length < 2 || fullName.length > 100) {
    showToast('Cardholder full name must be between 2 and 100 characters.', 'error');
    return;
  }
  if (!phoneCheck.valid) {
    showToast(phoneCheck.error, 'error');
    return;
  }
  if (!/^\d{6}$/.test(pin)) {
    showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
    return;
  }
  if (pin !== confirmPin) {
    showToast('PIN and Confirm PIN do not match.', 'error');
    return;
  }

  isAuthSubmitting = true;
  regSubmitBtn.disabled = true;
  regSubmitBtn.querySelector('span').textContent = 'Embossing Card…';

  try {
    const regRes = await apiRequest(
      '/api/auth/register',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          fullName,
          countryCode: phoneCheck.countryCode,
          mobileNumber: phoneCheck.nationalNumber,
          mobile_number: phoneCheck.e164,
          pin,
          confirmPin
        })
      },
      'Card issuance failed'
    );

    if (!regRes.ok) {
      showToast(regRes.errorMessage, 'error');
      return;
    }

    const data = regRes.data;
    const newAccNum = data.accountNumber;
    const issuedMobile = data.mobileNumber || phoneCheck.e164;
    registerForm.reset();
    resetPinVisibility();
    showToast(`New Card Issued: ${formatCardDisplay(newAccNum)} linked to ${issuedMobile} (₹10,000 balance)!`, 'success', 5000);

    // Auto-login with registered mobile number & PIN
    regSubmitBtn.querySelector('span').textContent = 'Activating Terminal…';
    const loginRes = await apiRequest(
      '/api/auth/login',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          mobile_number: issuedMobile,
          pin: pin
        })
      },
      'Card issued, please sign in with your mobile number.'
    );

    if (loginRes.ok && loginRes.data.sessionToken) {
      await applyAuthenticatedPayload(loginRes.data, fullName, issuedMobile);
    } else {
      isAuthSubmitting = false;
      showAuthPanel('login');
      loginCountryCode.value = phoneCheck.countryCode;
      loginMobile.value = phoneCheck.nationalNumber;
      showToast(`Card #${formatCardDisplay(newAccNum)} created, but auto-login failed: ${loginRes.errorMessage}`, 'error', 6000);
    }
  } catch (err) {
    console.error('Registration error:', err);
    showToast('Failed to communicate with VAULTX backend.', 'error');
  } finally {
    isAuthSubmitting = false;
    regSubmitBtn.disabled = false;
    regSubmitBtn.querySelector('span').textContent = 'Issue VAULTX Card & Claim ₹10,000';
  }
});

// ==========================================================================
// Form Submission: Link Mobile Number to Legacy Unlinked Account
// ==========================================================================

if (linkMobileForm) {
  linkMobileForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (isAuthSubmitting || linkSubmitBtn.disabled) return;

    const identifier = linkLegacyId.value.trim();
    const pin = linkPin.value.trim();
    const phoneCheck = normalizeMobileInputClient(linkCountryCode.value, linkMobile.value);

    if (!identifier || identifier.length < 3) {
      showToast('Enter your existing 12-digit Card Number or registered Email.', 'error');
      return;
    }
    if (!/^\d{6}$/.test(pin)) {
      showToast('ATM PIN must be exactly 6 numeric digits.', 'error');
      return;
    }
    if (!phoneCheck.valid) {
      showToast(phoneCheck.error, 'error');
      return;
    }

    isAuthSubmitting = true;
    linkSubmitBtn.disabled = true;
    linkSubmitBtn.querySelector('span').textContent = 'Linking Mobile…';

    try {
      const linkRes = await apiRequest(
        '/api/auth/link-mobile',
        {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            identifier,
            pin,
            countryCode: phoneCheck.countryCode,
            mobileNumber: phoneCheck.nationalNumber,
            mobile_number: phoneCheck.e164
          })
        },
        'Could not link mobile number'
      );

      if (!linkRes.ok || !linkRes.data.sessionToken) {
        showToast(linkRes.errorMessage, 'error');
        return;
      }

      linkMobileForm.reset();
      resetPinVisibility();
      showToast(`Mobile ${linkRes.data.mobileNumber || phoneCheck.e164} linked successfully!`, 'success', 4500);
      await applyAuthenticatedPayload(linkRes.data, 'Cardholder', phoneCheck.e164);
    } catch (err) {
      console.error('Link mobile error:', err);
      showToast('Communication error with VAULTX terminal.', 'error');
    } finally {
      isAuthSubmitting = false;
      linkSubmitBtn.disabled = false;
      linkSubmitBtn.querySelector('span').textContent = 'Link Mobile Number & Sign In';
    }
  });
}

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
  if (refreshBalanceBtn.disabled) return;
  refreshBalanceBtn.disabled = true;
  try {
    const updated = await refreshAccountBalance(true);
    if (updated) {
      showToast('Available balance updated.', 'info', 2000);
    }
  } finally {
    refreshBalanceBtn.disabled = false;
  }
});

async function refreshAccountBalance(showErrors = false) {
  if (!sessionToken) return false;
  try {
    const res = await apiRequest(
      '/api/account',
      { headers: { 'Authorization': `Bearer ${sessionToken}` } },
      'Could not refresh account balance'
    );
    if (res.ok) {
      currentAccount = res.data;
      updateDashboardUI();
      return true;
    }
    if (res.status === 401) {
      handleSessionExpired(res.errorMessage);
      return false;
    }
    if (showErrors) {
      showToast(res.errorMessage, 'error');
    }
    return false;
  } catch (err) {
    console.error('Refresh balance error:', err);
    if (showErrors) {
      showToast('Could not reach VAULTX backend to refresh balance.', 'error');
    }
    return false;
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
  if (isTxSubmitting || withdrawSubmitBtn.disabled) return;
  const amount = Number(withdrawAmountInput.value);

  if (!amount || amount <= 0 || amount % 100 !== 0) {
    showToast('Withdrawal amount must be in multiples of ₹100.', 'error');
    return;
  }
  if (amount > 50000) {
    showToast('Maximum cash withdrawal per transaction is ₹50,000.', 'error');
    return;
  }
  if (currentAccount && currentAccount.account && amount > currentAccount.account.balance) {
    showToast('Insufficient funds for this withdrawal amount.', 'error');
    return;
  }

  isTxSubmitting = true;
  withdrawSubmitBtn.disabled = true;
  withdrawSubmitBtn.querySelector('span').textContent = 'Dispensing Notes…';

  try {
    const res = await apiRequest(
      '/api/withdraw',
      {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${sessionToken}`
        },
        body: JSON.stringify({ amount: amount })
      },
      'Withdrawal failed'
    );

    if (!res.ok) {
      if (res.status === 401) {
        handleSessionExpired(res.errorMessage);
      } else {
        showToast(res.errorMessage, 'error');
      }
      return;
    }

    showToast(`Please collect your simulated cash: ${formatRupees(amount)}`, 'success', 5000);
    withdrawAmountInput.value = '';
    dispenserText.textContent = 'Dispenser ready: select or enter withdrawal amount in multiples of ₹100';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Withdrawal error:', err);
    showToast('VAULTX communication error.', 'error');
  } finally {
    isTxSubmitting = false;
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
  if (isTxSubmitting || depositSubmitBtn.disabled) return;
  const amount = Number(depositAmountInput.value);

  if (!amount || amount <= 0) {
    showToast('Deposit amount must be positive.', 'error');
    return;
  }
  if (Math.round(amount * 100) !== amount * 100) {
    showToast('Deposit amount cannot exceed two decimal places.', 'error');
    return;
  }

  isTxSubmitting = true;
  depositSubmitBtn.disabled = true;
  depositSubmitBtn.querySelector('span').textContent = 'Processing Deposit…';

  try {
    const res = await apiRequest(
      '/api/deposit',
      {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${sessionToken}`
        },
        body: JSON.stringify({ amount: amount })
      },
      'Deposit failed'
    );

    if (!res.ok) {
      if (res.status === 401) {
        handleSessionExpired(res.errorMessage);
      } else {
        showToast(res.errorMessage, 'error');
      }
      return;
    }

    showToast(`Deposit of ${formatRupees(amount)} credited successfully!`, 'success');
    depositAmountInput.value = '';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Deposit error:', err);
    showToast('VAULTX communication error.', 'error');
  } finally {
    isTxSubmitting = false;
    depositSubmitBtn.disabled = false;
    depositSubmitBtn.querySelector('span').textContent = 'Confirm Cash Deposit';
  }
});

// ==========================================================================
// Operations: Fund Transfer
// ==========================================================================

transferForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  if (isTxSubmitting || transferSubmitBtn.disabled) return;
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

  isTxSubmitting = true;
  transferSubmitBtn.disabled = true;
  transferSubmitBtn.querySelector('span').textContent = 'Executing Transfer…';

  try {
    const res = await apiRequest(
      '/api/transfer',
      {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${sessionToken}`
        },
        body: JSON.stringify({ targetAccount: target, amount: amount })
      },
      'Transfer failed'
    );

    if (!res.ok) {
      if (res.status === 401) {
        handleSessionExpired(res.errorMessage);
      } else {
        showToast(res.errorMessage, 'error');
      }
      return;
    }

    showToast(`Transferred ${formatRupees(amount)} to Account #${formatCardDisplay(target)} successfully!`, 'success');
    transferTargetInput.value = '';
    transferAmountInput.value = '';

    await refreshAccountBalance();
    await fetchTransactions();
  } catch (err) {
    console.error('Transfer error:', err);
    showToast('VAULTX communication error.', 'error');
  } finally {
    isTxSubmitting = false;
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
    const res = await apiRequest(
      '/api/transactions?limit=50',
      { headers: { 'Authorization': `Bearer ${sessionToken}` } },
      'Could not load transaction history'
    );

    if (res.ok) {
      currentTransactions = res.data.transactions || [];
      renderTransactions();
    } else if (res.status === 401) {
      handleSessionExpired(res.errorMessage);
    } else {
      showToast(res.errorMessage, 'error');
    }
  } catch (err) {
    console.error('Fetch transactions error:', err);
    showToast('Failed to load transaction ledger.', 'error');
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
    const party = tx.relatedAccount ? `A/C #${formatCardDisplay(tx.relatedAccount)}` : 'Self / VAULTX Dispenser';

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
  initTheme();
  checkExistingSession();
});
