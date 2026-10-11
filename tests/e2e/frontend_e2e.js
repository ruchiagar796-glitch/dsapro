// End-to-end test of the FRONTEND logic against the REAL compiled server.
//   cd tests/e2e && npm install && node frontend_e2e.js [path/to/bank_server]
// jsdom runs the real HTML + JavaScript (no browser needed). It checks behaviour, rendering and
// console errors. It does NOT check visual layout / responsiveness (use a real browser for that).
const { JSDOM, VirtualConsole, CookieJar } = require('jsdom');
const { spawn } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const BIN = process.argv[2] || path.join(ROOT, 'build', 'bank_server');
const PORT = 18080 + Math.floor(Math.random() * 500);
const BASE = 'http://127.0.0.1:' + PORT;
const DATA = fs.mkdtempSync(path.join(os.tmpdir(), 'bankpro-e2e-'));
const ADMIN_PW = 'Adm1nPassw0rd!';
const PW = 'Str0ngPass1';

let passed = 0, failed = 0;
const consoleErrors = [];
function ok(cond, name) { if (cond) { passed++; console.log('  \u2713 ' + name); } else { failed++; console.log('  \u2717 FAILED: ' + name); } }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function waitFor(fn, what, ms = 4000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { try { const v = fn(); if (v) return v; } catch (e) { /* retry */ } await sleep(20); }
  throw new Error('timed out waiting for: ' + what);
}

function makeFetch(jar, state) {
  return async (url, opts = {}) => {
    const abs = new URL(url, BASE).toString();
    const headers = Object.assign({}, opts.headers || {});
    const cookie = jar.getCookieStringSync(abs);
    if (cookie) headers.Cookie = cookie;
    state.requests.push((opts.method || 'GET') + ' ' + new URL(abs).pathname);
    const res = await fetch(abs, { method: opts.method || 'GET', headers, body: opts.body, redirect: 'manual' });
    for (const c of res.headers.getSetCookie()) jar.setCookieSync(c, abs);
    const clone = res.clone();
    try { state.last = await clone.json(); } catch (e) { state.last = null; }
    return res;
  };
}

async function open(p, jar) {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { if (!/Not implemented: navigation/.test(e.message)) consoleErrors.push(p + ' jsdomError: ' + e.message); });
  vc.on('error', (m) => consoleErrors.push(p + ' console.error: ' + m));
  const state = { requests: [], last: null };
  const dom = await JSDOM.fromURL(BASE + p, {
    runScripts: 'dangerously', resources: 'usable', cookieJar: jar, pretendToBeVisual: true, virtualConsole: vc,
    beforeParse(w) { w.fetch = makeFetch(jar, state); w.scrollTo = () => {}; }
  });
  await new Promise((r) => (dom.window.document.readyState === 'complete' ? r() : dom.window.addEventListener('load', r)));
  const doc = dom.window.document;
  const api = {
    dom, doc, state, $: (s) => doc.querySelector(s), $$: (s) => Array.from(doc.querySelectorAll(s)),
    text: (s) => (doc.querySelector(s) ? doc.querySelector(s).textContent : ''),
    type(sel, v) { const el = doc.querySelector(sel); el.value = v; el.dispatchEvent(new dom.window.Event('input', { bubbles: true })); el.dispatchEvent(new dom.window.Event('change', { bubbles: true })); },
    submit(sel) { doc.querySelector(sel).dispatchEvent(new dom.window.Event('submit', { bubbles: true, cancelable: true })); },
    click(sel) { doc.querySelector(sel).dispatchEvent(new dom.window.MouseEvent('click', { bubbles: true, cancelable: true })); },
    go(hash) { dom.window.location.hash = hash; },
    async confirmYes() { await waitFor(() => doc.querySelector('.modal'), 'confirm dialog'); doc.querySelector('[data-yes]').click(); },
    close() { dom.window.close(); }
  };
  return api;
}

async function plain(p, jar, init = {}) {
  const headers = Object.assign({}, init.headers || {});
  const c = jar.getCookieStringSync(BASE + p); if (c) headers.Cookie = c;
  return fetch(BASE + p, Object.assign({ redirect: 'manual' }, init, { headers }));
}

