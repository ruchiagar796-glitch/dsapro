/* Customer portal. Every number shown comes from the server; after each money operation the
   balance and history are re-read from the server (nothing is updated optimistically).
   Security does not depend on this file: the server checks the session on every request. */
(function () {
  'use strict';
  const B = window.Bank, $ = B.$, esc = B.esc, icon = B.icon;
  const view = $('#view');
  let account = null;   // last account data from the server
  let profile = null;

  const NAV = [
    ['dashboard', 'Dashboard', 'home'], ['account', 'My Account', 'card'], ['deposit', 'Deposit Money', 'plus'],
    ['withdraw', 'Withdraw Money', 'minus'], ['transfer', 'Transfer Money', 'send'],
    ['history', 'Transaction History', 'list'], ['profile', 'Profile & Settings', 'settings'], ['password', 'Change Password', 'lock']
  ];

  const store = {
    get: (k) => { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set: (k, v) => { try { localStorage.setItem(k, v); } catch (e) { /* private mode */ } }
  };

  function buildNav(active) {
    $('#nav').innerHTML = NAV.map((n) => '<a href="#/' + n[0] + '"' + (n[0] === active ? ' class="active" aria-current="page"' : '') + '>' + icon(n[2]) + '<span>' + n[1] + '</span></a>').join('') +
      '<div class="spacer"></div><hr><button id="logoutBtn">' + icon('logout') + '<span>Logout</span></button>';
    $('#logoutBtn').onclick = async () => {
      if (await B.confirm({ title: 'Log out?', message: 'You will need to sign in again to access your account.', confirmText: 'Log out' })) B.logout('/login');
    };
  }

  function errorView(message, retry) {
    view.innerHTML = B.errorState(message);
    $('#retry').onclick = retry;
  }

  async function loadAccount() {
    const r = await B.get('/api/me/account');
    if (r.ok) account = r.data;
    return r;
  }

  const deactivatedNotice = () => account && account.status !== 'active'
    ? '<div class="alert warn">' + icon('alert') + '<div><b>Your account is deactivated.</b><br>You can view your information, but deposits, withdrawals and transfers are disabled. Please contact the bank.</div></div>' : '';

  // ------------------------------------------------------------------ dashboard
  async function dashboard() {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/me/overview');
    if (!r.ok) return errorView(r.error.message, dashboard);
    const d = r.data; account = d.account; profile = d.profile;
    const hidden = store.get('bp.hide') === '1';
    const first = profile.fullName.split(' ')[0];
    view.innerHTML =
      '<div class="page-title"><h2>Welcome back, ' + esc(first) + '</h2><p class="muted">Customer ID: <b>' + esc(profile.customerId) + '</b></p></div>' + deactivatedNotice() +
      '<div class="balance-card"><div class="lbl">Available balance</div>' +
      '<div class="bal"><span id="bal"></span><button class="eye" id="eyeBtn" aria-label="Hide or show balance"></button></div>' +
      '<div class="acct"><span>' + esc(account.accountType) + ' account</span><span>Account no. <code id="acctNo"></code></span>' +
      '<button class="btn btn-sm btn-outline-white" id="revealBtn">Show</button> ' + B.statusBadge(account.status) + '</div></div>' +
      '<div class="quick mt"><a href="#/deposit"><span class="ico">' + icon('plus') + '</span>Deposit</a><a href="#/withdraw"><span class="ico">' + icon('minus') + '</span>Withdraw</a>' +
      '<a href="#/transfer"><span class="ico">' + icon('send') + '</span>Transfer</a><a href="#/history"><span class="ico">' + icon('list') + '</span>History</a></div>' +
      '<div class="grid g4 mt">' +
      stat('arrowdown', 'green', 'Total deposits', d.summary.totalDeposits) + stat('arrowup', 'red', 'Total withdrawals', d.summary.totalWithdrawals) +
      stat('send', '', 'Transfers sent', d.summary.totalTransfersOut) + stat('inbox', 'amber', 'Transfers received', d.summary.totalTransfersIn) + '</div>' +
      '<div class="card mt"><div class="card-head"><h2>Recent transactions</h2><a class="btn btn-sm btn-ghost" href="#/history">View all</a></div><div id="recent"></div></div>';

    let hide = hidden, reveal = false;
    const paint = () => {
      $('#bal').textContent = hide ? '\u20B9 \u2022\u2022\u2022\u2022\u2022\u2022' : B.money(account.balance);
      $('#eyeBtn').innerHTML = icon(hide ? 'eyeoff' : 'eye');
      $('#eyeBtn').setAttribute('aria-pressed', String(hide));
      $('#acctNo').textContent = reveal ? account.accountNumber : account.accountNumberMasked;
      $('#revealBtn').textContent = reveal ? 'Hide' : 'Show';
    };
    $('#eyeBtn').onclick = () => { hide = !hide; store.set('bp.hide', hide ? '1' : '0'); paint(); };
    $('#revealBtn').onclick = () => { reveal = !reveal; paint(); };
    paint();
    $('#recent').innerHTML = d.recentTransactions.length
      ? '<ul class="tx-list">' + d.recentTransactions.map(txItem).join('') + '</ul>'
      : B.emptyState('inbox', 'No transactions yet', 'Make a deposit to get started. Your activity will appear here.') + '<div class="center"><a class="btn" href="#/deposit">Make a deposit</a></div>';
  }
  function stat(ico, color, label, amount) {
    return '<div class="card stat"><div class="ico ' + color + '">' + icon(ico) + '</div><div><div class="lbl">' + label + '</div><div class="val">' + B.money(amount) + '</div></div></div>';
  }
  function txItem(t) {
    const credit = t.direction === 'credit';
    return '<li><span class="dir ' + t.direction + '">' + icon(credit ? 'arrowdown' : 'arrowup', 'sm') + '</span><div class="meta"><b>' + esc(t.label) + '</b><span class="small muted">' + esc(t.description) + ' \u00B7 ' + B.dateTime(t.timestamp) + '</span></div>' +
      '<span class="amt ' + t.direction + '">' + (credit ? '+' : '\u2212') + B.money(t.amount) + '</span></li>';
  }

  // ------------------------------------------------------------------ my account
  async function accountView() {
    view.innerHTML = B.skeleton();
    const [a, p] = await Promise.all([B.get('/api/me/account'), B.get('/api/me/profile')]);
    if (!a.ok) return errorView(a.error.message, accountView);
    if (!p.ok) return errorView(p.error.message, accountView);
    account = a.data; profile = p.data;
    let reveal = false;
    view.innerHTML = deactivatedNotice() + '<div class="grid g2"><div class="card"><div class="card-head"><h2>Account details</h2>' + B.statusBadge(account.status) + '</div><dl class="dl">' +
      '<dt>Account holder</dt><dd>' + esc(account.holderName) + '</dd><dt>Account type</dt><dd>' + esc(account.accountType) + '</dd>' +
      '<dt>Account number</dt><dd><span id="an"></span> <button class="btn btn-sm btn-ghost" id="rv">Show</button> <button class="btn btn-sm btn-ghost" id="cp">' + icon('copy', 'sm') + ' Copy</button></dd>' +
      '<dt>Customer ID</dt><dd>' + esc(profile.customerId) + '</dd><dt>Opened on</dt><dd>' + B.date(account.openedAt) + '</dd><dt>Current balance</dt><dd>' + B.money(account.balance) + '</dd></dl></div>' +
      '<div class="card"><div class="card-head"><h2>Limits &amp; activity</h2></div><dl class="dl"><dt>Max per withdrawal / transfer</dt><dd>' + B.money(account.maxWithdrawalPerTransaction) + '</dd>' +
      '<dt>Max per transaction</dt><dd>' + B.money(account.maxTransactionAmount) + '</dd><dt>Total deposits</dt><dd>' + B.money(account.summary.totalDeposits) + '</dd>' +
      '<dt>Total withdrawals</dt><dd>' + B.money(account.summary.totalWithdrawals) + '</dd><dt>Transactions</dt><dd>' + account.transactionCount + '</dd></dl></div></div>';
    const paint = () => { $('#an').textContent = reveal ? account.accountNumber : account.accountNumberMasked; $('#rv').textContent = reveal ? 'Hide' : 'Show'; };
    $('#rv').onclick = () => { reveal = !reveal; paint(); };
    $('#cp').onclick = () => B.copy(String(account.accountNumber));
    paint();
  }

  // ------------------------------------------------------------------ deposit / withdraw
  async function moneyForm(kind) {
    view.innerHTML = B.skeleton();
    const r = await loadAccount();
    if (!r.ok) return errorView(r.error.message, () => moneyForm(kind));
    const isDep = kind === 'deposit';
    const blocked = account.status !== 'active';
    view.innerHTML = deactivatedNotice() +
      '<div class="grid g2"><div class="card"><div class="card-head"><h2>' + (isDep ? 'Deposit money' : 'Withdraw money') + '</h2></div>' +
      '<p class="muted">Available balance: <b id="curBal">' + B.money(account.balance) + '</b></p>' +
      '<form id="form" novalidate><div class="form-alert hidden"></div><div class="field"><label for="amount">Amount</label>' +
      '<div class="input-prefix"><span>\u20B9</span><input class="input" id="amount" name="amount" inputmode="decimal" autocomplete="off" placeholder="0.00"></div>' +
      '<div class="hint">' + (isDep ? 'Up to ' + B.money(account.maxTransactionAmount) + ' per deposit.' : 'Up to ' + B.money(account.maxWithdrawalPerTransaction) + ' per withdrawal.') + '</div>' +
      '<div class="field-error" data-error-for="amount"></div><div class="chips">' + [500, 1000, 5000, 10000].map((v) => '<button type="button" class="chip" data-v="' + v + '">' + B.money(v).replace('.00', '') + '</button>').join('') + '</div></div>' +
      '<button class="btn btn-block ' + (isDep ? 'btn-success' : '') + '" type="submit"' + (blocked ? ' disabled' : '') + '>' + (isDep ? 'Deposit' : 'Withdraw') + '</button></form></div>' +
      '<div class="card" id="result"><div class="card-head"><h2>How it works</h2></div><p class="muted">' + (isDep
        ? 'Enter an amount and confirm. The server checks it, updates your balance and records the transaction.'
        : 'You can withdraw up to your available balance. Every withdrawal is checked by the server before your balance changes.') + '</p></div></div>';
    const form = $('#form'), input = form.elements.amount;
    let key = B.newKey();
    input.addEventListener('input', () => { key = B.newKey(); }); // a new amount is a new request
    $('.chips', view).addEventListener('click', (e) => { if (e.target.dataset.v) { input.value = e.target.dataset.v; key = B.newKey(); input.focus(); } });
    form.addEventListener('submit', async (e) => {
      e.preventDefault(); B.clearErrors(form);
      const v = input.value.trim().replace(/,/g, '');
      if (!/^\d{1,10}(\.\d{1,2})?$/.test(v) || Number(v) <= 0) return B.fieldError(form, 'amount', 'Enter an amount greater than zero, with at most 2 decimals');
      const btn = $('button[type=submit]', form);
      B.setLoading(btn, true, 'Processing\u2026');
      const res = await B.post('/api/me/' + kind, { amount: v, idempotencyKey: key });
      B.setLoading(btn, false);
      if (!res.ok) return B.showServerErrors(form, res.error);
      key = B.newKey(); input.value = '';
      B.toast(res.message, 'success');
      const fresh = await loadAccount();   // show what the server says, not what we guess
      if (fresh.ok) $('#curBal').textContent = B.money(account.balance);
      const t = res.data.transaction;
      $('#result').innerHTML = '<div class="success-box"><div class="tick">' + icon('check', 'lg') + '</div><h2>' + esc(res.message) + '</h2><p class="muted">Reference <b>' + esc(t.reference) + '</b></p>' +
        '<p>New balance: <b>' + B.money(res.data.account.balance) + '</b></p><a class="btn btn-light" href="#/history">View history</a></div>';
    });
  }

  // ------------------------------------------------------------------ transfer
  async function transfer() {
    view.innerHTML = B.skeleton();
    const r = await loadAccount();
    if (!r.ok) return errorView(r.error.message, transfer);
    const blocked = account.status !== 'active';
    view.innerHTML = deactivatedNotice() +
      '<div class="grid g2"><div class="card"><div class="card-head"><h2>Transfer money</h2></div><p class="muted">Available balance: <b id="curBal">' + B.money(account.balance) + '</b></p>' +
      '<form id="form" novalidate><div class="form-alert hidden"></div>' +
      '<div class="field"><label for="toAccount">Recipient account number</label><div class="row"><input class="input grow" id="toAccount" name="toAccount" inputmode="numeric" autocomplete="off" maxlength="15" placeholder="e.g. 1000000002"><button type="button" class="btn btn-ghost" id="verify">Verify</button></div>' +
      '<div class="field-error" data-error-for="toAccount"></div><div class="hint" id="recipient"></div></div>' +
      '<div class="field"><label for="amount">Amount</label><div class="input-prefix"><span>\u20B9</span><input class="input" id="amount" name="amount" inputmode="decimal" autocomplete="off" placeholder="0.00"></div>' +
      '<div class="hint">Up to ' + B.money(account.maxWithdrawalPerTransaction) + ' per transfer.</div><div class="field-error" data-error-for="amount"></div></div>' +
      '<button class="btn btn-block" type="submit"' + (blocked ? ' disabled' : '') + '>' + icon('send') + ' Review &amp; send</button></form></div>' +
      '<div class="card" id="result"><div class="card-head"><h2>Before you send</h2></div><ul class="muted"><li>Check the account number carefully.</li><li>Use <b>Verify</b> to confirm the recipient\u2019s name (partly hidden for privacy).</li><li>Transfers cannot be undone by you.</li></ul></div></div>';
    const form = $('#form'), to = form.elements.toAccount, amt = form.elements.amount, rec = $('#recipient');
    let key = B.newKey(), verified = null;
    const rekey = () => { key = B.newKey(); };
    to.addEventListener('input', () => { verified = null; rec.textContent = ''; rec.className = 'hint'; rekey(); });
    amt.addEventListener('input', rekey);

    async function verify() {
      B.clearErrors(form);
      const n = to.value.trim();
      if (!/^\d{1,15}$/.test(n)) { B.fieldError(form, 'toAccount', 'Enter the recipient\u2019s account number (digits only)'); return null; }
      rec.textContent = 'Checking\u2026';
      const res = await B.get('/api/me/recipient?account=' + encodeURIComponent(n));
      if (!res.ok) { rec.textContent = ''; B.fieldError(form, 'toAccount', res.error.message); return null; }
      verified = res.data;
      rec.innerHTML = icon('check', 'sm') + ' Recipient: <b></b>'; $('b', rec).textContent = res.data.maskedName;
      rec.className = 'hint ok-text';
      return verified;
    }
    $('#verify').onclick = verify;

    form.addEventListener('submit', async (e) => {
      e.preventDefault(); B.clearErrors(form);
      const v = amt.value.trim().replace(/,/g, '');
      if (!/^\d{1,10}(\.\d{1,2})?$/.test(v) || Number(v) <= 0) return B.fieldError(form, 'amount', 'Enter an amount greater than zero, with at most 2 decimals');
      const who = verified || await verify();
      if (!who) return;
      const ok = await B.confirm({ title: 'Confirm transfer', html: '<p>Send <b>' + esc(B.money(v)) + '</b> to <b>' + esc(who.maskedName) + '</b> (account ' + esc(who.accountNumber) + ')?</p>', confirmText: 'Send money' });
      if (!ok) return;
      const btn = $('button[type=submit]', form);
      B.setLoading(btn, true, 'Sending\u2026');
      const res = await B.post('/api/me/transfer', { toAccount: String(who.accountNumber), amount: v, idempotencyKey: key });
      B.setLoading(btn, false);
      if (!res.ok) return B.showServerErrors(form, res.error);
      key = B.newKey(); amt.value = ''; to.value = ''; verified = null; rec.textContent = '';
      B.toast(res.message, 'success');
      const fresh = await loadAccount();
      if (fresh.ok) $('#curBal').textContent = B.money(account.balance);
      $('#result').innerHTML = '<div class="success-box"><div class="tick">' + icon('check', 'lg') + '</div><h2>Transfer successful</h2><p class="muted">Reference <b>' + esc(res.data.reference) + '</b></p>' +
        '<p>New balance: <b>' + B.money(res.data.account.balance) + '</b></p><a class="btn btn-light" href="#/history">View history</a></div>';
    });
  }

  // ------------------------------------------------------------------ history
  const hist = { type: 'all', from: '', to: '', offset: 0, limit: 10 };
  async function history() {
    view.innerHTML = '<div class="card"><div class="card-head"><h2>Transaction history</h2></div>' +
      '<form id="filters" class="row mb"><div class="field flush"><label for="ftype" class="small">Type</label><select class="input" id="ftype"><option value="all">All transactions</option><option value="deposit">Deposits</option><option value="withdrawal">Withdrawals</option><option value="transfer">Transfers</option></select></div>' +
      '<div class="field flush"><label for="ffrom" class="small">From</label><input class="input" type="date" id="ffrom"></div>' +
      '<div class="field flush"><label for="fto" class="small">To</label><input class="input" type="date" id="fto"></div>' +
      '<button class="btn" type="submit">Apply</button><button class="btn btn-light" type="button" id="freset">Reset</button></form><div class="form-alert hidden"></div><div id="list">' + B.skeleton() + '</div></div>';
    $('#ftype').value = hist.type; $('#ffrom').value = hist.from; $('#fto').value = hist.to;
    $('#filters').addEventListener('submit', (e) => { e.preventDefault(); hist.type = $('#ftype').value; hist.from = $('#ffrom').value; hist.to = $('#fto').value; hist.offset = 0; loadHistory(); });
    $('#freset').onclick = () => { Object.assign(hist, { type: 'all', from: '', to: '', offset: 0 }); $('#ftype').value = 'all'; $('#ffrom').value = ''; $('#fto').value = ''; loadHistory(); };
    loadHistory();
  }
  async function loadHistory() {
    const list = $('#list'); list.innerHTML = B.skeleton();
    const params = { type: hist.type === 'all' ? '' : hist.type, limit: hist.limit, offset: hist.offset };
    if (hist.from) params.from = Math.floor(new Date(hist.from + 'T00:00:00').getTime() / 1000);
    if (hist.to) params.to = Math.floor(new Date(hist.to + 'T23:59:59').getTime() / 1000);
    if (params.from && params.to && params.from > params.to) { list.innerHTML = ''; return B.formAlert($('#filters').parentElement, 'The start date must not be after the end date.', 'error'); }
    const r = await B.get('/api/me/transactions?' + B.qs(params));
    if (!r.ok) { list.innerHTML = B.errorState(r.error.message); $('#retry').onclick = loadHistory; return; }
    const d = r.data;
    if (!d.total) {
      const filtered = hist.type !== 'all' || hist.from || hist.to;
      list.innerHTML = B.emptyState('inbox', filtered ? 'No matching transactions' : 'No transactions yet', filtered ? 'Try a different type or date range.' : 'Your deposits, withdrawals and transfers will appear here.');
      return;
    }
    list.innerHTML = '<div class="table-wrap"><table class="tbl cards"><thead><tr><th>ID</th><th>Date &amp; time</th><th>Type</th><th>Flow</th><th class="right">Amount</th><th>Status</th><th>Reference</th></tr></thead><tbody>' +
      d.items.map((t) => { const c = t.direction === 'credit'; return '<tr><td data-label="ID">#' + t.id + '</td><td data-label="Date">' + B.dateTime(t.timestamp) + '</td>' +
        '<td data-label="Type"><b>' + esc(t.label) + '</b><br><span class="small muted">' + esc(t.description) + '</span></td>' +
        '<td data-label="Flow"><span class="badge ' + (c ? 'ok' : 'bad') + '">' + (c ? 'Credit' : 'Debit') + '</span></td>' +
        '<td data-label="Amount" class="right amt ' + t.direction + '">' + (c ? '+' : '\u2212') + B.money(t.amount) + '</td><td data-label="Status">' + B.txStatusBadge(t.status) + '</td><td data-label="Reference"><code>' + esc(t.reference) + '</code></td></tr>'; }).join('') +
      '</tbody></table></div><div class="pager"><span class="small muted">Showing ' + (d.offset + 1) + '\u2013' + (d.offset + d.items.length) + ' of ' + d.total + '</span><span class="row"><button class="btn btn-light btn-sm" id="prev"' + (d.offset ? '' : ' disabled') + '>Previous</button><button class="btn btn-light btn-sm" id="next"' + (d.offset + d.items.length < d.total ? '' : ' disabled') + '>Next</button></span></div>';
    $('#prev').onclick = () => { hist.offset = Math.max(0, hist.offset - hist.limit); loadHistory(); };
    $('#next').onclick = () => { hist.offset += hist.limit; loadHistory(); };
  }

  // ------------------------------------------------------------------ profile
  async function profileView() {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/me/profile');
    if (!r.ok) return errorView(r.error.message, profileView);
    profile = r.data;
    view.innerHTML = '<div class="grid g2"><div class="card"><div class="card-head"><h2>Profile</h2></div><form id="form" novalidate><div class="form-alert hidden"></div>' +
      '<div class="field"><label for="fullName">Full name</label><input class="input" id="fullName" name="fullName" maxlength="60"><div class="field-error" data-error-for="fullName"></div></div>' +
      '<div class="field"><label for="phone">Phone number</label><input class="input" id="phone" name="phone" inputmode="tel"><div class="field-error" data-error-for="phone"></div></div>' +
      '<div class="field"><label for="email">Email address</label><input class="input" id="email" name="email" disabled><div class="hint">Your email is your sign-in name and cannot be changed here.</div></div>' +
      '<div class="field"><label for="cid">Customer ID</label><input class="input" id="cid" disabled></div><button class="btn" type="submit">Save changes</button></form></div>' +
      '<div class="card"><div class="card-head"><h2>Security</h2></div><p class="muted">Keep your account safe with a strong, unique password.</p><a class="btn btn-ghost" href="#/password">' + icon('lock') + ' Change password</a>' +
      '<hr class="sep"><p class="muted small">Member since ' + B.date(profile.memberSince) + '</p></div></div>';
    const form = $('#form');
    form.elements.fullName.value = profile.fullName; form.elements.phone.value = profile.phone; form.elements.email.value = profile.email; $('#cid').value = profile.customerId;
    form.addEventListener('submit', async (e) => {
      e.preventDefault(); B.clearErrors(form);
      const name = form.elements.fullName.value.trim(), phone = form.elements.phone.value.trim();
      if (name.length < 2) return B.fieldError(form, 'fullName', 'Enter your full name');
      if (phone.replace(/\D/g, '').length < 10) return B.fieldError(form, 'phone', 'Enter a phone number with at least 10 digits');
      const btn = $('button[type=submit]', form); B.setLoading(btn, true, 'Saving\u2026');
      const res = await B.put('/api/me/profile', { fullName: name, phone });
      B.setLoading(btn, false);
      if (!res.ok) return B.showServerErrors(form, res.error);
      profile = res.data; setChip(); B.formAlert(form, 'Profile updated.', 'success'); B.toast('Profile updated', 'success');
    });
  }

  // ------------------------------------------------------------------ change password
  function passwordView() {
    view.innerHTML = '<div class="card narrow"><div class="card-head"><h2>Change password</h2></div><form id="form" novalidate><div class="form-alert hidden"></div>' +
      ['currentPassword|Current password|current-password', 'newPassword|New password|new-password', 'confirmPassword|Confirm new password|new-password'].map((f) => {
        const p = f.split('|');
        return '<div class="field"><label for="' + p[0] + '">' + p[1] + '</label><div class="input-wrap"><input class="input" id="' + p[0] + '" name="' + p[0] + '" type="password" autocomplete="' + p[2] + '"><button type="button" class="eye" data-toggle-password aria-label="Show password"></button></div>' +
          (p[0] === 'newPassword' ? '<div class="meter"><i id="meter"></i></div><div class="hint">At least 8 characters with a letter and a number.</div>' : '') + '<div class="field-error" data-error-for="' + p[0] + '"></div></div>';
      }).join('') + '<button class="btn" type="submit">Update password</button></form></div>';
    const form = $('#form'); B.bindPasswordToggles(form); B.bindMeter(form.elements.newPassword, $('#meter'));
    form.addEventListener('submit', async (e) => {
      e.preventDefault(); B.clearErrors(form);
      const f = form.elements;
      if (!f.currentPassword.value) return B.fieldError(form, 'currentPassword', 'Enter your current password');
      if (f.newPassword.value.length < 8 || !/[A-Za-z]/.test(f.newPassword.value) || !/\d/.test(f.newPassword.value)) return B.fieldError(form, 'newPassword', 'Use at least 8 characters with a letter and a number');
      if (f.newPassword.value !== f.confirmPassword.value) return B.fieldError(form, 'confirmPassword', 'Passwords do not match');
      const btn = $('button[type=submit]', form); B.setLoading(btn, true, 'Updating\u2026');
      const res = await B.post('/api/me/password', { currentPassword: f.currentPassword.value, newPassword: f.newPassword.value, confirmPassword: f.confirmPassword.value });
      B.setLoading(btn, false);
      if (!res.ok) return B.showServerErrors(form, res.error);
      form.reset(); $('#meter').className = ''; B.formAlert(form, res.message, 'success'); B.toast('Password changed', 'success');
    });
  }

  // ------------------------------------------------------------------ router
  const ROUTES = {
    dashboard, account: accountView, deposit: () => moneyForm('deposit'), withdraw: () => moneyForm('withdraw'),
    transfer, history, profile: profileView, password: passwordView
  };
  function setChip() {
    if (!profile) return;
    $('#chipName').textContent = profile.fullName; $('#chipAvatar').textContent = B.initials(profile.fullName);
  }
  async function route() {
    const name = (location.hash.replace(/^#\//, '') || 'dashboard').split('?')[0];
    const key = ROUTES[name] ? name : 'dashboard';
    if (!ROUTES[name]) { location.replace('#/dashboard'); return; }
    buildNav(key);
    $('#pageTitle').textContent = NAV.find((n) => n[0] === key)[1];
    document.title = NAV.find((n) => n[0] === key)[1] + ' | BankPro';
    if (B.closeNav) B.closeNav();
    await ROUTES[key]();
    view.focus({ preventScroll: true });
    window.scrollTo(0, 0);
  }

  (async function init() {
    B.bindShell();
    B.onUnauthorized = () => { location.href = '/login?expired=1'; };
    const s = await B.session();
    if (!s || s.role !== 'customer') { location.href = '/login'; return; }
    $('#chipName').textContent = s.name; $('#chipAvatar').textContent = B.initials(s.name);
    window.addEventListener('hashchange', route);
    route();
  })();
})();
