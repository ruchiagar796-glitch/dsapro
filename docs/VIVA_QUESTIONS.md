# Viva questions and answers (project specific)

## OOP
**1. What is encapsulation here?** `BankAccount` keeps `balance_` private. Only `deposit`, `withdraw`, `reverseDeposit` and `reverseWithdrawal` change it, and each validates first. Outside code cannot set a wrong balance.

**2. Where do you use inheritance and why?** `BankAccount` -> `SavingsAccount`/`CurrentAccount`. They really differ: minimum opening deposit (Rs 500 vs Rs 5,000) and withdrawal limit (Rs 50,000 vs Rs 5,00,000). I did not add inheritance elsewhere because nothing else varies.

**3. Where is runtime polymorphism?** `Bank::createAccount` and `BankAccount::validateWithdrawal` call virtual functions (`minimumOpeningDeposit()`, `maxWithdrawalPerTransaction()`) through a base class; the derived class decides the value.

**4. Why is `BankAccount` abstract?** A generic "account" without a type has no rules. Pure virtual functions force each type to define its rules; objects are created through the factory `BankAccount::create`.

**5. What is composition? Example?** "Has-a" relationship. `Bank` has an `AccountLinkedList`, `TransactionStack`, `CustomerServiceQueue`, `TransactionHistory` and `FileStorage`.

**6. Why `const` and two `find` overloads?** Read-only callers (`const Bank&`) must not get a modifiable account. The `const` overload returns `const BankAccount*`; the non-const one returns `BankAccount*` for `Bank` to modify balances.

**7. How do you handle errors?** One exception type `BankError` with an `ErrorCode`. `BankService` catches it and maps it to an HTTP status and JSON message. Other `std::exception`s become a generic 500 so the server cannot crash from bad input.

**8. Why are copy constructors deleted for the list, stack and queue?** They own raw-pointer nodes. A default copy would copy pointers, and both objects would `delete` the same nodes (double free).

**9. How do you separate layers?** HTTP (`ApiServer`) -> validation/JSON (`BankService`) -> rules (`Bank`) -> files (`FileStorage`). `Bank` has no HTTP or JSON, so tests call it directly.

## DSA
**10. Why a linked list for accounts?** Required to demonstrate pointers and node management. It gives easy deletion without shifting elements. Cost: lookup is O(n).

**11. Why not a vector or hash table?** Vector/hash hide the node mechanics the project is meant to show. A hash table would give O(1) average lookup and is listed as a future improvement.

**12. Complexity of insertion?** `insertBack` is O(n) in my code, because it first scans for a duplicate account number. Linking at the tail is O(1) thanks to the `tail_` pointer, but the check dominates.

**13. How do you delete a node?** Keep a `prev` pointer while scanning. If the node is the head, move `head_`; otherwise `prev->next = cur->next`. If it was the tail, move `tail_` to `prev`. Then `delete cur` and decrement `size_`.

**14. How is the stack used?** Each deposit/withdrawal is pushed. Undo looks at the top, validates, and pops. LIFO means only the newest reversible transaction can be undone.

**15. Why are transfers not reversible?** A transfer touches two accounts; undoing one leg could fail while the other succeeds. Safe two-account reversal was out of scope, so transfers are excluded and documented.

**16. How do you stop reversing the same transaction twice?** Reversed ids are kept in a set and saved in `reversed_transactions.txt`; on restart the stack is rebuilt skipping those ids. Also the transaction is popped from the stack.

**17. Can a reversal create a negative balance?** No. `reverseDeposit` checks `amount <= balance` before subtracting. If it fails (for example money was transferred out), the transaction stays on the stack and an error is shown. This can block older undos; documented limitation.

**18. How is the queue implemented and why?** Linked nodes with `head_` and `tail_`. Enqueue at tail, dequeue at head, both O(1). FIFO matches serving customers in arrival order.

**19. Why does the queue need a tail pointer?** Without it each enqueue would walk the whole list (O(n)). With it enqueue is O(1). When the queue becomes empty, `tail_` must be reset to null (a classic bug).

**20. Searching: which algorithms and complexity?** Linear search by account number O(n); substring search by name O(n*m). A sorted array + binary search or a hash map would be faster but the primary collection is a linked list.

**21. Why merge sort?** Guaranteed O(n log n), stable, and works without random access. It sorts a vector of pointers, so the original list is untouched. Extra space O(n).

**22. What does "stable" mean and why does it matter?** Equal keys keep their original order. I also tie-break on account number so output is deterministic.

## File handling
**23. How is data stored?** Five text files in `data/` (accounts, transactions, metadata, reversed ids, queue), one record per line, fields separated by `|`.