async function registerAndLogin(name, email) {
  const jar = new CookieJar();
  let pg = await open('/register', jar);
  pg.type('#fullName', name); pg.type('#email', email); pg.type('#phone', '+91 98765 43210');
  pg.type('#password', PW); pg.type('#confirmPassword', PW); pg.submit('#form');
  await waitFor(() => !pg.$('#doneView').classList.contains('hidden'), 'registration success');
  const acct = pg.text('#acctNo'); pg.close();
  pg = await open('/login', jar);
  pg.type('#email', email); pg.type('#password', PW); pg.submit('#form');
  await waitFor(() => pg.state.last && pg.state.last.data && pg.state.last.data.redirect, 'login response');
  pg.close();
  return { jar, acct };
}

(async function main() {
  const server = spawn(BIN, ['--port', String(PORT), '--data', DATA, '--frontend', path.join(ROOT, 'frontend')],
    { env: Object.assign({}, process.env, { BMS_ADMIN_USERNAME: 'root', BMS_ADMIN_PASSWORD: ADMIN_PW }), stdio: ['ignore', 'pipe', 'pipe'] });
  let serverLog = ''; server.stdout.on('data', (d) => (serverLog += d)); server.stderr.on('data', (d) => (serverLog += d));
  try {
    await waitFor(() => fetch(BASE + '/healthz').then((r) => r.ok).catch(() => false) && true, 'server', 100).catch(() => {});
    for (let i = 0; i < 100; i++) { try { if ((await fetch(BASE + '/healthz')).ok) break; } catch (e) { await sleep(50); } }

    console.log('Landing, auth pages');
    let jar = new CookieJar();
    let pg = await open('/', jar);
    ok(pg.$$('a[href="/login"]').length > 0 && /Customer Banking/.test(pg.doc.body.textContent), 'landing page shows Customer Banking');
    ok(pg.$$('a[href="/admin-login"]').length > 0 && /Admin Login/.test(pg.doc.body.textContent), 'landing page shows Admin Login');
    ok(pg.$('svg') !== null, 'logo / icons are hydrated'); pg.close();

    pg = await open('/register', jar);
    pg.type('#fullName', 'A'); pg.type('#email', 'nope'); pg.type('#phone', '12'); pg.type('#password', 'abc'); pg.type('#confirmPassword', 'x'); pg.submit('#form');
    await waitFor(() => pg.text('[data-error-for=email]'), 'client validation');
    ok(/valid email/i.test(pg.text('[data-error-for=email]')), 'register: invalid email shown');
    ok(pg.text('[data-error-for=password]') !== '', 'register: weak password shown');
    ok(pg.state.requests.length === 0, 'register: nothing sent to the server while invalid');
    pg.type('#fullName', 'Ruchi Agarwal'); pg.type('#email', 'ruchi@example.com'); pg.type('#phone', '9876543210'); pg.type('#password', PW); pg.type('#confirmPassword', PW);
    pg.submit('#form');
    await waitFor(() => !pg.$('#doneView').classList.contains('hidden'), 'registration success');
    ok(/^\d{10}$/.test(pg.text('#acctNo')), 'register: account number shown (' + pg.text('#acctNo') + ')');
    ok(/^CUS\d{6}$/.test(pg.text('#custId')), 'register: customer id shown');
    pg.close();
    pg = await open('/register', jar);
    pg.type('#fullName', 'Ruchi Again'); pg.type('#email', 'RUCHI@example.com'); pg.type('#phone', '9876543210'); pg.type('#password', PW); pg.type('#confirmPassword', PW); pg.submit('#form');
    await waitFor(() => pg.text('[data-error-for=email]'), 'duplicate error');
    ok(/already exists/i.test(pg.text('[data-error-for=email]')), 'register: duplicate email rejected'); pg.close();

    console.log('Login');
    pg = await open('/login', jar);
    pg.type('#email', 'ruchi@example.com'); pg.type('#password', 'WrongPass9'); pg.submit('#form');
    await waitFor(() => pg.text('.form-alert'), 'login error');
    ok(/Invalid email or password/.test(pg.text('.form-alert')), 'login: wrong password -> generic error');
    pg.type('#password', PW); pg.submit('#form');
    await waitFor(() => pg.state.last && pg.state.last.data && pg.state.last.data.redirect, 'login ok');
    ok(pg.state.last.data.redirect === '/dashboard', 'login: server says redirect /dashboard'); pg.close();
    const other = await registerAndLogin('Bhanu Pratap', 'bhanu@example.com');

    console.log('Customer dashboard');
    const dash = await open('/dashboard', jar);
    await waitFor(() => /Welcome back, Ruchi/.test(dash.text('#view')), 'dashboard render');
    ok(/Welcome back, Ruchi/.test(dash.text('#view')), 'dashboard: personalised welcome');
    ok(/CUS000001/.test(dash.text('#view')), 'dashboard: customer id');
    ok(/\u20B90\.00/.test(dash.text('#bal')), 'dashboard: zero starting balance');
    ok(/XXXXXX\d{4}/.test(dash.text('#acctNo')), 'dashboard: account number masked');
    dash.click('#revealBtn'); ok(/^\d{10}$/.test(dash.text('#acctNo')), 'dashboard: reveal shows full number');
    ok(/No transactions yet/.test(dash.text('#recent')), 'dashboard: empty state');
    dash.click('#eyeBtn'); ok(/\u2022\u2022/.test(dash.text('#bal')), 'dashboard: hide balance'); dash.click('#eyeBtn');
    ok(dash.$$('#nav a').length === 8 && /Logout/.test(dash.text('#nav')), 'sidebar: 8 sections + logout');

    console.log('Deposit / withdraw');
    dash.go('#/deposit'); await waitFor(() => dash.$('#amount'), 'deposit form');
    dash.type('#amount', '0'); dash.submit('#form');
    await waitFor(() => dash.text('[data-error-for=amount]'), 'amount error');
    ok(/greater than zero/.test(dash.text('[data-error-for=amount]')), 'deposit: zero rejected in the form');
    const before = dash.state.requests.filter((r) => r.includes('deposit')).length;
    dash.type('#amount', '1000.50'); dash.submit('#form');
    await waitFor(() => /Reference/.test(dash.text('#result')), 'deposit result');
    ok(/New balance: \u20B91,000\.50/.test(dash.text('#result')), 'deposit: success panel with new balance');
    ok(dash.state.requests.filter((r) => r.includes('deposit')).length === before + 1, 'deposit: exactly one request sent');
    ok(/\u20B91,000\.50/.test(dash.text('#curBal')), 'deposit: balance re-read from server');
    dash.go('#/withdraw'); await waitFor(() => /Withdraw money/.test(dash.text('#view')), 'withdraw form');
    dash.type('#amount', '5000'); dash.submit('#form');
    await waitFor(() => dash.text('.form-alert'), 'insufficient');
    ok(/Insufficient balance/.test(dash.text('.form-alert')), 'withdraw: more than balance -> server error shown');
    dash.type('#amount', '100.25'); dash.submit('#form');
    await waitFor(() => /Reference/.test(dash.text('#result')), 'withdraw result');
    ok(/\u20B9900\.25/.test(dash.text('#result')), 'withdraw: new balance 900.25');

    console.log('Transfer');
    dash.go('#/transfer'); await waitFor(() => dash.$('#toAccount'), 'transfer form');
    dash.type('#toAccount', '123'); dash.click('#verify');
    await waitFor(() => dash.text('[data-error-for=toAccount]'), 'recipient error');
    ok(/not found/i.test(dash.text('[data-error-for=toAccount]')), 'transfer: nonexistent recipient rejected');
    dash.type('#toAccount', other.acct); dash.click('#verify');
    await waitFor(() => /Recipient:/.test(dash.text('#recipient')), 'recipient ok');
    ok(/B\*+ P\*+/.test(dash.text('#recipient')) && !/Bhanu/.test(dash.text('#recipient')), 'transfer: recipient name masked (' + dash.text('#recipient').trim() + ')');
    dash.type('#amount', '50'); dash.submit('#form');
    await dash.confirmYes();
    await waitFor(() => /Transfer successful/.test(dash.text('#result')), 'transfer ok');
    ok(/\u20B9850\.25/.test(dash.text('#result')), 'transfer: new balance 850.25');

    console.log('History');
    dash.go('#/history'); await waitFor(() => dash.$$('#list tbody tr').length > 0, 'history rows');
    ok(dash.$$('#list tbody tr').length === 3, 'history: 3 transactions');
    ok(/Credit/.test(dash.text('#list')) && /Debit/.test(dash.text('#list')), 'history: credit and debit indicators');
    ok(/TXN\d{8}/.test(dash.text('#list')), 'history: reference numbers');
    dash.type('#ftype', 'withdrawal'); dash.submit('#filters');
    await waitFor(() => dash.$$('#list tbody tr').length === 1, 'filter withdrawals');
    ok(/Withdrawal/.test(dash.text('#list tbody')), 'history: withdrawal filter works');
    dash.type('#ftype', 'all'); dash.type('#ffrom', '2999-01-01'); dash.submit('#filters');
    await waitFor(() => /No matching transactions/.test(dash.text('#list')), 'empty filter state');
    ok(true, 'history: empty state for a date range with no transactions');
    dash.click('#freset'); await waitFor(() => dash.$$('#list tbody tr').length === 3, 'reset filters');
    ok(true, 'history: reset restores all rows');

    console.log('Profile, password, account');
    dash.go('#/account'); await waitFor(() => /Account details/.test(dash.text('#view')), 'account view');
    ok(/Savings/.test(dash.text('#view')) && /\u20B9850\.25/.test(dash.text('#view')), 'my account: details + balance');
    dash.go('#/profile'); await waitFor(() => dash.$('#fullName'), 'profile');
    ok(dash.$('#email').disabled, 'profile: email is read-only');
    dash.type('#fullName', 'Ruchi A. Agarwal'); dash.submit('#form');
    await waitFor(() => /Profile updated/.test(dash.text('.form-alert')), 'profile saved');
    ok(/Ruchi A\. Agarwal/.test(dash.text('#chipName')), 'profile: name updated in the header');
    dash.go('#/password'); await waitFor(() => dash.$('#newPassword'), 'password form');
    dash.type('#currentPassword', PW); dash.type('#newPassword', 'N3wStrongPass'); dash.type('#confirmPassword', 'different1'); dash.submit('#form');
    await waitFor(() => dash.text('[data-error-for=confirmPassword]'), 'mismatch');
    ok(/do not match/.test(dash.text('[data-error-for=confirmPassword]')), 'password: mismatch caught');
    dash.type('#confirmPassword', 'N3wStrongPass'); dash.submit('#form');
    await waitFor(() => /Password changed/.test(dash.text('.form-alert')), 'password changed');
    ok(true, 'password: change succeeds');

    console.log('Authorization seen from the outside');
    ok((await plain('/admin', jar)).status === 302, 'customer cannot open the admin page (redirected)');
    ok((await plain('/api/admin/customers', jar)).status === 403, 'customer cookie -> admin API = 403');
    ok((await plain('/dashboard', new CookieJar())).status === 302, 'signed-out visitor cannot open the dashboard');
    ok((await plain('/api/me/overview', new CookieJar())).status === 401, 'signed-out API call = 401');

    console.log('Admin portal');
    const ajar = new CookieJar();
    pg = await open('/admin-login', ajar);
    pg.type('#username', 'root'); pg.type('#password', 'wrong-password-1'); pg.submit('#form');
    await waitFor(() => pg.text('.form-alert'), 'admin login error');
    ok(/Invalid username or password/.test(pg.text('.form-alert')), 'admin login: wrong password rejected');
    pg.type('#password', ADMIN_PW); pg.submit('#form');
    await waitFor(() => pg.state.last && pg.state.last.data && pg.state.last.data.redirect === '/admin', 'admin login ok');
    ok(true, 'admin login: success -> /admin'); pg.close();
    ok((await plain('/api/me/overview', ajar)).status === 403, 'admin cookie -> customer API = 403');
    const adm = await open('/admin', ajar);
    await waitFor(() => /Bank overview/.test(adm.text('#view')), 'admin overview');
    ok(/Total customers/.test(adm.text('#view')) && /Recorded deposits/.test(adm.text('#view')) && /Recorded withdrawals/.test(adm.text('#view')) && /Total transactions/.test(adm.text('#view')) && /Total accounts/.test(adm.text('#view')), 'admin overview: all 5 summary cards');
    ok(/Temporary storage|Latest transactions/.test(adm.text('#view')), 'admin overview: renders');
    ok(adm.$$('#nav a').length === 7, 'admin sidebar: 7 sections + logout');
    adm.go('#/customers'); await waitFor(() => adm.$$('#list tbody tr').length === 2, 'customers table');
    ok(true, 'customers: both customers listed');
    adm.type('#q', 'bhanu'); await waitFor(() => adm.$$('#list tbody tr').length === 1, 'search filter');
    ok(/Bhanu Pratap/.test(adm.text('#list')), 'customers: search narrows the list');
    ok(!/argon2|passwordHash/.test(adm.doc.body.innerHTML), 'admin pages never contain a password hash');
    adm.type('#q', ''); await waitFor(() => adm.$$('#list tbody tr').length === 2, 'clear search');
    const viewLink = adm.$$('#list a[href^="#/customer/"]')[0].getAttribute('href');
    adm.go(viewLink); await waitFor(() => /Customer details/.test(adm.text('#pageTitle')) && /Passwords are stored/.test(adm.text('#view')), 'customer detail');
    ok(/Recent transactions/.test(adm.text('#view')), 'customer detail: shows account + transactions');
    adm.go('#/accounts'); await waitFor(() => adm.$$('#list tbody tr').length === 2, 'accounts table');
    adm.click('#list [data-toggle]'); await adm.confirmYes();
    await waitFor(() => /Deactivated/.test(adm.text('#list')), 'deactivated');
    ok(true, 'accounts: deactivate (with confirmation dialog) works');
    dash.go('#/deposit'); await waitFor(() => dash.$('#amount') && /deactivated/i.test(dash.text('#view')), 'deactivated notice');
    ok(/deactivated/i.test(dash.text('#view')) && dash.$('button[type=submit]').disabled, 'customer: sees deactivation notice, deposit disabled');
    adm.click('#list [data-toggle]'); await adm.confirmYes(); await waitFor(() => !/Deactivated/.test(adm.text('#list')), 'reactivated');
    ok(true, 'accounts: re-activate works');
    adm.go('#/transactions'); await waitFor(() => adm.$$('#list tbody tr').length === 4, 'tx monitor');
    ok(/Reverse latest transaction/.test(adm.text('#rev')) && adm.$('#revbtn') !== null, 'transactions: list + reversal card');
    adm.type('#ty', 'transfer'); adm.submit('#f'); await waitFor(() => adm.$$('#list tbody tr').length === 2, 'transfer filter');
    ok(true, 'transactions: type filter (transfer shows both legs)');
    adm.go('#/search'); await waitFor(() => adm.$('#cq'), 'search page');
    adm.type('#cq', 'ruchi'); adm.submit('#cs'); await waitFor(() => /1 result/.test(adm.text('#res')), 'search results');
    ok(true, 'search: customer search');
    adm.type('#aq', 'abc'); adm.submit('#as'); await waitFor(() => adm.text('[data-error-for=aq]'), 'acct err');
    ok(true, 'search: invalid account number validated');
    adm.type('#aq', other.acct); adm.submit('#as'); await waitFor(() => /Account details/.test(adm.text('#pageTitle')), 'account detail');
    await waitFor(() => /Bhanu Pratap/.test(adm.text('#view')), 'account detail render');
    ok(true, 'search: by account number opens the account');
    adm.go('#/queue'); await waitFor(() => adm.$('#customerName'), 'queue');
    adm.type('#customerName', 'Walk In'); adm.type('#purpose', 'Loan'); adm.submit('#f'); await waitFor(() => /Walk In/.test(adm.text('#view')), 'token issued');
    adm.type('#customerName', '<img src=x onerror=window.__xss=1>'); adm.type('#purpose', '<script>window.__xss=2</script>'); adm.submit('#f');
    await waitFor(() => /<img src=x/.test(adm.text('#view')), 'hostile token shown as text');
    ok(adm.$('#view img') === null && adm.$('#view script') === null && !adm.dom.window.__xss, 'XSS: hostile text is displayed as text, no element is created, nothing runs');
    adm.click('#serve'); await waitFor(() => /<img src=x/.test(adm.text('#view')) === true && /Walk In/.test(adm.text('#view')) === false, 'served first');
    adm.click('#serve'); await waitFor(() => /Queue is empty/.test(adm.text('#view')), 'served');
    ok(true, 'queue: issue token + serve next');
    adm.go('#/nowhere'); await sleep(100);
    ok(adm.dom.window.location.hash === '#/overview', 'admin: unknown route falls back to overview');
    adm.go('#/security'); await waitFor(() => adm.$('#newPassword'), 'admin password page'); ok(true, 'admin: password page');

    console.log('Logout');
    dash.click('#logoutBtn'); await dash.confirmYes();
    await waitFor(() => !jar.getCookieStringSync(BASE + '/').includes('bms_session='), 'cookie cleared');
    ok((await plain('/api/me/overview', jar)).status === 401, 'logout: session no longer valid');
    ok((await plain('/dashboard', jar)).status === 302, 'logout: dashboard redirects to login');
    ok((await fetch(BASE + '/definitely/missing')).status === 404, '404 page for unknown URLs');

    ok(consoleErrors.length === 0, 'no console errors / script errors in any page' + (consoleErrors.length ? ': ' + consoleErrors.join(' | ') : ''));
  } catch (e) {
    failed++; console.log('  \u2717 ABORTED: ' + e.message);
    console.log('--- server log ---\n' + serverLog.slice(-1500));
  } finally {
    server.kill('SIGTERM'); await sleep(200); fs.rmSync(DATA, { recursive: true, force: true });
  }
  console.log('\n' + passed + ' passed, ' + failed + ' failed');
  process.exit(failed ? 1 : 0);
})();
