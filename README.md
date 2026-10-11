# BankPro - Bank Management System (C++17 - OOP - DSA - File Handling)

A digital banking web application with **two separate portals**: a **Customer portal** (register, deposit, withdraw, transfer, history, profile) and an **Admin portal** (monitor customers, accounts and transactions, activate/deactivate accounts). The backend is C++17, the frontend is plain HTML/CSS/JavaScript, and all data is stored in **text files** (no SQL).

> **Educational simulation.** BankPro is a college DSA + OOP project. It is not a real financial institution - never enter real financial information.

## 1. What you get

| Area | Highlights |
|---|---|
| Landing page | Hero, *Customer Banking* and *Admin Login* buttons, features, security section, footer |
| Customer portal | Registration, login, dashboard (balance card with hide/show, masked account number, totals, recent transactions), My Account, Deposit, Withdraw, Transfer (recipient verification + confirmation), Transaction History (type + date filters, pagination, debit/credit), Profile, Change Password, Logout |
| Admin portal | Overview cards, Customer Management (search/filter/sort), Account Management (activate/deactivate with confirmation), Transaction Monitoring (filters, pagination, reverse latest), Search (customers / by account number), Customer & Account details, Service Queue |
| Security | Argon2id password hashes, random server-side sessions, HttpOnly + SameSite=Strict cookies, CSRF tokens, role checks on every endpoint, ownership enforced by the server, brute-force lockout, CSP and other security headers |
| Data safety | Crash-safe multi-file commits, atomic transfers, duplicate-submission protection, exact money arithmetic (integer paise) |
| Frontend | Responsive (sidebar on desktop, drawer on mobile, table-to-card layout), toasts, confirmation dialogs, loading / empty / error states, no `alert()` pop-ups |

## 2. Quick start

```bash
# 1. Install a C++17 compiler, CMake, Ninja and libsodium  (see section 10 for Windows)
sudo apt install build-essential cmake ninja-build libsodium-dev      # Debian / Ubuntu / WSL

# 2. Build (the first run downloads two header-only libraries, SHA-256 verified)
cmake -S . -B build -G Ninja
cmake --build build

# 3. Run the tests
./build/bank_tests

# 4. Create the first administrator (there is NO default admin account) and start
BMS_ADMIN_USERNAME=admin BMS_ADMIN_PASSWORD='ChooseAStrongOne1' ./build/bank_server
#   -> open http://localhost:8080
```
`BMS_ADMIN_PASSWORD` is only used to create the admin the first time (an existing admin is never overwritten). Alternative: `./build/bank_server --create-admin admin` asks for the password on the keyboard.

## 3. Architecture

```
Browser (HTML/CSS/JS)
   |  JSON over HTTP, session cookie + CSRF header
   v
ApiServer      front door: cookie -> session -> role check -> CSRF/Origin check -> route
   |
   +-- AuthService     register, login, logout, change password, brute-force limits
   |       uses: Validation, Security (Argon2id), SessionManager, RateLimiter
   +-- BankService     one method per use-case; takes the customer id FROM THE SESSION
   |
   v
Bank           business rules + owns the data structures (one mutex = serialisable transactions)
   |-- AccountLinkedList  (singly linked list)     |-- TransactionHistory (vector + hash indexes)
   |-- TransactionStack   (undo / reversal)        |-- CustomerServiceQueue (FIFO)
   |-- UserDirectory      (hash tables: customers, admins)
   v
FileStorage    text files, fsync + commit-marker atomic save, crash recovery
```
One request, end to end (customer transfers Rs 250 to account X):
1. `POST /api/me/transfer {toAccount, amount, idempotencyKey}` with cookie `bms_session` and header `X-CSRF-Token`.
2. `ApiServer` finds the session on the **server**, checks role = customer, checks the CSRF token and Origin.
3. `BankService::transfer(session.customerId, ...)` locks, validates amount / recipient, and ignores any "from" account in the body - the sender **is** the session's account.
4. `Bank::transfer` validates **both** accounts first, then debits and credits, records two transactions with one shared reference.
5. `FileStorage::save` writes all files atomically. If saving fails, memory is reloaded from the last good files, so nothing half-happens.
6. JSON goes back; the browser re-reads the balance from the server.

Full file list: `PROJECT_STRUCTURE.md`.

## 4. Customer and admin authorization (how it works)

