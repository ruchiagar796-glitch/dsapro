# Security design and checklist

| Requirement | How it is met | Where | Verified by |
|---|---|---|---|
| Passwords hashed, never plaintext | Argon2id (libsodium `crypto_pwhash_str`), random salt per password, cost "interactive" (64 MiB, about 65 ms) | `Security.cpp` | `password_hashing_*`, real-process check that no plaintext is in the data files |
| Passwords never logged/returned | no log statement touches a body; profile JSON has no hash | `BankService.cpp` | `admin_views_never_contain_password_hashes`, e2e HTML scan |
| Secure session ids | 256-bit CSPRNG token (`randombytes_buf`), stored server-side only, new id on every login | `SessionManager.cpp` | `sessions_expire_*`, `login_errors_*` |
| Session timeouts / logout | 30 min idle, 8 h absolute; logout destroys the server session; password change signs out other devices | `SessionManager`, `AuthService` | `http_register_login_logout_cycle`, `password_change_*` |
| Cookie flags | `HttpOnly; SameSite=Strict; Path=/`; `Secure` over https (`X-Forwarded-Proto` or `BMS_COOKIE_SECURE`) | `ApiServer.cpp` | `http_*`, proxy check |
| CSRF | SameSite=Strict **plus** per-session token header on every write, **plus** Origin / `Sec-Fetch-Site` check, **plus** JSON-only bodies | `ApiServer.cpp` | `http_csrf_origin_and_content_type_checks` |
| Role never trusted from the browser | role/customer id come from the server session; body fields `role`, `customerId`, `accountNumber`, `email` are ignored | `BankService`, `ApiServer` | `customers_only_ever_see_their_own_data`, `http_customer_a_cannot_touch_customer_b` |
| Ownership checks | customer queries are built from the session's account; no way to name another | `BankService::me()`, `transactionsFor` | same tests |
| Customers cannot call admin endpoints | route guard `Need::Admin` -> 403 | `ApiServer.cpp` | all `/api/admin/*` probed as customer |
| Brute force | failures counted per ip+account (5/15 min), per account (30/15 min), per ip (40/15 min); registrations 10/h/ip; recipient look-ups 20/min | `AuthService`, `RateLimiter` | `brute_force_*`, real-process 429 check |
| No user enumeration | identical error text for unknown e-mail / wrong password and a decoy-hash verification so timing matches | `AuthService::login` | `login_errors_do_not_reveal_*` |
| Input validation | server-side name/e-mail/phone/password/amount/query validation; strict amount parser | `Validation.cpp`, `Money.cpp` | `validation_*`, `service_validates_amounts` |
| XSS | every dynamic string is HTML-escaped; names reject markup characters; strict CSP (`script-src 'self'`, no inline script/style) | `common.js`, `ApiServer.cpp` | e2e XSS check |
| Clickjacking / sniffing | `X-Frame-Options: DENY`, `frame-ancestors 'none'`, `nosniff`, HSTS over https | `ApiServer.cpp` | `http_pages_and_basic_protection` |
| Safe errors | 500s show a generic message; details only in the server log | `BankService::run` | by inspection |
| No hardcoded credentials | no default admin; first admin from a secret env var or CLI prompt | `main.cpp` | `admin_bootstrap_*`, CLI check |
| Data integrity | atomic multi-file commit, rollback on failed save, serialised operations, idempotency keys | `FileStorage`, `Bank`, `BankService` | crash tests, concurrency test, `kill -9` check |

**Known gaps (be honest in your viva):** no e-mail verification or MFA; session ids are held in memory (a restart signs everyone out); `X-Forwarded-For` is trusted only when `BMS_TRUST_PROXY=1` and a client can spoof it, which is why a per-account limit that ignores the IP also exists; that per-account limit (30 failures / 15 min) means a determined attacker can temporarily block a victim's logins (a denial-of-service trade-off chosen over allowing unlimited guessing); a real bank would add audit logs, MFA, managed secrets and database-grade storage.
