# Project structure

```
include/  src/            C++17 backend (one header + one source per class)
  Money.h/.cpp              integer-paise money type, safe parsing/formatting
  BankError.h               one exception type + error codes -> HTTP status
  BankAccount.*             abstract account, SavingsAccount / CurrentAccount, active flag
  Transaction.*             immutable record (+ derived reference number)
  AccountLinkedList.h       singly linked list of accounts           (DSA)
  TransactionStack.h        undo / reversal stack                     (DSA)
  CustomerServiceQueue.h    FIFO queue of walk-in tokens              (DSA)
  TransactionHistory.*      ordered log + hash indexes                (DSA)
  SortingAlgorithms.h       hand-written stable merge sort            (DSA)
  UserDirectory.*           customers + admins in hash tables         (DSA)
  FileStorage.*             text files, fsync, commit marker, recovery
  Bank.*                    business rules; owns all data structures
  Validation.*              name / email / phone / password rules
  Security.*                Argon2id hashing, secure random (libsodium)
  SessionManager.*          server-side sessions with timeouts
  RateLimiter.*             sliding-window brute-force limiter
  BankService.*             use-cases as JSON (customer + admin), idempotency
  AuthService.*             register / login / logout / change password
  ApiServer.*               routes, role + CSRF checks, security headers, pages
  main.cpp                  configuration, first admin, start server
frontend/
  pages/                    index, login, register, admin-login, dashboard, admin, 404 (.html)
  static/css/app.css        design system (tokens, components, responsive rules)
  static/js/common.js       API client, toasts, dialogs, formatting, icons
  static/js/auth.js         login / register pages
  static/js/customer.js     customer portal (8 sections)
  static/js/admin.js        admin portal (7 sections + details)
tests/
  test_*.cpp                C++ test suites (run by bank_tests)
  e2e/frontend_e2e.js       jsdom end-to-end test of the real UI
  e2e/restart_persistence.py  real-process tests (restart, kill -9, proxy, CLI)
docs/                       API, security, DSA, OOP, testing, viva questions
CMakeLists.txt  Dockerfile  docker-entrypoint.sh  render.yaml  DEPLOY.md  README.md
```
Dependency direction (never upwards): `ApiServer -> AuthService / BankService -> Bank -> data structures -> FileStorage`.
