#!/usr/bin/env python3
"""Real-process checks: persistence across restart and kill -9, first-admin CLI, instance lock,
cookie flags behind a proxy, and what is stored on disk.  Usage: python3 restart_persistence.py [bank_server]"""
import http.client, json, os, shutil, signal, subprocess, sys, tempfile, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'bank_server')
PORT = 19000 + os.getpid() % 500
DATA = tempfile.mkdtemp(prefix='bankpro-persist-')
PW, ADMIN_PW = 'Str0ngPass1', 'Adm1nPassw0rd!'
passed = failed = 0

def check(cond, name):
    global passed, failed
    if cond: passed += 1; print('  \u2713', name)
    else: failed += 1; print('  \u2717 FAILED:', name)

def start(env_extra=None):
    env = dict(os.environ, BMS_ADMIN_USERNAME='root', BMS_ADMIN_PASSWORD=ADMIN_PW, **(env_extra or {}))
    p = subprocess.Popen([BIN, '--port', str(PORT), '--data', DATA, '--frontend', os.path.join(ROOT, 'frontend')],
                         env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for _ in range(100):
        try:
            c = http.client.HTTPConnection('127.0.0.1', PORT, timeout=1); c.request('GET', '/healthz')
            if c.getresponse().status == 200: return p
        except OSError: time.sleep(0.05)
    raise RuntimeError('server did not start')

class Client:
    def __init__(self): self.cookie = ''; self.csrf = ''
    def call(self, method, path, body=None, headers=None, csrf=True):
        c = http.client.HTTPConnection('127.0.0.1', PORT, timeout=5)
        h = dict(headers or {})
        if self.cookie: h['Cookie'] = 'bms_session=' + self.cookie
        if body is not None: h['Content-Type'] = 'application/json'
        if csrf and method != 'GET' and self.csrf: h['X-CSRF-Token'] = self.csrf
        c.request(method, path, json.dumps(body) if body is not None else None, h)
        r = c.getresponse(); raw = r.read().decode()
        try: data = json.loads(raw)
        except ValueError: data = None
        return r.status, data, r
    def login(self, path, body, headers=None):
        s, d, r = self.call('POST', path, body, headers, csrf=False)
        if s == 200:
            self.cookie = r.getheader('Set-Cookie').split('=', 1)[1].split(';', 1)[0]; self.csrf = d['data']['csrfToken']
        return s, d, r

def stop(p, sig=signal.SIGTERM):
    p.send_signal(sig); p.wait(timeout=10)

try:
    print('First run')
    p = start()
    u = Client()
    s, d, _ = u.call('POST', '/api/auth/register', {'fullName': 'Persist Tester', 'email': 'persist@example.com', 'phone': '9876543210', 'password': PW, 'confirmPassword': PW}, csrf=False)
    check(s == 201, 'register'); acct = d['data']['accountNumber']
    check(u.login('/api/auth/login', {'email': 'persist@example.com', 'password': PW})[0] == 200, 'login')
    check(u.call('POST', '/api/me/deposit', {'amount': '2500.75'})[0] == 201, 'deposit 2500.75')
    check(u.call('POST', '/api/me/withdraw', {'amount': '500.25'})[0] == 201, 'withdraw 500.25')
    # a second customer + transfer
    v = Client(); v.call('POST', '/api/auth/register', {'fullName': 'Second Person', 'email': 'second@example.com', 'phone': '9876543211', 'password': PW, 'confirmPassword': PW}, csrf=False)
    v.login('/api/auth/login', {'email': 'second@example.com', 'password': PW})
    check(u.call('POST', '/api/me/transfer', {'toAccount': v.call('GET', '/api/me/account')[1]['data']['accountNumber'], 'amount': '300'})[0] == 201, 'transfer 300')
    a = Client(); check(a.login('/api/admin/login', {'username': 'root', 'password': ADMIN_PW})[0] == 200, 'admin from BMS_ADMIN_PASSWORD can log in')
    a.call('POST', f'/api/admin/accounts/{v.call("GET","/api/me/account")[1]["data"]["accountNumber"]}/status', {'status': 'deactivated'})

    print('On-disk data')
    files = os.listdir(DATA)
    check({'accounts.txt', 'customers.txt', 'transactions.txt', 'admins.txt', 'metadata.txt'} <= set(files), 'all data files exist: ' + ', '.join(sorted(f for f in files if not f.startswith('.'))))
    check(not any(f.endswith('.tmp') or f == 'COMMIT' for f in files), 'no leftover .tmp / COMMIT files')
    blob = ''.join(open(os.path.join(DATA, f)).read() for f in files if f.endswith('.txt'))
    check(PW not in blob and ADMIN_PW not in blob, 'no plaintext password anywhere in the data files')
    check(blob.count('$argon2id$') == 3, 'passwords stored as Argon2id hashes (2 customers + 1 admin)')

    print('Restart (graceful)')
    stop(p); p = start(); u2 = Client()
    check(u.call('GET', '/api/me/overview')[0] == 401, 'old session cookie invalid after restart (sessions are memory-only)')
    check(u2.login('/api/auth/login', {'email': 'persist@example.com', 'password': PW})[0] == 200, 'login works with the persisted hash')
    s, d, _ = u2.call('GET', '/api/me/account')
    check(d['data']['balance'] == '1700.50' and d['data']['accountNumber'] == acct, 'balance + account number unchanged (1700.50)')
    check(u2.call('GET', '/api/me/transactions')[1]['data']['total'] == 3, 'history intact (3 transactions)')
    v2 = Client(); v2.login('/api/auth/login', {'email': 'second@example.com', 'password': PW})
    check(v2.call('GET', '/api/me/account')[1]['data']['status'] == 'deactivated', 'deactivated status persisted')
    check(v2.call('POST', '/api/me/deposit', {'amount': '1'})[0] == 403, 'deactivated account still blocked after restart')

    print('Hard kill (kill -9) in the middle of activity')
    stop(p, signal.SIGKILL); p = start(); u3 = Client(); u3.login('/api/auth/login', {'email': 'persist@example.com', 'password': PW})
    for _ in range(5): u3.call('POST', '/api/me/deposit', {'amount': '10'})
    stop(p, signal.SIGKILL); p = start(); u4 = Client(); u4.login('/api/auth/login', {'email': 'persist@example.com', 'password': PW})
    check(u4.call('GET', '/api/me/account')[1]['data']['balance'] == '1750.50', 'every acknowledged deposit survived kill -9 (1750.50)')

    print('Single-instance lock')
    q = subprocess.run([BIN, '--port', str(PORT + 1), '--data', DATA, '--frontend', os.path.join(ROOT, 'frontend')], capture_output=True, text=True, timeout=10)
    check(q.returncode != 0 and 'already using the data folder' in q.stderr, 'a second server on the same data folder refuses to start')

    print('Cookie / headers behind a proxy (Render-like)')
    stop(p); p = start({'BMS_TRUST_PROXY': '1'})
    c = Client()
    s, d, r = c.login('/api/auth/login', {'email': 'persist@example.com', 'password': PW}, {'X-Forwarded-Proto': 'https', 'X-Forwarded-For': '203.0.113.9'})
    ck = r.getheader('Set-Cookie')
    check(s == 200 and 'Secure' in ck and 'HttpOnly' in ck and 'SameSite=Strict' in ck, 'https cookie: HttpOnly; Secure; SameSite=Strict')
    s, d, r = c.call('GET', '/', headers={'X-Forwarded-Proto': 'https'})
    check('Strict-Transport-Security' in dict(r.getheaders()) or r.getheader('Strict-Transport-Security'), 'HSTS sent over https')
    csp = r.getheader('Content-Security-Policy') or ''
    check("script-src 'self'" in csp and "frame-ancestors 'none'" in csp, 'CSP restricts scripts and framing')
    # lockout is keyed on the forwarded client address
    bad = [Client().login('/api/auth/login', {'email': 'persist@example.com', 'password': 'Wrong%d9x' % i}, {'X-Forwarded-For': '198.51.100.7'})[0] for i in range(6)]
    check(bad[:5] == [401] * 5 and bad[5] == 429, 'brute force: 5 failures then 429 Too Many Requests')
    check(Client().login('/api/auth/login', {'email': 'persist@example.com', 'password': PW}, {'X-Forwarded-For': '192.0.2.55'})[0] == 200, 'owner on another network is not locked out')
    stop(p)

    print('First-admin CLI')
    d2 = tempfile.mkdtemp(prefix='bankpro-cli-')
    run = lambda pw, user='boss': subprocess.run([BIN, '--data', d2, '--create-admin', user], input=pw + '\n', capture_output=True, text=True, timeout=20, env={k: v for k, v in os.environ.items() if k != 'BMS_ADMIN_PASSWORD'})
    r1 = run('short1'); check(r1.returncode == 1 and 'Not created' in r1.stderr, 'weak admin password refused')
    r2 = run('Sup3rSecretPw'); check(r2.returncode == 0 and 'created' in r2.stdout, '--create-admin creates the admin')
    r3 = run('AnotherPassw0rd'); check('already exists' in r3.stdout, 'running it again never overwrites')
    check('Sup3rSecretPw' not in open(os.path.join(d2, 'admins.txt')).read(), 'admin password not stored in plaintext')
    shutil.rmtree(d2, ignore_errors=True)
finally:
    shutil.rmtree(DATA, ignore_errors=True)
print(f'\n{passed} passed, {failed} failed'); sys.exit(1 if failed else 0)
