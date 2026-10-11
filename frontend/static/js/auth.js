/* Login, registration and admin-login pages. Client-side checks only give friendly, instant
   messages; the server repeats every check and is the one that decides. */
(function () {
  'use strict';
  const B = window.Bank, $ = B.$;
  const page = document.body.dataset.page;
  if (!$('#form')) return;
  B.bindPasswordToggles(document);
  const form = $('#form');
  const submit = $('button[type=submit]', form);

  const val = (n) => form.elements[n].value;
  const emailOk = (s) => /^[^\s@]+@[^\s@]+\.[A-Za-z]{2,}$/.test(s);

  if (page === 'login') {
    const q = new URLSearchParams(location.search);
    const note = $('#notice');
    const show = (m) => { note.textContent = m; note.classList.remove('hidden'); };
    if (q.get('registered')) show('Account created. Please sign in with your email and password.');
    if (q.get('expired')) show('Your session has ended. Please sign in again.');
  }

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    B.clearErrors(form);
    let bad = false;
    const need = (name, msg) => { B.fieldError(form, name, msg); bad = true; };

    if (page === 'login') {
      if (!emailOk(val('email').trim())) need('email', 'Enter a valid email address');
      if (!val('password')) need('password', 'Enter your password');
    } else if (page === 'admin-login') {
      if (!val('username').trim()) need('username', 'Enter your username');
      if (!val('password')) need('password', 'Enter your password');
    } else if (page === 'register') {
      if (val('fullName').trim().length < 2) need('fullName', 'Enter your full name');
      else if (!/^[\p{L}][\p{L} .'-]*$/u.test(val('fullName').trim())) need('fullName', 'Use letters, spaces, dots, apostrophes and hyphens only');
      if (!emailOk(val('email').trim())) need('email', 'Enter a valid email address');
      if (val('phone').replace(/[\s-]/g, '').replace(/^\+/, '').replace(/\D/g, '').length < 10) need('phone', 'Enter a phone number with at least 10 digits');
      const pw = val('password');
      if (pw.length < 8) need('password', 'Password must be at least 8 characters');
      else if (!/[A-Za-z]/.test(pw) || !/\d/.test(pw)) need('password', 'Password must contain a letter and a number');
      if (val('confirmPassword') !== pw) need('confirmPassword', 'Passwords do not match');
    }
    if (bad) { const f = $('[aria-invalid="true"]', form); if (f) f.focus(); return; }

    B.setLoading(submit, true, page === 'register' ? 'Creating account\u2026' : 'Signing in\u2026');
    let r;
    if (page === 'register') {
      r = await B.post('/api/auth/register', {
        fullName: val('fullName'), email: val('email'), phone: val('phone'),
        password: val('password'), confirmPassword: val('confirmPassword')
      });
    } else if (page === 'login') {
      r = await B.post('/api/auth/login', { email: val('email'), password: val('password') });
    } else {
      r = await B.post('/api/admin/login', { username: val('username'), password: val('password') });
    }
    B.setLoading(submit, false);

    if (!r.ok) {
      form.elements.password && (form.elements.password.value = page === 'register' ? form.elements.password.value : '');
      B.showServerErrors(form, r.error);
      return;
    }
    if (page === 'register') {
      $('#acctNo').textContent = r.data.accountNumber;
      $('#custId').textContent = r.data.customerId;
      $('#formView').classList.add('hidden');
      $('#doneView').classList.remove('hidden');
    } else {
      location.href = r.data.redirect; // "/dashboard" or "/admin", chosen by the server
    }
  });

  if (page === 'register') B.bindMeter(form.elements.password, $('#meter'));
})();