* Login creates a **server-side session** (256-bit random id) holding `{role, customerId/adminId, csrfToken}`. The browser only holds the random id in an `HttpOnly` cookie; JavaScript cannot read it.
* The browser can never state its role, customer id or account number: the server looks them up from the session.
* `/api/me/*` routes require role **customer**; `/api/admin/*` routes require role **admin**. A wrong role gets 403, no session gets 401. The dashboards' HTML pages are also protected by the server (redirect to the login page).
* Customer queries are built from the session's own account (`Bank::transactionsFor(myAccount)`), so there is no parameter through which another customer's data could be requested. Tests send `?account=...`, `customerId` and `role` fields on purpose and prove they are ignored.
* Hiding buttons in JavaScript is only for convenience - it is not the security.

More detail and the threat checklist: `docs/SECURITY.md`.

## 5. Transaction logic (money safety)

* Money is stored as **integer paise** (never floating point). Amounts are parsed from text with at most 2 decimals, > 0, and <= Rs 1 crore.
* **Validate first, change second.** `Bank::transfer` checks sender, recipient, limits, balance and status before touching either balance, so a failed transfer cannot deduct money without crediting.
* **Both legs, one reference.** A transfer creates a `TransferOut` and a `TransferIn` record; both show the same reference `TXN00000042` (derived from the smaller of the two linked ids).
* **Atomic persistence.** The new state is written to all files in one commit (section 6); if it fails, memory is rolled back from disk.
* **Serialisable concurrency.** A single mutex in `BankService` makes every operation atomic with respect to every other. A test fires 160 concurrent deposits/transfers and checks the exact final balances.
* **No double submissions.** The browser sends an `idempotencyKey` per form; the server remembers it per customer for 15 minutes and replays the first result instead of moving money twice. The submit button is also disabled while a request is running.
* Deactivated accounts cannot deposit, withdraw, send or receive transfers (the server enforces it).

## 6. File handling

