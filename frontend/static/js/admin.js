/* Administrator portal. Every action is authorised again on the server (admin session + CSRF
   token); this file only provides the interface. Sensitive actions ask for confirmation. */
(function () {
  'use strict';
  const B = window.Bank, $ = B.$, $$ = B.$$, esc = B.esc, icon = B.icon;
  const view = $('#view');

  const NAV = [
    ['overview', 'Dashboard', 'home'], ['customers', 'Customer Management', 'users'], ['accounts', 'Account Management', 'card'],
    ['transactions', 'Transaction Monitoring', 'activity'], ['search', 'Search', 'search'], ['queue', 'Service Queue', 'queue'], ['security', 'My Password', 'lock']
  ];

  function buildNav(active) {
    $('#nav').innerHTML = NAV.map((n) => '<a href="#/' + n[0] + '"' + (n[0] === active ? ' class="active" aria-current="page"' : '') + '>' + icon(n[2]) + '<span>' + n[1] + '</span></a>').join('') +
      '<div class="spacer"></div><hr><button id="logoutBtn">' + icon('logout') + '<span>Logout</span></button>';
    $('#logoutBtn').onclick = async () => {
      if (await B.confirm({ title: 'Log out?', message: 'You will be signed out of the admin portal.', confirmText: 'Log out' })) B.logout('/admin-login');
    };
  }
  function errorView(msg, retry) { view.innerHTML = B.errorState(msg); $('#retry').onclick = retry; }
  const debounce = (fn, ms) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };
  const dayStart = (s) => Math.floor(new Date(s + 'T00:00:00').getTime() / 1000);
  const dayEnd = (s) => Math.floor(new Date(s + 'T23:59:59').getTime() / 1000);

  // Activate / deactivate with a confirmation dialog. Calls `after()` once the server confirms.
  async function toggleStatus(accountNumber, holder, current, after) {
    const next = current === 'active' ? 'deactivated' : 'active';
    const ok = await B.confirm({
      title: next === 'deactivated' ? 'Deactivate this account?' : 'Activate this account?',
      html: '<p>Account <b>' + esc(accountNumber) + '</b> (' + esc(holder) + ') ' + (next === 'deactivated'
        ? 'will no longer be able to deposit, withdraw or transfer, and cannot receive transfers.' : 'will be able to make transactions again.') + '</p>',
      confirmText: next === 'deactivated' ? 'Deactivate' : 'Activate', danger: next === 'deactivated'
    });
    if (!ok) return;
    const r = await B.post('/api/admin/accounts/' + accountNumber + '/status', { status: next });
    if (!r.ok) return B.toast(r.error.message, 'error');
    B.toast(r.message, 'success');
    after();
  }
  const statusBtn = (a) => '<button class="btn btn-sm ' + (a.status === 'active' ? 'btn-light' : 'btn-success') + '" data-toggle="' + esc(a.accountNumber) + '" data-holder="' + esc(a.holderName || a.fullName) + '" data-status="' + esc(a.status) + '">' + (a.status === 'active' ? 'Deactivate' : 'Activate') + '</button>';
  const bindToggles = (root, after) => $$('[data-toggle]', root).forEach((b) => { b.onclick = () => toggleStatus(b.dataset.toggle, b.dataset.holder, b.dataset.status, after); });

  function txTable(items, withAccount) {
    return '<div class="table-wrap"><table class="tbl cards"><thead><tr><th>ID</th><th>Date &amp; time</th>' + (withAccount ? '<th>Account</th>' : '') + '<th>Type</th><th>Flow</th><th class="right">Amount</th><th>Status</th><th>Reference</th></tr></thead><tbody>' +
      items.map((t) => { const c = t.direction === 'credit'; return '<tr><td data-label="ID">#' + t.id + '</td><td data-label="Date">' + B.dateTime(t.timestamp) + '</td>' +
        (withAccount ? '<td data-label="Account">' + (t.customerId ? '<a href="#/customer/' + esc(t.customerId) + '">' + esc(t.holderName) + '</a>' : esc(t.holderName || '')) + '<br><span class="small muted">' + esc(t.accountNumber) + '</span></td>' : '') +
        '<td data-label="Type">' + esc(t.label) + '</td><td data-label="Flow"><span class="badge ' + (c ? 'ok' : 'bad') + '">' + (c ? 'Credit' : 'Debit') + '</span></td>' +
        '<td data-label="Amount" class="right amt ' + t.direction + '">' + (c ? '+' : '\u2212') + B.money(t.amount) + '</td><td data-label="Status">' + B.txStatusBadge(t.status) + '</td><td data-label="Reference"><code>' + esc(t.reference) + '</code></td></tr>'; }).join('') + '</tbody></table></div>';
  }
  function stat(ico, color, label, value) {
    return '<div class="card stat"><div class="ico ' + color + '">' + icon(ico) + '</div><div><div class="lbl">' + label + '</div><div class="val">' + value + '</div></div></div>';
  }

  // ------------------------------------------------------------------ overview
  async function overview() {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/admin/overview');
    if (!r.ok) return errorView(r.error.message, overview);
    const d = r.data;
    const warn = d.storage.known && !d.storage.persistent
      ? '<div class="alert warn">' + icon('alert') + '<div><b>Temporary storage.</b> The data folder is not on a persistent disk, so all customers, balances and transactions will be lost when the service restarts or redeploys. See DEPLOY.md to attach a disk.</div></div>' : '';
    view.innerHTML = warn + '<div class="page-title"><h2>Bank overview</h2><p class="muted">Live figures from the bank\u2019s records.</p></div>' +
      '<div class="grid g3">' + stat('users', '', 'Total customers', d.totalCustomers) + stat('card', '', 'Total accounts', d.totalAccounts + ' <span class="small muted">(' + d.deactivatedAccounts + ' deactivated)</span>') + stat('activity', 'amber', 'Total transactions', d.totalTransactions) + '</div>' +
      '<div class="grid g3 mt">' + stat('arrowdown', 'green', 'Recorded deposits', B.money(d.totalDeposits)) + stat('arrowup', 'red', 'Recorded withdrawals', B.money(d.totalWithdrawals)) + stat('wallet', '', 'Money held in accounts', B.money(d.totalBalance)) + '</div>' +
      '<div class="card mt"><div class="card-head"><h2>Latest transactions</h2><a class="btn btn-sm btn-ghost" href="#/transactions">Open monitoring</a></div>' +
      (d.recentTransactions.length ? txTable(d.recentTransactions, true) : B.emptyState('inbox', 'No transactions yet', 'Transactions made by customers will appear here.')) + '</div>';
  }

  // ------------------------------------------------------------------ customers
  const cf = { search: '', status: 'all', sort: 'created', order: 'asc' };
  function customers() {
    view.innerHTML = '<div class="card"><div class="card-head"><h2>Customers</h2><span class="muted small" id="count"></span></div>' +
      '<form class="row mb" id="f"><input class="input grow" id="q" type="search" placeholder="Search name, email, phone, customer ID or account no." aria-label="Search customers">' +
      '<select class="input" id="st" aria-label="Status"><option value="all">All statuses</option><option value="active">Active</option><option value="deactivated">Deactivated</option></select>' +
      '<select class="input" id="so" aria-label="Sort"><option value="created">Newest last</option><option value="name">Name</option><option value="balance">Balance</option></select>' +
      '<select class="input" id="or" aria-label="Order"><option value="asc">Ascending</option><option value="desc">Descending</option></select></form><div id="list">' + B.skeleton() + '</div></div>';
    $('#q').value = cf.search; $('#st').value = cf.status; $('#so').value = cf.sort; $('#or').value = cf.order;
    const reload = () => { cf.search = $('#q').value.trim(); cf.status = $('#st').value; cf.sort = $('#so').value; cf.order = $('#or').value; loadCustomers(); };
    $('#q').addEventListener('input', debounce(reload, 300));
    ['st', 'so', 'or'].forEach((i) => $('#' + i).addEventListener('change', reload));
    $('#f').addEventListener('submit', (e) => { e.preventDefault(); reload(); });
    loadCustomers();
  }
  async function loadCustomers() {
    const r = await B.get('/api/admin/customers?' + B.qs(cf));
    const list = $('#list'); if (!list) return;
    if (!r.ok) { list.innerHTML = B.errorState(r.error.message); $('#retry').onclick = loadCustomers; return; }
    $('#count').textContent = r.data.total + ' customer' + (r.data.total === 1 ? '' : 's');
    if (!r.data.items.length) { list.innerHTML = B.emptyState('users', 'No customers found', cf.search || cf.status !== 'all' ? 'Try a different search or filter.' : 'Customers appear here after they register.'); return; }
    list.innerHTML = '<div class="table-wrap"><table class="tbl cards"><thead><tr><th>Customer</th><th>Contact</th><th>Account</th><th class="right">Balance</th><th>Status</th><th>Actions</th></tr></thead><tbody>' +
      r.data.items.map((c) => '<tr><td data-label="Customer"><b>' + esc(c.fullName) + '</b><br><span class="small muted">' + esc(c.customerId) + '</span></td><td data-label="Contact">' + esc(c.email) + '<br><span class="small muted">' + esc(c.phone) + '</span></td>' +
        '<td data-label="Account">' + esc(c.accountNumber) + '</td><td data-label="Balance" class="right amt">' + B.money(c.balance) + '</td><td data-label="Status">' + B.statusBadge(c.status) + '</td>' +
        '<td data-label="Actions"><a class="btn btn-sm btn-ghost" href="#/customer/' + esc(c.customerId) + '">View</a> ' + statusBtn({ accountNumber: c.accountNumber, holderName: c.fullName, status: c.status }) + '</td></tr>').join('') + '</tbody></table></div>';
    bindToggles(list, loadCustomers);
  }

  // ------------------------------------------------------------------ customer / account detail
  async function customerDetail(id) {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/admin/customers/' + encodeURIComponent(id));
    if (!r.ok) return errorView(r.status === 404 ? 'That customer does not exist.' : r.error.message, () => customerDetail(id));
    renderDetail(r.data, true, () => customerDetail(id));
  }
  async function accountDetail(n) {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/admin/accounts/' + encodeURIComponent(n));
    if (!r.ok) return errorView(r.status === 404 ? 'No account with that number exists.' : r.error.message, () => accountDetail(n));
    renderDetail(r.data, false, () => accountDetail(n));
  }
  function renderDetail(d, isCustomer, reload) {
    const a = d.account, c = d.customer;
    view.innerHTML = '<p><a href="#/' + (isCustomer ? 'customers' : 'accounts') + '">&larr; Back</a></p><div class="grid g2">' +
      '<div class="card"><div class="card-head"><h2>' + esc(isCustomer ? c.fullName : a.holderName) + '</h2>' + B.statusBadge(a.status) + '</div><dl class="dl">' +
      (isCustomer ? '<dt>Customer ID</dt><dd>' + esc(c.customerId) + '</dd><dt>Email</dt><dd>' + esc(c.email) + '</dd><dt>Phone</dt><dd>' + esc(c.phone) + '</dd><dt>Member since</dt><dd>' + B.date(c.memberSince) + '</dd>'
        : '<dt>Customer</dt><dd>' + (a.customerId ? '<a href="#/customer/' + esc(a.customerId) + '">' + esc(a.customerId) + '</a>' : 'Not linked to a customer profile') + '</dd>') +
      '<dt>Account number</dt><dd>' + esc(a.accountNumber) + '</dd><dt>Account type</dt><dd>' + esc(a.accountType) + '</dd><dt>Balance</dt><dd>' + B.money(a.balance) + '</dd><dt>Opened</dt><dd>' + B.date(a.openedAt) + '</dd></dl>' +
      '<div class="alert info">' + icon('lock', 'sm') + '<span>Passwords are stored as one-way hashes and cannot be viewed by anyone.</span></div>' + statusBtn(a) + '</div>' +
      '<div class="card"><div class="card-head"><h2>Activity summary</h2></div><dl class="dl"><dt>Total deposits</dt><dd>' + B.money(d.summary.totalDeposits) + '</dd><dt>Total withdrawals</dt><dd>' + B.money(d.summary.totalWithdrawals) + '</dd>' +
      '<dt>Transfers sent</dt><dd>' + B.money(d.summary.totalTransfersOut) + '</dd><dt>Transfers received</dt><dd>' + B.money(d.summary.totalTransfersIn) + '</dd><dt>Transactions</dt><dd>' + d.transactionCount + '</dd></dl></div></div>' +
      '<div class="card mt"><div class="card-head"><h2>Recent transactions</h2></div>' + (d.recentTransactions.length ? txTable(d.recentTransactions, false) : B.emptyState('inbox', 'No transactions', 'This account has no activity yet.')) + '</div>';
    bindToggles(view, reload);
  }

  // ------------------------------------------------------------------ accounts
  const af = { search: '', status: 'all', sort: 'number', order: 'asc' };
  function accounts() {
    view.innerHTML = '<div class="card"><div class="card-head"><h2>Accounts</h2><span class="muted small" id="count"></span></div>' +
      '<form class="row mb" id="f"><input class="input grow" id="q" type="search" placeholder="Search account number or holder name" aria-label="Search accounts">' +
      '<select class="input" id="st" aria-label="Status"><option value="all">All statuses</option><option value="active">Active</option><option value="deactivated">Deactivated</option></select>' +
      '<select class="input" id="so" aria-label="Sort"><option value="number">Account no.</option><option value="name">Holder name</option><option value="balance">Balance</option></select>' +
      '<select class="input" id="or" aria-label="Order"><option value="asc">Ascending</option><option value="desc">Descending</option></select></form><div id="list">' + B.skeleton() + '</div></div>';
    $('#q').value = af.search; $('#st').value = af.status; $('#so').value = af.sort; $('#or').value = af.order;
    const reload = () => { af.search = $('#q').value.trim(); af.status = $('#st').value; af.sort = $('#so').value; af.order = $('#or').value; loadAccounts(); };
    $('#q').addEventListener('input', debounce(reload, 300));
    ['st', 'so', 'or'].forEach((i) => $('#' + i).addEventListener('change', reload));
    $('#f').addEventListener('submit', (e) => { e.preventDefault(); reload(); });
    loadAccounts();
  }
  async function loadAccounts() {
    const r = await B.get('/api/admin/accounts?' + B.qs(af));
    const list = $('#list'); if (!list) return;
    if (!r.ok) { list.innerHTML = B.errorState(r.error.message); $('#retry').onclick = loadAccounts; return; }
    $('#count').textContent = r.data.total + ' account' + (r.data.total === 1 ? '' : 's');
    if (!r.data.items.length) { list.innerHTML = B.emptyState('card', 'No accounts found', 'Try a different search or filter.'); return; }
    list.innerHTML = '<div class="table-wrap"><table class="tbl cards"><thead><tr><th>Account</th><th>Holder</th><th>Type</th><th class="right">Balance</th><th>Status</th><th>Actions</th></tr></thead><tbody>' +
      r.data.items.map((a) => '<tr><td data-label="Account"><b>' + esc(a.accountNumber) + '</b></td><td data-label="Holder">' + esc(a.holderName) + '<br><span class="small muted">' + esc(a.customerId || 'no customer profile') + '</span></td><td data-label="Type">' + esc(a.accountType) + '</td>' +
        '<td data-label="Balance" class="right amt">' + B.money(a.balance) + '</td><td data-label="Status">' + B.statusBadge(a.status) + '</td><td data-label="Actions"><a class="btn btn-sm btn-ghost" href="#/account/' + esc(a.accountNumber) + '">Details</a> ' + statusBtn(a) + '</td></tr>').join('') + '</tbody></table></div>';
    bindToggles(list, loadAccounts);
  }

  // ------------------------------------------------------------------ transactions
  const tf = { type: 'all', search: '', account: '', from: '', to: '', offset: 0, limit: 15 };
  function transactions() {
    view.innerHTML = '<div class="card mb" id="rev"></div><div class="card"><div class="card-head"><h2>All transactions</h2><span class="muted small" id="count"></span></div>' +
      '<form class="row mb" id="f"><input class="input grow" id="q" type="search" placeholder="Reference, holder name or account no." aria-label="Search transactions">' +
      '<select class="input" id="ty" aria-label="Type"><option value="all">All types</option><option value="deposit">Deposits</option><option value="withdrawal">Withdrawals</option><option value="transfer">Transfers</option><option value="reversal">Reversals</option></select>' +
      '<input class="input" type="date" id="fr" aria-label="From date"><input class="input" type="date" id="to" aria-label="To date"><button class="btn" type="submit">Apply</button><button type="button" class="btn btn-light" id="rs">Reset</button></form><div id="list">' + B.skeleton() + '</div></div>';
    $('#q').value = tf.search; $('#ty').value = tf.type; $('#fr').value = tf.from; $('#to').value = tf.to;
    $('#f').addEventListener('submit', (e) => { e.preventDefault(); tf.search = $('#q').value.trim(); tf.type = $('#ty').value; tf.from = $('#fr').value; tf.to = $('#to').value; tf.offset = 0; loadTx(); });
    $('#rs').onclick = () => { Object.assign(tf, { type: 'all', search: '', account: '', from: '', to: '', offset: 0 }); transactions(); };
    loadReversal(); loadTx();
  }
  async function loadReversal() {
    const r = await B.get('/api/admin/reversal'); const box = $('#rev'); if (!box) return;
    if (!r.ok) { box.innerHTML = '<p class="muted">' + esc(r.error.message) + '</p>'; return; }
    const t = r.data.transaction;
    box.innerHTML = '<div class="card-head"><h2>Reverse latest transaction</h2></div>' + (t
      ? '<p class="muted">The most recent reversible deposit or withdrawal (last-in, first-out stack): <b>' + esc(t.label) + ' of ' + B.money(t.amount) + '</b> on account <b>' + esc(t.accountNumber) + '</b> (' + esc(t.holderName) + ') &middot; ' + esc(t.reference) + '.</p><button class="btn btn-danger btn-sm" id="revbtn">' + icon('undo', 'sm') + ' Reverse it</button>'
      : '<p class="muted">Nothing to reverse right now.</p>');
    const btn = $('#revbtn');
    if (btn) btn.onclick = async () => {
      if (!await B.confirm({ title: 'Reverse this transaction?', html: '<p>This will undo the ' + esc(t.label.toLowerCase()) + ' of <b>' + esc(B.money(t.amount)) + '</b> on account ' + esc(t.accountNumber) + ' and record a reversal. It fails if the account no longer has the funds.</p>', confirmText: 'Reverse', danger: true })) return;
      const res = await B.post('/api/admin/reversal', { transactionId: t.id });
      if (!res.ok) return B.toast(res.error.message, 'error');
      B.toast(res.message, 'success'); loadReversal(); loadTx();
    };
  }
  async function loadTx() {
    const list = $('#list'); if (!list) return; list.innerHTML = B.skeleton();
    const p = { type: tf.type === 'all' ? '' : tf.type, search: tf.search, account: tf.account, limit: tf.limit, offset: tf.offset };
    if (tf.from) p.from = dayStart(tf.from);
    if (tf.to) p.to = dayEnd(tf.to);
    const r = await B.get('/api/admin/transactions?' + B.qs(p));
    if (!r.ok) { list.innerHTML = B.errorState(r.error.message); $('#retry').onclick = loadTx; return; }
    const d = r.data; $('#count').textContent = d.total + ' transaction' + (d.total === 1 ? '' : 's');
    if (!d.total) { list.innerHTML = B.emptyState('inbox', 'No transactions found', 'Adjust the filters, or wait for customers to transact.'); return; }
    list.innerHTML = txTable(d.items, true) + '<div class="pager"><span class="small muted">Showing ' + (d.offset + 1) + '\u2013' + (d.offset + d.items.length) + ' of ' + d.total + '</span><span class="row"><button class="btn btn-light btn-sm" id="prev"' + (d.offset ? '' : ' disabled') + '>Previous</button><button class="btn btn-light btn-sm" id="next"' + (d.offset + d.items.length < d.total ? '' : ' disabled') + '>Next</button></span></div>';
    $('#prev').onclick = () => { tf.offset = Math.max(0, tf.offset - tf.limit); loadTx(); };
    $('#next').onclick = () => { tf.offset += tf.limit; loadTx(); };
  }

  // ------------------------------------------------------------------ search
  function search() {
    view.innerHTML = '<div class="grid g2"><div class="card"><div class="card-head"><h2>Search customers</h2></div><form id="cs" novalidate><div class="field"><label for="cq">Name, email, phone or customer ID</label><input class="input" id="cq" maxlength="60"></div><button class="btn" type="submit">' + icon('search') + ' Search</button></form></div>' +
      '<div class="card"><div class="card-head"><h2>Search by account number</h2></div><form id="as" novalidate><div class="form-alert hidden"></div><div class="field"><label for="aq">Account number</label><input class="input" name="aq" id="aq" inputmode="numeric" maxlength="15"><div class="field-error" data-error-for="aq"></div></div><button class="btn" type="submit">' + icon('search') + ' Find account</button></form></div></div><div class="card mt hidden" id="res"></div>';
    $('#cs').addEventListener('submit', async (e) => {
      e.preventDefault(); const q = $('#cq').value.trim(); const box = $('#res'); box.classList.remove('hidden');
      if (!q) { box.innerHTML = B.emptyState('search', 'Type something to search', 'Search by name, email, phone, customer ID or account number.'); return; }
      box.innerHTML = B.skeleton();
      const r = await B.get('/api/admin/customers?' + B.qs({ search: q }));
      if (!r.ok) { box.innerHTML = B.errorState(r.error.message); $('#retry').onclick = () => $('#cs').requestSubmit(); return; }
      box.innerHTML = '<div class="card-head"><h2>' + r.data.total + ' result' + (r.data.total === 1 ? '' : 's') + '</h2></div>' + (r.data.items.length
        ? '<ul class="tx-list">' + r.data.items.map((c) => '<li><span class="avatar">' + esc(B.initials(c.fullName)) + '</span><div class="meta"><b>' + esc(c.fullName) + '</b><span class="small muted">' + esc(c.email) + ' \u00B7 acct ' + esc(c.accountNumber) + '</span></div>' + B.statusBadge(c.status) + '<a class="btn btn-sm btn-ghost" href="#/customer/' + esc(c.customerId) + '">View</a></li>').join('') + '</ul>'
        : B.emptyState('search', 'No customers match', 'Check the spelling or try fewer words.'));
    });
    $('#as').addEventListener('submit', (e) => {
      e.preventDefault(); const f = $('#as'); B.clearErrors(f);
      const n = $('#aq').value.trim();
      if (!/^\d{1,15}$/.test(n)) return B.fieldError(f, 'aq', 'Enter an account number (digits only)');
      location.hash = '#/account/' + n;
    });
  }

  // ------------------------------------------------------------------ service queue
  async function queue() {
    view.innerHTML = B.skeleton();
    const r = await B.get('/api/admin/queue');
    if (!r.ok) return errorView(r.error.message, queue);
    const q = r.data;
    view.innerHTML = '<div class="grid g2"><div class="card"><div class="card-head"><h2>Issue a token</h2></div><p class="muted small">Walk-in customers join a first-come, first-served queue (a FIFO queue data structure).</p><form id="f" novalidate><div class="form-alert hidden"></div>' +
      '<div class="field"><label for="customerName">Customer name</label><input class="input" id="customerName" name="customerName" maxlength="60"><div class="field-error" data-error-for="customerName"></div></div>' +
      '<div class="field"><label for="purpose">Purpose (optional)</label><input class="input" id="purpose" name="purpose" maxlength="60"></div><button class="btn" type="submit">Issue token</button></form></div>' +
      '<div class="card"><div class="card-head"><h2>Waiting (' + q.count + ')</h2><button class="btn btn-sm" id="serve"' + (q.count ? '' : ' disabled') + '>Serve next</button></div>' + (q.waiting.length
        ? '<ul class="tx-list">' + q.waiting.map((t, i) => '<li><span class="avatar">' + esc(t.token) + '</span><div class="meta"><b>' + esc(t.customerName) + (i === 0 ? ' <span class="badge info">Next</span>' : '') + '</b><span class="small muted">' + esc(t.purpose || 'No purpose given') + ' \u00B7 ' + B.dateTime(t.issuedAt) + '</span></div></li>').join('') + '</ul>'
        : B.emptyState('queue', 'Queue is empty', 'Issue a token when a customer arrives.')) + '</div></div>';
    $('#f').addEventListener('submit', async (e) => {
      e.preventDefault(); const f = $('#f'); B.clearErrors(f);
      if (!f.elements.customerName.value.trim()) return B.fieldError(f, 'customerName', 'Enter the customer\u2019s name');
      const res = await B.post('/api/admin/queue', { customerName: f.elements.customerName.value, purpose: f.elements.purpose.value });
      if (!res.ok) return B.showServerErrors(f, res.error);
      B.toast(res.message, 'success'); queue();
    });
    const s = $('#serve'); if (s) s.onclick = async () => {
      const res = await B.post('/api/admin/queue/serve', {});
      if (!res.ok) return B.toast(res.error.message, 'error');
      B.toast(res.message, 'success'); queue();
    };
  }

  // ------------------------------------------------------------------ admin password
  function security() {
    view.innerHTML = '<div class="card narrow"><div class="card-head"><h2>Change your password</h2></div><form id="form" novalidate><div class="form-alert hidden"></div>' +
      ['currentPassword|Current password|current-password', 'newPassword|New password (min. 10 characters)|new-password', 'confirmPassword|Confirm new password|new-password'].map((f) => { const p = f.split('|');
        return '<div class="field"><label for="' + p[0] + '">' + p[1] + '</label><div class="input-wrap"><input class="input" id="' + p[0] + '" name="' + p[0] + '" type="password" autocomplete="' + p[2] + '"><button type="button" class="eye" data-toggle-password aria-label="Show password"></button></div><div class="field-error" data-error-for="' + p[0] + '"></div></div>'; }).join('') +
      '<button class="btn" type="submit">Update password</button></form></div>';
    const form = $('#form'); B.bindPasswordToggles(form);
    form.addEventListener('submit', async (e) => {
      e.preventDefault(); B.clearErrors(form); const f = form.elements;
      if (!f.currentPassword.value) return B.fieldError(form, 'currentPassword', 'Enter your current password');
      if (f.newPassword.value.length < 10) return B.fieldError(form, 'newPassword', 'Use at least 10 characters');
      if (f.newPassword.value !== f.confirmPassword.value) return B.fieldError(form, 'confirmPassword', 'Passwords do not match');
      const btn = $('button[type=submit]', form); B.setLoading(btn, true, 'Updating\u2026');
      const res = await B.post('/api/admin/password', { currentPassword: f.currentPassword.value, newPassword: f.newPassword.value, confirmPassword: f.confirmPassword.value });
      B.setLoading(btn, false);
      if (!res.ok) return B.showServerErrors(form, res.error);
      form.reset(); B.formAlert(form, res.message, 'success'); B.toast('Password changed', 'success');
    });
  }

  // ------------------------------------------------------------------ router
  const TITLES = { customer: 'Customer details', account: 'Account details' };
  async function route() {
    const parts = (location.hash.replace(/^#\//, '') || 'overview').split('/');
    const name = parts[0];
    let navKey = name, title;
    if (name === 'customer' && parts[1]) { navKey = 'customers'; title = TITLES.customer; }
    else if (name === 'account' && parts[1]) { navKey = 'accounts'; title = TITLES.account; }
    else if (!NAV.some((n) => n[0] === name)) { location.replace('#/overview'); return; }
    title = title || NAV.find((n) => n[0] === name)[1];
    buildNav(navKey); $('#pageTitle').textContent = title; document.title = title + ' | BankPro Admin';
    if (B.closeNav) B.closeNav();
    const handlers = { overview, customers, accounts, transactions, search, queue, security };
    if (name === 'customer') await customerDetail(parts[1]);
    else if (name === 'account') await accountDetail(parts[1]);
    else await handlers[name]();
    view.focus({ preventScroll: true }); window.scrollTo(0, 0);
  }

  (async function init() {
    B.bindShell();
    B.onUnauthorized = () => { location.href = '/admin-login'; };
    const s = await B.session();
    if (!s || s.role !== 'admin') { location.href = '/admin-login'; return; }
    $('#chipName').textContent = s.name; $('#chipAvatar').textContent = B.initials(s.name);
    window.addEventListener('hashchange', route);
    route();
  })();
})();
