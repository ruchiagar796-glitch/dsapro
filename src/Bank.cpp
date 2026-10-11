#include "Bank.h"

#include <algorithm>
#include <cctype>
#include <ctime>

#include "SortingAlgorithms.h"

namespace {
std::string lowerCase(const std::string& s) {
    std::string r;
    for (char c : s) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
std::string num(long long v) { return std::to_string(v); }
}  // namespace

Bank::Bank(const std::string& dataDir, long long firstAccountNumber)
    : firstAccountNumber_(firstAccountNumber), nextAccountNumber_(firstAccountNumber) {
    if (!dataDir.empty()) {
        storage_ = std::make_unique<FileStorage>(dataDir);
        storage_->recover();  // finish or discard a save that was interrupted by a crash
        loadFromStorage();
    }
}

// ---------------------------------------------------------------- persistence
StoredState Bank::snapshot() const {
    StoredState st;
    accounts_.forEach([&](const BankAccount& a) {
        st.accounts.push_back({a.accountNumber(), a.holderName(), a.type(), a.balance(), a.createdAt(), a.active()});
    });
    for (const Customer* c : users_.customers()) st.customers.push_back(*c);
    for (const AdminUser* a : users_.admins()) st.admins.push_back(*a);
    st.transactions = history_.all();
    st.reversedIds.assign(reversedIds_.begin(), reversedIds_.end());
    st.queue = queue_.toVector();
    st.counters = {nextAccountNumber_, nextTransactionId_, queue_.nextTokenNumber(),
                   users_.nextCustomerNumber, users_.nextAdminNumber};
    return st;
}

void Bank::loadFromStorage() {
    const StoredState st = storage_->load();

    // Pass 1: validate everything into temporaries. Nothing in the Bank changes yet.
    std::vector<std::unique_ptr<BankAccount>> loaded;
    std::set<long long> accountNumbers, txIds, tokenNumbers;
    long long maxAccount = 0, maxTx = 0, maxToken = 0;
    for (const AccountRecord& r : st.accounts) {
        if (!accountNumbers.insert(r.accountNumber).second)
            throw BankError(ErrorCode::DataCorrupt, "accounts.txt: duplicate account number " + num(r.accountNumber));
        try {
            loaded.push_back(BankAccount::create(r.type, r.accountNumber, r.holderName, r.createdAt, r.balance));
            loaded.back()->setActive(r.active);
        } catch (const BankError& e) {
            throw BankError(ErrorCode::DataCorrupt,
                            "accounts.txt: account " + num(r.accountNumber) + " is invalid (" + e.what() + ")");
        }
        maxAccount = std::max(maxAccount, r.accountNumber);
    }
    for (const Transaction& t : st.transactions) {
        if (!txIds.insert(t.id()).second)
            throw BankError(ErrorCode::DataCorrupt, "transactions.txt: duplicate transaction id " + num(t.id()));
        maxTx = std::max(maxTx, t.id());
    }
    for (long long id : st.reversedIds)
        if (!txIds.count(id))
            throw BankError(ErrorCode::DataCorrupt,
                            "reversed_transactions.txt: unknown transaction id " + num(id));
    for (const QueueToken& q : st.queue) {
        if (!tokenNumbers.insert(q.number).second)
            throw BankError(ErrorCode::DataCorrupt, "customer_queue.txt: duplicate token " + num(q.number));
        maxToken = std::max(maxToken, q.number);
    }

    // Customers and admins: every customer must own exactly one existing account, and ids,
    // e-mails and usernames must be unique. A temporary directory performs those checks.
    UserDirectory loadedUsers;
    long long maxCustomer = 0, maxAdmin = 0;
    for (const Customer& c : st.customers) {
        if (!accountNumbers.count(c.accountNumber))
            throw BankError(ErrorCode::DataCorrupt, "customers.txt: customer " + c.id + " refers to a missing account");
        try { loadedUsers.addCustomer(c); }
        catch (const BankError& e) { throw BankError(ErrorCode::DataCorrupt, std::string("customers.txt: ") + e.what()); }
        if (c.id.size() > 3 && c.id.compare(0, 3, "CUS") == 0) {
            try { maxCustomer = std::max(maxCustomer, std::stoll(c.id.substr(3))); } catch (...) {}
        }
    }
    for (const AdminUser& a : st.admins) {
        try { loadedUsers.addAdmin(a); }
        catch (const BankError& e) { throw BankError(ErrorCode::DataCorrupt, std::string("admins.txt: ") + e.what()); }
        if (a.id.size() > 3 && a.id.compare(0, 3, "ADM") == 0) {
            try { maxAdmin = std::max(maxAdmin, std::stoll(a.id.substr(3))); } catch (...) {}
        }
    }

    // Pass 2: apply (cannot fail for validation reasons any more).
    users_ = std::move(loadedUsers);
    users_.nextCustomerNumber = std::max(st.counters.nextCustomerNumber, maxCustomer + 1);
    users_.nextAdminNumber = std::max(st.counters.nextAdminNumber, maxAdmin + 1);
    accounts_.clear();
    history_.clear();
    reversible_.clear();
    queue_.clear();
    reversedIds_.clear();
    for (auto& a : loaded) accounts_.insertBack(std::move(a));
    for (const Transaction& t : st.transactions) history_.add(t);
    reversedIds_.insert(st.reversedIds.begin(), st.reversedIds.end());
    for (const QueueToken& q : st.queue) queue_.enqueue(q);

    // Counters never go below what already exists, even if metadata.txt is missing/old.
    nextAccountNumber_ = std::max({st.counters.nextAccountNumber, maxAccount + 1, firstAccountNumber_});
    nextTransactionId_ = std::max(st.counters.nextTransactionId, maxTx + 1);
    queue_.setNextTokenNumber(std::max(st.counters.nextTokenNumber, maxToken + 1));

    // Rebuild the reversal stack in original order: oldest pushed first, newest on top.
    for (const Transaction& t : st.transactions)
        if (t.isReversible() && !reversedIds_.count(t.id()) && accounts_.find(t.accountNumber()))
            reversible_.push(t);
}

void Bank::commit() {
    if (!storage_) return;
    try {
        storage_->save(snapshot());
    } catch (...) {
        try { loadFromStorage(); } catch (...) {}  // roll memory back to the last good files
        throw;
    }
}

Transaction Bank::record(long long account, TransactionType type, money::Paise amount,
                         money::Paise balanceAfter, const std::string& description, long long relatedId) {
    Transaction tx(nextTransactionId_++, account, type, amount, std::time(nullptr), balanceAfter,
                   description, relatedId);
    history_.add(tx);
    return tx;
}

// ------------------------------------------------------------------- accounts
BankAccount& Bank::openAccount(const std::string& holderName, AccountType type) {
    const std::string name = BankAccount::validateName(holderName);
    const long long number = nextAccountNumber_;
    accounts_.insertBack(BankAccount::create(type, number, name, std::time(nullptr), 0));
    ++nextAccountNumber_;
    return *accounts_.find(number);
}

const BankAccount& Bank::createAccount(const std::string& holderName, AccountType type,
                                       money::Paise initialDeposit) {
    const std::string name = BankAccount::validateName(holderName);
    const long long number = nextAccountNumber_;
    auto account = BankAccount::create(type, number, name, std::time(nullptr), 0);
    if (initialDeposit < account->minimumOpeningDeposit())
        throw BankError(ErrorCode::InvalidInput,
                        account->typeName() + " accounts require an opening deposit of at least Rs " +
                            money::format(account->minimumOpeningDeposit()));
    account->deposit(initialDeposit);  // validates limits; throws before anything is stored

    accounts_.insertBack(std::move(account));
    ++nextAccountNumber_;
    Transaction tx = record(number, TransactionType::Deposit, initialDeposit, initialDeposit, "Initial deposit");
    reversible_.push(tx);
    commit();
    return *findAccount(number);
}

// ------------------------------------------------------- customers & admins
const Customer& Bank::registerCustomer(const std::string& fullName, const std::string& email,
                                       const std::string& phone, const std::string& passwordHash) {
    if (users_.findCustomerByEmail(email))
        throw BankError(ErrorCode::Conflict, "An account with this email already exists");
    // Everything below happens in memory first; commit() then writes ALL files atomically, and
    // on failure reloads the last good files, so a half-registered customer can never persist.
    BankAccount& account = openAccount(fullName, AccountType::Savings);
    Customer c;
    c.id = users_.makeCustomerId(users_.nextCustomerNumber);
    c.fullName = account.holderName();
    c.email = email;
    c.phone = phone;
    c.passwordHash = passwordHash;
    c.accountNumber = account.accountNumber();
    c.createdAt = std::time(nullptr);
    try {
        users_.addCustomer(c);
        ++users_.nextCustomerNumber;
        commit();
    } catch (...) {
        // commit() already reloads from disk when saving fails; this covers addCustomer failing
        // (in-memory only banks have no storage_ to reload from).
        if (storage_) { try { loadFromStorage(); } catch (...) {} }
        else { accounts_.remove(c.accountNumber); }
        throw;
    }
    return *users_.findCustomerById(c.id);
}

const AdminUser& Bank::createAdmin(const std::string& username, const std::string& passwordHash) {
    AdminUser a;
    a.id = users_.makeAdminId(users_.nextAdminNumber);
    a.username = username;
    a.passwordHash = passwordHash;
    a.createdAt = std::time(nullptr);
    users_.addAdmin(a);
    ++users_.nextAdminNumber;
    commit();
    return *users_.findAdminById(a.id);
}

void Bank::updateCustomerProfile(const std::string& customerId, const std::string& fullName,
                                 const std::string& phone) {
    Customer* c = users_.mutableCustomer(customerId);
    if (!c) throw BankError(ErrorCode::NotFound, "Customer not found");
    BankAccount* a = accounts_.find(c->accountNumber);
    if (!a) throw BankError(ErrorCode::Internal, "Customer account is missing");
    a->setHolderName(fullName);  // validates; throws before anything changes
    c->fullName = a->holderName();
    c->phone = phone;
    commit();
}

void Bank::setCustomerPasswordHash(const std::string& customerId, const std::string& passwordHash) {
    Customer* c = users_.mutableCustomer(customerId);
    if (!c) throw BankError(ErrorCode::NotFound, "Customer not found");
    c->passwordHash = passwordHash;
    commit();
}

void Bank::setAdminPasswordHash(const std::string& adminId, const std::string& passwordHash) {
    AdminUser* a = users_.mutableAdmin(adminId);
    if (!a) throw BankError(ErrorCode::NotFound, "Administrator not found");
    a->passwordHash = passwordHash;
    commit();
}

void Bank::setAccountActive(long long accountNumber, bool active) {
    BankAccount* a = accounts_.find(accountNumber);
    if (!a) throw BankError(ErrorCode::NotFound, "Account " + num(accountNumber) + " not found");
    a->setActive(active);
    commit();
}

void Bank::closeAccount(long long accountNumber) {
    const BankAccount* a = accounts_.find(accountNumber);
    if (!a) throw BankError(ErrorCode::NotFound, "Account " + num(accountNumber) + " not found");
    if (users_.findCustomerByAccount(accountNumber))
        throw BankError(ErrorCode::Conflict,
                        "This account belongs to a registered customer. Deactivate it instead of closing it.");
    if (a->balance() != 0)
        throw BankError(ErrorCode::Conflict,
                        "Account has a balance of Rs " + money::format(a->balance()) +
                            ". Withdraw or transfer the full balance before closing it.");
    accounts_.remove(accountNumber);
    purgeStackFor(accountNumber);
    commit();
}

void Bank::purgeStackFor(long long accountNumber) {
    // Pop everything, then push back (in original order) what is not being removed.
    std::vector<Transaction> keep;
    while (!reversible_.empty()) {
        Transaction t = reversible_.pop();
        if (t.accountNumber() != accountNumber) keep.push_back(t);
    }
    for (auto it = keep.rbegin(); it != keep.rend(); ++it) reversible_.push(*it);
}

const BankAccount* Bank::findAccount(long long n) const { return accounts_.find(n); }
std::vector<const BankAccount*> Bank::allAccounts() const { return accounts_.toVector(); }
std::vector<const BankAccount*> Bank::searchByName(const std::string& f) const { return accounts_.findByName(f); }

std::vector<const BankAccount*> Bank::sortedAccounts(SortKey key, bool ascending) const {
    std::vector<const BankAccount*> v = accounts_.toVector();  // copy of pointers only
    sorting::mergeSort(v, [&](const BankAccount* a, const BankAccount* b) {
        int cmp = 0;
        if (key == SortKey::Name) {
            const std::string x = lowerCase(a->holderName()), y = lowerCase(b->holderName());
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        } else {
            cmp = a->balance() < b->balance() ? -1 : (a->balance() > b->balance() ? 1 : 0);
        }
        if (cmp == 0) return a->accountNumber() < b->accountNumber();  // deterministic tie-break
        return ascending ? cmp < 0 : cmp > 0;
    });
    return v;
}

// -------------------------------------------------------------- money movement
Transaction Bank::deposit(long long accountNumber, money::Paise amount) {
    BankAccount* a = accounts_.find(accountNumber);
    if (!a) throw BankError(ErrorCode::NotFound, "Account " + num(accountNumber) + " not found");
    a->deposit(amount);
    Transaction tx = record(accountNumber, TransactionType::Deposit, amount, a->balance(), "Cash deposit");
    reversible_.push(tx);
    commit();
    return tx;
}

Transaction Bank::withdraw(long long accountNumber, money::Paise amount) {
    BankAccount* a = accounts_.find(accountNumber);
    if (!a) throw BankError(ErrorCode::NotFound, "Account " + num(accountNumber) + " not found");
    a->withdraw(amount);
    Transaction tx = record(accountNumber, TransactionType::Withdrawal, amount, a->balance(), "Cash withdrawal");
    reversible_.push(tx);
    commit();
    return tx;
}

std::pair<Transaction, Transaction> Bank::transfer(long long from, long long to, money::Paise amount) {
    if (from == to) throw BankError(ErrorCode::InvalidInput, "Cannot transfer to the same account");
    BankAccount* src = accounts_.find(from);
    BankAccount* dst = accounts_.find(to);
    if (!src) throw BankError(ErrorCode::NotFound, "Sender account " + num(from) + " not found");
    if (!dst) throw BankError(ErrorCode::NotFound, "Receiver account " + num(to) + " not found");

    if (!dst->active())
        throw BankError(ErrorCode::Conflict, "The recipient account cannot receive transfers right now");
    // Check BOTH sides before changing either, so we never debit without crediting.
    src->validateWithdrawal(amount);
    dst->validateDeposit(amount);
    src->withdraw(amount);
    dst->deposit(amount);

    const long long outId = nextTransactionId_++;
    const long long inId = nextTransactionId_++;
    const std::time_t now = std::time(nullptr);
    Transaction out(outId, from, TransactionType::TransferOut, amount, now, src->balance(),
                    "Transfer to account " + num(to), inId);
    Transaction in(inId, to, TransactionType::TransferIn, amount, now, dst->balance(),
                   "Transfer from account " + num(from), outId);
    history_.add(out);
    history_.add(in);
    commit();  // transfers are NOT pushed on the reversal stack (documented limitation)
    return {out, in};
}

std::optional<Transaction> Bank::peekReversible() const {
    if (reversible_.empty()) return std::nullopt;
    return reversible_.top();
}

Transaction Bank::reverseLastTransaction(long long expectedId) {
    if (expectedId != 0) {
        const Transaction* t = history_.findById(expectedId);
        if (!t) throw BankError(ErrorCode::NotFound, "Transaction " + num(expectedId) + " not found");
        if (reversedIds_.count(expectedId))
            throw BankError(ErrorCode::Conflict, "Transaction " + num(expectedId) + " has already been reversed");
        if (!t->isReversible())
            throw BankError(ErrorCode::InvalidInput, "Only deposits and withdrawals can be reversed");
    }
    if (reversible_.empty()) throw BankError(ErrorCode::NotFound, "There is no transaction to reverse");
    const Transaction original = reversible_.top();
    if (expectedId != 0 && original.id() != expectedId)
        throw BankError(ErrorCode::Conflict,
                        "Only the most recent reversible transaction (#" + num(original.id()) +
                            ") can be reversed (LIFO order)");
    BankAccount* a = accounts_.find(original.accountNumber());
    if (!a) throw BankError(ErrorCode::Internal, "Account for the transaction no longer exists");

    // These throw BEFORE changing anything if the reversal is unsafe (e.g. negative balance).
    if (original.type() == TransactionType::Deposit) a->reverseDeposit(original.amount());
    else a->reverseWithdrawal(original.amount());

    reversible_.pop();
    reversedIds_.insert(original.id());
    Transaction rev = record(original.accountNumber(), TransactionType::Reversal, original.amount(),
                             a->balance(), "Reversal of " + toString(original.type()) + " #" + num(original.id()),
                             original.id());
    commit();
    return rev;
}

// ------------------------------------------------------------ history/summary
std::vector<Transaction> Bank::transactionsFor(long long acc, std::size_t limit) const {
    return history_.forAccount(acc, limit);
}

Bank::Summary Bank::summary() const {
    Summary s;
    s.accountCount = accounts_.size();
    accounts_.forEach([&](const BankAccount& a) { s.totalBalance += a.balance(); });
    s.transactionCount = history_.size();
    s.customersWaiting = queue_.size();
    return s;
}

// ---------------------------------------------------------------------- queue
QueueToken Bank::issueToken(const std::string& customerName, const std::string& purpose) {
    const std::string name = BankAccount::validateName(customerName);
    if (purpose.size() > 60) throw BankError(ErrorCode::InvalidInput, "Purpose must be at most 60 characters");
    for (char c : purpose)
        if (static_cast<unsigned char>(c) < 0x20) throw BankError(ErrorCode::InvalidInput, "Purpose contains invalid characters");
    QueueToken t = queue_.issueToken(name, purpose, std::time(nullptr));
    commit();
    return t;
}

QueueToken Bank::serveNextCustomer() {
    QueueToken t = queue_.dequeue();
    commit();
    return t;
}
