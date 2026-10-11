/* Shared helpers for every page: DOM shortcuts, safe escaping, API client, toasts, dialogs,
   formatting. Nothing here stores secrets: the session lives in an HttpOnly cookie that
   JavaScript cannot read; only the CSRF token is kept (in memory). */
(function () {
  'use strict';
  const B = (window.Bank = window.Bank || {});

  B.$ = (sel, root) => (root || document).querySelector(sel);
  B.$$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  // Escape EVERY piece of server data before putting it into HTML (stops injected markup).
  B.esc = (v) => String(v == null ? '' : v).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  // ---------- icons (static, trusted SVG) ----------
  const P = {
    home: '<path d="M3 11l9-8 9 8"/><path d="M5 10v10h14V10"/>',
    user: '<circle cx="12" cy="8" r="4"/><path d="M4 21c0-4 4-6 8-6s8 2 8 6"/>',
    wallet: '<path d="M3 7h16a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M3 7l12-4v4"/><circle cx="16.5" cy="13.5" r="1"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    minus: '<path d="M5 12h14"/>',
    send: '<path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4z"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13"/><circle cx="3.5" cy="6" r="1"/><circle cx="3.5" cy="12" r="1"/><circle cx="3.5" cy="18" r="1"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="M19 12a7 7 0 0 0-.1-1.2l2-1.5-2-3.4-2.3.9a7 7 0 0 0-2-1.2L14.2 3h-4.4l-.4 2.6a7 7 0 0 0-2 1.2l-2.3-.9-2 3.4 2 1.5A7 7 0 0 0 5 12c0 .4 0 .8.1 1.2l-2 1.5 2 3.4 2.3-.9a7 7 0 0 0 2 1.2l.4 2.6h4.4l.4-2.6a7 7 0 0 0 2-1.2l2.3.9 2-3.4-2-1.5c.1-.4.1-.8.1-1.2z"/>',
    lock: '<rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
    logout: '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><path d="M16 17l5-5-5-5M21 12H9"/>',
    eye: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    eyeoff: '<path d="M3 3l18 18"/><path d="M10.6 6.1A10 10 0 0 1 12 5c6 0 10 7 10 7a17 17 0 0 1-3.2 4M6.5 6.9C3.7 8.7 2 12 2 12s4 7 10 7c1.6 0 3-.4 4.3-1"/><path d="M9.9 9.9a3 3 0 0 0 4.2 4.2"/>',
    arrowdown: '<path d="M12 5v14M5 12l7 7 7-7"/>',
    arrowup: '<path d="M12 19V5M5 12l7-7 7 7"/>',
    check: '<path d="M20 6L9 17l-5-5"/>',
    alert: '<circle cx="12" cy="12" r="9"/><path d="M12 8v5M12 16.5v.5"/>',
    users: '<circle cx="9" cy="8" r="3.5"/><path d="M2 20c0-3.5 3.5-5.5 7-5.5s7 2 7 5.5"/><path d="M16 4.5a3.5 3.5 0 0 1 0 7M18 14.8c2.2.6 4 2.1 4 5.2"/>',
    card: '<rect x="2" y="5" width="20" height="14" rx="2"/><path d="M2 10h20"/>',
    activity: '<path d="M22 12h-4l-3 9L9 3l-3 9H2"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/>',
    shield: '<path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/><path d="M9 12l2 2 4-4"/>',
    copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h9"/>',
    undo: '<path d="M3 7v6h6"/><path d="M3 13a9 9 0 1 0 3-7"/>',
    queue: '<circle cx="6" cy="7" r="2.5"/><circle cx="12" cy="7" r="2.5"/><circle cx="18" cy="7" r="2.5"/><path d="M2 20v-3a3 3 0 0 1 3-3h2M9 20v-3a3 3 0 0 1 3-3h0a3 3 0 0 1 3 3v3M17 14h2a3 3 0 0 1 3 3v3"/>',
    menu: '<path d="M4 6h16M4 12h16M4 18h16"/>',
    inbox: '<path d="M22 12h-6l-2 3h-4l-2-3H2"/><path d="M5.5 5h13L22 12v6a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2v-6z"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    bolt: '<path d="M13 2L4 14h7l-1 8 9-12h-7z"/>',
    globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>',
    file: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>'
  };
  B.icon = (name, cls) => '<svg class="icon ' + (cls || '') + '" viewBox="0 0 24 24" aria-hidden="true">' + (P[name] || '') + '</svg>';
  B.logo = '<svg viewBox="0 0 64 64" aria-hidden="true"><defs><linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5a7dff"/><stop offset="1" stop-color="#2247c4"/></linearGradient></defs><rect width="64" height="64" rx="16" fill="url(#lg)"/><path d="M14 26 32 14l18 12v4H14zM18 34h6v14h-6zm11 0h6v14h-6zm11 0h6v14h-6zM14 50h36v4H14z" fill="#fff"/></svg>';

  // ---------- formatting ----------
  const inr = new Intl.NumberFormat('en-IN', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  // Amounts arrive from the server as exact strings such as "1500.50".
  B.money = (s) => '\u20B9' + inr.format(Number(s));
  B.date = (epoch) => new Intl.DateTimeFormat('en-IN', { dateStyle: 'medium' }).format(new Date(epoch * 1000));
  B.dateTime = (epoch) => new Intl.DateTimeFormat('en-IN', { dateStyle: 'medium', timeStyle: 'short' }).format(new Date(epoch * 1000));
  B.initials = (name) => String(name || '?').trim().split(/\s+/).slice(0, 2).map((w) => w[0]).join('').toUpperCase();
  B.newKey = () => (window.crypto && crypto.randomUUID ? crypto.randomUUID() : 'k' + Date.now().toString(36) + Math.random().toString(36).slice(2, 12));
  B.statusBadge = (s) => s === 'active' ? '<span class="badge ok">Active</span>' : '<span class="badge bad">Deactivated</span>';
  B.txStatusBadge = (s) => s === 'Completed' ? '<span class="badge ok">Completed</span>' : '<span class="badge warn">' + B.esc(s) + '</span>';

  // ---------- API client ----------
  B.csrf = '';
  B.onUnauthorized = null; // pages set this to redirect when the session has ended
  B.api = async function (method, path, body) {
    const opts = { method, headers: { Accept: 'application/json' }, credentials: 'same-origin' };
    if (method !== 'GET') opts.headers['X-CSRF-Token'] = B.csrf;
    if (body !== undefined) { opts.headers['Content-Type'] = 'application/json'; opts.body = JSON.stringify(body); }
    let res;
    try { res = await fetch(path, opts); }
    catch (e) { return { ok: false, status: 0, error: { code: 'NETWORK', message: 'Cannot reach the server. Check your internet connection and try again.' } }; }
    let json = null;
    try { json = await res.json(); } catch (e) { /* not JSON */ }
    if (res.ok && json && json.success) return { ok: true, status: res.status, data: json.data, message: json.message, raw: json };
    const error = (json && json.error) || { code: 'ERROR', message: 'Something went wrong (' + res.status + '). Please try again.' };
    if (res.status === 401 && B.onUnauthorized) B.onUnauthorized();
    return { ok: false, status: res.status, error };
  };
  B.get = (p) => B.api('GET', p);
  B.post = (p, b) => B.api('POST', p, b === undefined ? {} : b);
  B.put = (p, b) => B.api('PUT', p, b);
  B.qs = (o) => Object.keys(o).filter((k) => o[k] !== '' && o[k] != null).map((k) => encodeURIComponent(k) + '=' + encodeURIComponent(o[k])).join('&');

  B.session = async function () {
    const r = await B.get('/api/auth/session');
    if (r.ok && r.data.authenticated) { B.csrf = r.data.csrfToken; return r.data; }
    return null;
  };
  B.logout = async function (to) {
    await B.post('/api/auth/logout', {});
    location.href = to;
  };

  // ---------- toasts ----------
  B.toast = function (message, type) {
    let box = B.$('.toasts');
    if (!box) { box = document.createElement('div'); box.className = 'toasts'; box.setAttribute('role', 'status'); box.setAttribute('aria-live', 'polite'); document.body.appendChild(box); }
    const t = document.createElement('div');
    t.className = 'toast ' + (type || 'info');
    t.innerHTML = B.icon(type === 'error' ? 'alert' : 'check', 'sm') + '<span></span>';
    t.lastChild.textContent = message;
    box.appendChild(t);
    setTimeout(() => t.remove(), type === 'error' ? 6500 : 4200);
  };

  // ---------- confirmation dialog (replaces window.confirm) ----------
  B.confirm = function (o) {
    return new Promise((resolve) => {
      const back = document.createElement('div');
      back.className = 'modal-back';
      back.innerHTML = '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="mt"><h3 id="mt"></h3><div class="mbody"></div><div class="actions"><button class="btn btn-light" data-no>Cancel</button><button class="btn ' + (o.danger ? 'btn-danger' : '') + '" data-yes></button></div></div>';
      B.$('h3', back).textContent = o.title;
      B.$('.mbody', back).innerHTML = o.html || '<p></p>';
      if (!o.html) B.$('.mbody p', back).textContent = o.message || '';
      B.$('[data-yes]', back).textContent = o.confirmText || 'Confirm';
      const done = (v) => { document.removeEventListener('keydown', key); back.remove(); resolve(v); };
      const key = (e) => { if (e.key === 'Escape') done(false); };
      document.addEventListener('keydown', key);
      B.$('[data-no]', back).onclick = () => done(false);
      B.$('[data-yes]', back).onclick = () => done(true);
      back.addEventListener('click', (e) => { if (e.target === back) done(false); });
      document.body.appendChild(back);
      B.$('[data-yes]', back).focus();
    });
  };

  // ---------- form helpers ----------
  B.setLoading = function (btn, on, label) {
    if (!btn) return;
    if (on) { btn.dataset.label = btn.innerHTML; btn.disabled = true; btn.classList.add('loading'); btn.innerHTML = '<span class="spinner"></span><span>' + B.esc(label || 'Please wait\u2026') + '</span>'; }
    else { btn.disabled = false; btn.classList.remove('loading'); if (btn.dataset.label) btn.innerHTML = btn.dataset.label; }
  };
  B.clearErrors = function (form) {
    B.$$('.field-error', form).forEach((e) => { e.textContent = ''; });
    B.$$('[aria-invalid]', form).forEach((e) => e.removeAttribute('aria-invalid'));
    const a = B.$('.form-alert', form); if (a) { a.className = 'form-alert hidden'; a.textContent = ''; }
  };
  B.fieldError = function (form, name, msg) {
    const input = form.elements[name];
    const slot = B.$('[data-error-for="' + name + '"]', form);
    if (slot) slot.textContent = msg;
    if (input) input.setAttribute('aria-invalid', 'true');
  };
  B.formAlert = function (form, msg, type) {
    const a = B.$('.form-alert', form);
    if (!a) return B.toast(msg, type === 'success' ? 'success' : 'error');
    a.className = 'form-alert alert ' + (type || 'error');
    a.setAttribute('role', 'alert');
    a.textContent = msg;
  };
  // Shows server-side validation (per-field) errors on a form.
  B.showServerErrors = function (form, error) {
    if (error.fields) {
      Object.keys(error.fields).forEach((k) => B.fieldError(form, k, error.fields[k]));
      const first = B.$('[aria-invalid="true"]', form); if (first) first.focus();
      B.formAlert(form, error.message, 'error');
    } else B.formAlert(form, error.message, 'error');
  };
  B.bindPasswordToggles = function (root) {
    B.$$('[data-toggle-password]', root).forEach((btn) => {
      btn.innerHTML = B.icon('eye');
      btn.addEventListener('click', () => {
        const input = btn.parentElement.querySelector('input');
        const show = input.type === 'password';
        input.type = show ? 'text' : 'password';
        btn.innerHTML = B.icon(show ? 'eyeoff' : 'eye');
        btn.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
      });
    });
  };
  // Very small strength estimate for the password meter (the server enforces the real rules).
  B.strength = function (pw) {
    let s = 0;
    if (pw.length >= 8) s++;
    if (pw.length >= 12) s++;
    if (/[a-z]/.test(pw) && /[A-Z]/.test(pw)) s++;
    if (/\d/.test(pw) && /[^A-Za-z0-9]/.test(pw)) s++;
    return Math.min(4, s);
  };
  B.bindMeter = function (input, meter) {
    input.addEventListener('input', () => {
      const s = input.value ? Math.max(1, B.strength(input.value)) : 0;
      meter.className = s ? 'w' + s : '';
    });
  };
  B.copy = async function (text) {
    try { await navigator.clipboard.writeText(text); B.toast('Copied to clipboard', 'success'); }
    catch (e) { B.toast('Copy is not available in this browser', 'error'); }
  };
  B.emptyState = (icon, title, text) => '<div class="empty">' + B.icon(icon) + '<h3>' + B.esc(title) + '</h3><p>' + B.esc(text) + '</p></div>';
  B.errorState = (msg, retryId) => '<div class="empty">' + B.icon('alert') + '<h3>Could not load this page</h3><p>' + B.esc(msg) + '</p><button class="btn btn-light" id="' + (retryId || 'retry') + '">Try again</button></div>';
  B.skeleton = () => '<div class="skel big"></div><div class="skel"></div><div class="skel"></div>';

  // ---------- responsive sidebar ----------
  B.bindShell = function () {
    const sb = B.$('.sidebar'), scrim = B.$('.scrim'), btn = B.$('.menu-btn');
    if (!sb || !btn) return;
    const set = (open) => { sb.classList.toggle('open', open); scrim.classList.toggle('open', open); btn.setAttribute('aria-expanded', String(open)); };
    btn.innerHTML = B.icon('menu');
    btn.addEventListener('click', () => set(!sb.classList.contains('open')));
    scrim.addEventListener('click', () => set(false));
    B.closeNav = () => set(false);
  };

  // Fills <span data-logo> and <span data-i="icon"> placeholders with the trusted SVGs above.
  B.hydrate = function (root) {
    B.$$('[data-logo]', root).forEach((e) => { e.innerHTML = B.logo; e.style.display = 'inline-flex'; });
    B.$$('[data-i]', root).forEach((e) => { e.insertAdjacentHTML('afterbegin', B.icon(e.dataset.i)); });
  };
  document.addEventListener('DOMContentLoaded', () => B.hydrate(document));
})();