**24. How do you handle names containing `|`?** Escaping: `\` -> `\\`, `|` -> `\|`, newline -> `\n`. The parser reverses it; an unknown escape is treated as corruption.

**25. Why integer paise instead of `double`?** Floating point cannot represent values like 0.10 exactly, so errors accumulate. Integers are exact. 1 rupee = 100 paise, parsed from text without floating point.

**26. How do you avoid corrupting files?** Write every file to `*.tmp`, check each write, then rename over the real file. If a write fails, the real files are untouched and in-memory state is reloaded from them.

**27. What can still go wrong?** Five renames happen one after another. A crash between them can leave files from different saves. A real system would use a database with transactions or a write-ahead log. This is stated in the README.

**28. What happens with a corrupted file at start-up?** The server prints the file name and line and refuses to start. It never overwrites data it could not read.

**29. How do you keep IDs unique after restart?** Counters are saved in `metadata.txt`; on load they are also raised above the maximum id found in the data. Closed account numbers are never reused.

**30. What if the same save fails halfway (disk full)?** The exception propagates, `Bank::commit` reloads the last good files, and the user gets a 500 `STORAGE_FAILURE`. The balance did not change.

## C++ memory management
**31. Who owns what?** The list owns its nodes (`new`/`delete`); each node owns its account via `unique_ptr`. `Bank` owns `FileStorage` via `unique_ptr`. Destructors free everything; AddressSanitizer found no leaks.

**32. Why is the destructor iterative?** A recursive destructor would use one stack frame per node and could overflow with a long list.

## API and communication
**33. How does the browser talk to C++?** `api.js` calls `fetch('/api/...')`; the C++ server (cpp-httplib) parses JSON, calls `BankService`, and returns JSON. The same server serves the HTML, so there is no CORS problem.

**34. Why a mutex?** cpp-httplib handles requests on a thread pool. A `std::mutex` in `BankService` makes each operation atomic with respect to others and avoids data races on the shared lists.

**35. What HTTP status codes are used?** 200/201 success, 400 bad input, 404 missing, 409 conflict, 413 too large, 422 business rule (insufficient funds), 500 server/storage error.

## Validation and design decisions
**36. Where is validation done?** In the browser for usability and **again** in C++ (`BankService` and `BankAccount`), which is the real protection. The browser can be bypassed with curl.

**37. How are overflow risks handled?** Amounts have at most 12 whole digits, a per-transaction limit (Rs 1 crore) and a maximum balance (Rs 100 billion), all in 64-bit integers far below overflow.

**38. How do you guarantee a transfer never debits without crediting?** Both sides are validated (`validateWithdrawal`, `validateDeposit`) before either balance changes, then both changes are applied and saved in one save.

**39. Why can an account be closed only at zero balance?** Closing with money would destroy funds. The user must withdraw or transfer first.

## Limitations and improvements
**40. Biggest limitations?** No authentication, not crash-proof across five files, whole files rewritten per change, undo only for deposits/withdrawals in LIFO order.

**41. How would you improve it?** SQLite or a write-ahead log, login with hashed passwords, hash-table index for accounts, reversal of whole transfers, pagination, interest and statements.

**42. How did you test it?** 41 automated tests (data structures, business rules, file persistence including 14 corrupt-file cases, service validation), AddressSanitizer/UBSan runs, live curl tests, and a headless-browser UI test. Windows was not tested by me.


## Customer / admin version: security and design
**A1. How do you make sure Customer A cannot see Customer B's balance?** The browser never says whose data it wants. After login the server stores `customerId` in a server-side session; `BankService` methods take that id from the session and look up *that customer's* account. There is no parameter for another account, and my tests send `accountNumber`, `customerId` and `role` in the URL and body to prove they are ignored.

**A2. Why are passwords hashed, and with what?** If the files leak, plaintext passwords would be exposed. I use Argon2id (libsodium): salted, deliberately slow and memory-hard, so guessing is expensive. I do not write my own crypto.

**A3. What is a session and why not just store the customer id in a cookie?** A cookie the browser holds can be edited. I store a 256-bit random id in the cookie and keep the real identity on the server; an edited id simply is not found.

**A4. What is CSRF and how do you stop it?** Another website making your browser send a request with your cookie. Defences here: `SameSite=Strict` cookies, a secret CSRF token header required on every write, an Origin check, and JSON-only bodies.

**A5. How do you prevent a transfer from debiting without crediting?** `Bank::transfer` validates both accounts first and only then changes both; all files are saved in one atomic commit; if saving fails, memory is reloaded from the last good files. One mutex makes each operation indivisible.

**A6. What if the server crashes during a save?** Files are written to `.tmp` and `fsync`ed, then a `COMMIT` marker is created atomically, then renamed into place. On restart, a present marker means "finish the renames" (new state), an absent one means "delete the temp files" (old state). Never a mixture.

**A7. What is idempotency?** Doing the same request twice has the same effect as once. The browser sends a unique key per form; the server replays the first result for a repeated key, so a double-click cannot deposit twice.

**A8. How does the login avoid telling attackers which e-mails exist?** Same error message, and an Argon2 check against a decoy hash when the user is unknown so the response time is the same.

**A9. Why a hash table for customers but a linked list for accounts?** The linked list is the project's required DSA showcase and is fine for hundreds of accounts; customers are always found by exact key (id, e-mail, account), which is exactly what a hash table is for. I would index accounts the same way at scale.

**A10. Why integer paise?** Floating point cannot represent 0.10 exactly; adding such values drifts. Integers are exact.

**A11. What would you add next?** MFA and e-mail verification, audit logging, a real database (or append-only log) for scale, a hash index for accounts, and a session store that survives restarts.
