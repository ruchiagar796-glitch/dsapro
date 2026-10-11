# OOP concepts used (with examples from this project)

## Classes and objects
`BankAccount`, `Transaction`, `Bank`, `BankService`, `TransactionHistory`, `CustomerServiceQueue`, `AccountLinkedList`, `TransactionStack`, `FileStorage`, `ApiServer`. Each has one job (single responsibility).

## Encapsulation
`BankAccount` keeps `balance_` **private**. The only ways to change it are `deposit`, `withdraw`, `reverseDeposit`, `reverseWithdrawal`, and each *validates first, then changes*. So nothing outside can set a negative or overflowing balance.
```cpp
void BankAccount::deposit(money::Paise amount) {
    validateDeposit(amount);   // throws before anything changes
    balance_ += amount;
}
```

## Constructors
- `BankAccount` protected constructor validates the account number, name and opening balance.
- `Transaction` constructor sets all fields once; there are no setters (immutable record).
- `Bank(const std::string& dataDir)` loads saved data on construction.

## Access specifiers
`private` data everywhere; `protected` constructor on the abstract base so only derived classes build accounts; `public` interface functions only.

## Inheritance and runtime polymorphism (justified)
`BankAccount` is abstract with pure virtual functions `typeName()`, `minimumOpeningDeposit()`, `maxWithdrawalPerTransaction()`.
| | Savings | Current |
|---|---|---|
| minimum opening deposit | Rs 500 | Rs 5,000 |
| max per withdrawal | Rs 50,000 | Rs 5,00,000 |
`Bank` calls `account->minimumOpeningDeposit()` through a base pointer and gets the right rule at run time. Inheritance is used because the two types have genuinely different rules. `SavingsAccount` and `CurrentAccount` are `final`.

## Composition
`Bank` *has* an `AccountLinkedList`, `TransactionHistory`, `TransactionStack`, `CustomerServiceQueue` and a `FileStorage` (via `unique_ptr`). They are not inherited from.

## Separation of interface, implementation and business logic
Headers in `include/`, definitions in `src/`. Layers: `ApiServer` (HTTP) -> `BankService` (validation/JSON) -> `Bank` (rules) -> `FileStorage` (files). Each layer only knows the one below. `Bank` can be tested without HTTP.

## Const-correctness
- Getters are `const noexcept`.
- Two overloads of `find`: `BankAccount* find(long long)` and `const BankAccount* find(long long) const`.
- Read operations (`findAccount`, `summary`, `sortedAccounts`) are `const`.

## Function overloading (only where it makes sense)
Only the `find` const/non-const pair (the standard idiom). I deliberately did not add more overloads just to claim the feature.

## Exceptions and error handling
`BankError` (derived from `std::runtime_error`) carries an `ErrorCode` (`InvalidInput`, `NotFound`, `InsufficientFunds`, ...). `BankService` catches it and converts it to an HTTP status and JSON error. Unexpected `std::exception`s become a generic 500 so the server never crashes on bad input.

## RAII, ownership, rule of three/five
- Accounts are owned by `std::unique_ptr<BankAccount>` inside list nodes.
- The list/stack/queue allocate nodes with `new` and free them in their destructor and `clear()`. Because copying a chain of raw pointers would double-delete, **copy constructors and assignment are deleted**.
- `std::lock_guard` releases the service mutex automatically.

## Templates
`sorting::mergeSort<T, Less>` is a function template so the same code sorts any vector with any comparison.

## What is *not* used, and why
No multiple inheritance, no virtual functions on `Transaction` or `Bank` (nothing varies), no operator overloading: adding them would only make the project harder to explain.


## Added in the customer / admin version
* **New classes and their single jobs:** `UserDirectory` (who the customers and admins are), `SessionManager` (who is logged in), `RateLimiter` (how many failures recently), `AuthService` (register/login/password), `Validation` and `security` (pure helper functions in namespaces), `ApiServer` (front door only).
* **Encapsulation again:** `BankAccount::active_` is private; only `setActive` changes it, and `validateDeposit` / `validateWithdrawal` consult it, so *no code path* can move money on a deactivated account. `Bank` never receives a plaintext password - only a hash string - so it cannot leak one.
* **Composition:** `Bank` now also *has-a* `UserDirectory`, so registering a customer and opening their account are one commit.
* **Dependency inversion in miniature:** `SessionManager` and `RateLimiter` take a `Clock` (`std::function<time_t()>`) in the constructor. Production uses the real clock; tests pass a fake one to "fast-forward" 30 minutes instantly.
* **RAII:** `std::lock_guard` releases the mutex even when an exception is thrown; `unique_ptr` owns accounts; the process lock file closes when the program ends.
* **Exceptions as control flow for errors:** new codes `Unauthorized`, `Forbidden`, `RateLimited` map to HTTP 401/403/429 in one place.
* **Templates / lambdas:** `BankService::run(F&&)` wraps every use-case in lock + try/catch; `paginate` is a template that formats customer or admin transactions with a different `ToJson` function.