Everything lives in one data folder (`data/`, or `BMS_DATA_DIR`). Plain text, `|`-separated, `#` comments, `|` and `\` escaped inside fields:

| File | Line format |
|---|---|
| `accounts.txt` | `accountNumber\|holderName\|type\|balancePaise\|createdAt\|status` |
| `customers.txt` | `customerId\|fullName\|email\|phone\|passwordHash\|accountNumber\|createdAt` |
| `admins.txt` | `adminId\|username\|passwordHash\|createdAt` |
| `transactions.txt` | `id\|account\|type\|amountPaise\|timestamp\|balanceAfterPaise\|relatedId\|description` |
| `metadata.txt` | counters (`next_account_number`, `next_transaction_id`, `next_customer_number`, ...) so ids are never reused |
| `reversed_transactions.txt`, `customer_queue.txt` | undo state and the waiting queue |

**Crash-safe saving** (`FileStorage::save`): (1) write every file to `name.tmp` and `fsync`; (2) atomically create a small `COMMIT` marker - the point of no return; (3) rename the temp files over the real ones and delete `COMMIT`. On start-up `recover()` finishes an interrupted step 3 (roll forward) or discards temp files of an interrupted step 1. Result: after any crash you get the complete old state or the complete new state, never a mixture. **Loading** validates everything (field counts, numbers, duplicates, every customer owns an existing account); a malformed file stops start-up with a clear message and **is never modified**. Old 5-field account lines still load.

Only one server may use a data folder at a time (a lock file enforces this).

## 7. Environment variables

| Variable | Meaning | Default |
|---|---|---|
| `PORT` | listening port (Render sets it) | `8080` |
| `BMS_HOST` | listen address (`0.0.0.0` in Docker) | `127.0.0.1` |
| `BMS_DATA_DIR` | data folder - **mount your persistent disk here** | `data` |
| `BMS_ADMIN_USERNAME` | first administrator's user name | `admin` |
| `BMS_ADMIN_PASSWORD` | first administrator's password (**secret**, min 10 chars, letter + digit) | not set |
| `BMS_TRUST_PROXY` | `1` behind a reverse proxy such as Render (real client IP, https flag) | off |
| `BMS_COOKIE_SECURE` | `1` forces the `Secure` cookie flag | auto from `X-Forwarded-Proto` |
| `BMS_FIRST_ACCOUNT_NUMBER` | first account number of a brand-new bank | `1000000001` |
| `BMS_STORAGE_PERSISTENT` | `true`/`false` overrides automatic disk detection | auto |

The old `BMS_USER` / `BMS_PASSWORD` (one site-wide password) are no longer used and are ignored with a notice.

## 8. OOP and DSA concepts

OOP: encapsulation (private balance, validated mutators), abstraction + inheritance + runtime polymorphism (`BankAccount` -> `SavingsAccount`/`CurrentAccount`), composition (`Bank` has-a list, stack, queue, directory, storage), RAII (`unique_ptr`, `lock_guard`), exceptions (`BankError`), single-responsibility layering. DSA: singly linked list (accounts), stack (reversals), queue (service tokens, also the rate limiter's sliding window), hash tables (customers by id/email/account, sessions, transaction indexes), merge sort (customer/account sorting), linear and hash search. Details with complexities: `docs/OOP_CONCEPTS.md`, `docs/DSA_COMPLEXITY.md`, viva Q&A: `docs/VIVA_QUESTIONS.md`.

## 9. Testing

| Suite | What | Command |
|---|---|---|
| C++ tests (86) | accounts, transactions, file storage + crash recovery, data structures, validation, hashing, sessions, rate limiter, services, authorization, **real HTTP end-to-end** (cross-customer access, admin protection, CSRF, concurrency, restart persistence) | `./build/bank_tests` |
| Frontend end-to-end (69 checks) | real pages + JavaScript against the real server using jsdom: register, login, dashboard, deposit, withdraw, transfer, history filters, profile, password, admin portal, XSS, console errors | `cd tests/e2e && npm install && node frontend_e2e.js` |
| Real-process checks (27) | restart persistence, `kill -9` durability, instance lock, cookie flags behind a proxy, brute-force lockout, `--create-admin`, no plaintext passwords on disk | `python3 tests/e2e/restart_persistence.py` |

The C++ tests also passed under AddressSanitizer + UBSan and ThreadSanitizer when this version was built (see section 12). Details: `docs/TESTING.md`.

## 10. Installation (details)

**Linux / WSL / macOS:** install a C++17 compiler, CMake >= 3.16, Ninja and **libsodium** (`apt install libsodium-dev`, `brew install libsodium`), then follow the quick start.

**Windows (VS Code):** the easiest route is **WSL** (Ubuntu) and the Linux commands above, or **Docker**: `docker build -t bankpro .` then `docker run -p 8080:8080 -e BMS_ADMIN_PASSWORD=ChooseAStrongOne1 -v bankdata:/data bankpro`. For a native build install libsodium with vcpkg (`vcpkg install libsodium:x64-windows-static`) and add `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` to the CMake configure command. I could not test a native Windows build in the environment this was developed in.

**Dependencies:** `httplib.h` and `nlohmann/json.hpp` are downloaded by CMake into `third_party/` (SHA-256 verified). If you are offline, download them manually to the paths printed in the error message.

**Troubleshooting:** *"libsodium not found"* - install `libsodium-dev`. *"Another bank_server is already using the data folder"* - stop the other copy. *Start-up says a data file is malformed* - nothing was changed; fix or restore the named line (or delete the data folder for a fresh start). *Nobody can log in as admin* - set `BMS_ADMIN_PASSWORD` or run `--create-admin`.

## 11. Deployment (Render)

See **`DEPLOY.md`** - environment variables, the persistent-disk setup (`/data`), first-admin initialisation and how to redeploy. **Important:** on Render's free plan the file system is temporary, so customers and balances disappear on every restart or redeploy; the admin dashboard shows a warning when this is the case.

## 12. Verification status (honest summary)

Done and run while building this version:
* C++17 build with `-Wall -Wextra` - no warnings; a clean-directory build that mirrors the Docker build stage; **86/86 C++ tests**; the same suite under ASan+UBSan and TSan with no findings.
* **69/69** jsdom frontend checks and **27/27** real-process checks (restart, `kill -9`, lock, proxy cookies, CLI).

**Not** done / not verifiable here - please do these yourself:
* The Docker image was **not built** (no Docker in the build environment) and nothing was deployed to Render or pushed to GitHub. The Dockerfile steps were reproduced manually.
* The frontend was **not opened in a real browser**, so visual design and responsive layout on phones/tablets have not been visually inspected. The responsive CSS is written for three breakpoints (1000, 860 and 720 px) but needs your eyes. Re-take screenshots after deploying.
* Native Windows build and the Render persistent-disk ownership fix (`docker-entrypoint.sh`) are untested.

## 13. Known limitations and ideas

* Sessions and idempotency keys are in memory: a restart signs everyone out (data is safe).
* Account lookup is a linked list, O(A): fine for hundreds of accounts, a hash index would be next for thousands.
* Whole-file rewrite on every change is simple and safe but O(data size); a database or an append-only log would scale better.
* No e-mail verification / password reset by e-mail (no mail service); an admin cannot reset a customer's password (by design: admins never handle credentials).
* Customers can "deposit" without a real cash source - this is a simulation.

## 14. GitHub

```bash
git add -A
git commit -m "Customer + admin portals, authentication, crash-safe storage, tests"
git push
```
`data/`, `build/`, `third_party/` and `node_modules/` are git-ignored; never commit the `data/` folder (it holds customer data and password hashes).
