# Testing

## Suites
| Suite | Count | Command |
|---|---|---|
| C++ tests (`bank_tests`, no external framework) | 86 | `./build/bank_tests` (or `ctest --test-dir build --output-on-failure`) |
| Frontend end-to-end (jsdom: real pages + real JS vs real server) | 69 checks | `cd tests/e2e && npm install && node frontend_e2e.js [path/to/bank_server]` |
| Real-process checks (restart, kill -9, lock, proxy cookies, CLI) | 27 checks | `python3 tests/e2e/restart_persistence.py [path/to/bank_server]` |

Run a subset of the C++ tests with `./build/bank_tests transfer` (name contains the text).

## C++ test files
| File | Covers |
|---|---|
| `test_accounts.cpp` | account creation rules, unique numbers, Savings vs Current, search, close, money parsing |
| `test_transactions.cpp` | deposit/withdraw/transfer rules, nothing changes on failure, LIFO reversal, history, queue |
| `test_file_storage.cpp` | save/reload, escaping, id persistence, 14 malformed-file cases (files untouched), failed save rolls back |
| `test_data_structures.cpp` | linked list, stack, queue, merge sort |
| `test_service.cpp` | customer + admin services: amount validation, correct balances, shared transfer reference, idempotency, ownership, deactivation, masking, filters, search/sort, no hashes in output |
| `test_security.cpp` | Argon2id, random tokens, sessions (idle/absolute expiry, destroy), rate limiter, input validation |
| `test_users.cpp` | registration persistence, duplicates, failed save leaves no half registration, admins, status persistence, legacy files, 8 corrupt customer/admin files, **crash roll-forward / discard**, transfers survive restart |
| `test_authorization.cpp` | AuthService: field validation, hashing, duplicates, identical login errors, lockout, registration limit, admin bootstrap, password change |
| `test_http.cpp` | a real server on a free port: page protection, register/login/logout, cookie flags, CSRF/Origin/Content-Type, money flows, **Customer A cannot touch Customer B**, customer->admin 403, admin portal, lockout, double-submit, **160 concurrent requests keep balances exact**, restart persistence |

## Other checks performed when this version was built
* Clean-directory build mirroring the Docker build stage (static libsodium, only libc needed at runtime).
* The whole C++ suite under AddressSanitizer + UndefinedBehaviorSanitizer, and under ThreadSanitizer: no findings.
* Production-cost Argon2id: about 65 ms per login, peak memory about 71 MB.

## What is NOT covered
* Visual layout and responsiveness (no real browser was available); do a manual pass at about 360, 768 and 1280 px width.
* The Docker image build and the Render deployment.
* A native Windows build.

## Manual checklist for you
1. Register two customers in two browsers (or a private window). 2. Deposit, withdraw, transfer; compare balances. 3. Open `/admin` while signed in as a customer: you are redirected. 4. Sign in as admin, deactivate customer A; A's deposit must be refused. 5. Stop the server (Ctrl+C), start it again: balances and logins must still be there.
