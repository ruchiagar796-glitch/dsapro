#pragma once
// Bank: the business-logic class. It COMPOSES the linked list (accounts), the
// transaction history, the reversal stack, the customer queue and the file
// storage. It knows nothing about HTTP or JSON.
//
// Safety rule: every public mutating function validates first, mutates memory,
// then saves to disk. If the save fails, the in-memory state is rolled back by
// reloading the last good files and the error is re-thrown.
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "AccountLinkedList.h"
#include "CustomerServiceQueue.h"
#include "FileStorage.h"
#include "TransactionHistory.h"
#include "TransactionStack.h"
#include "UserDirectory.h"

enum class SortKey { Name, Balance };

class Bank {
public:
    struct Summary {
        std::size_t accountCount = 0;
        money::Paise totalBalance = 0;
        std::size_t transactionCount = 0;
        std::size_t customersWaiting = 0;
    };

    // dataDir empty -> memory only (used by some tests). Otherwise loads the files
    // (throws BankError if they are corrupt: we refuse to start rather than overwrite).
    // firstAccountNumber is only used while the bank is still empty (no metadata yet); once
    // accounts exist the counter continues from the data. Tests keep the default 1001.
    explicit Bank(const std::string& dataDir = "", long long firstAccountNumber = 1001);
    ~Bank() = default;
    Bank(const Bank&) = delete;
    Bank& operator=(const Bank&) = delete;

    // ---- accounts ----
    const BankAccount& createAccount(const std::string& holderName, AccountType type,
                                     money::Paise initialDeposit);
    void closeAccount(long long accountNumber);  // only when balance == 0
    const BankAccount* findAccount(long long accountNumber) const;
    std::vector<const BankAccount*> allAccounts() const;
    std::vector<const BankAccount*> searchByName(const std::string& fragment) const;
    std::vector<const BankAccount*> sortedAccounts(SortKey key, bool ascending) const;

    // ---- customers & administrators (all of these save atomically with the accounts) ----
    // Registers a customer AND opens their Savings account with a zero balance in ONE commit:
    // either both exist afterwards or neither does. Inputs must already be validated and the
    // password already hashed (Bank never sees plaintext passwords).
    const Customer& registerCustomer(const std::string& fullName, const std::string& email,
                                     const std::string& phone, const std::string& passwordHash);
    const AdminUser& createAdmin(const std::string& username, const std::string& passwordHash);
    void updateCustomerProfile(const std::string& customerId, const std::string& fullName,
                               const std::string& phone);
    void setCustomerPasswordHash(const std::string& customerId, const std::string& passwordHash);
    void setAdminPasswordHash(const std::string& adminId, const std::string& passwordHash);
    const UserDirectory& users() const noexcept { return users_; }

    // Activate / deactivate an account (deactivated accounts cannot deposit, withdraw or transfer).
    void setAccountActive(long long accountNumber, bool active);

    // ---- money movement ----
    Transaction deposit(long long accountNumber, money::Paise amount);
    Transaction withdraw(long long accountNumber, money::Paise amount);
    // Returns {debit record, credit record}.
    std::pair<Transaction, Transaction> transfer(long long from, long long to, money::Paise amount);
    // Reverses the most recent reversible transaction (LIFO). If expectedId != 0 it
    // must be that transaction, otherwise an error explains why not.
    Transaction reverseLastTransaction(long long expectedId = 0);
    std::optional<Transaction> peekReversible() const;
    const Transaction* findTransaction(long long id) const { return history_.findById(id); }
    bool isReversed(long long id) const { return reversedIds_.count(id) != 0; }
    const std::vector<Transaction>& allTransactions() const { return history_.all(); }
    std::size_t reversibleCount() const { return reversible_.size(); }

    // ---- history & summary ----
    std::vector<Transaction> recentTransactions(std::size_t limit) const { return history_.recent(limit); }
    std::vector<Transaction> transactionsFor(long long accountNumber, std::size_t limit = 0) const;
    Summary summary() const;

    // ---- customer service queue ----
    QueueToken issueToken(const std::string& customerName, const std::string& purpose);
    QueueToken serveNextCustomer();
    std::vector<QueueToken> queueSnapshot() const { return queue_.toVector(); }
    long long nextTokenNumber() const { return queue_.nextTokenNumber(); }

private:
    Transaction record(long long account, TransactionType type, money::Paise amount,
                       money::Paise balanceAfter, const std::string& description,
                       long long relatedId = 0);
    void commit();                  // save, or roll back + rethrow
    StoredState snapshot() const;
    void loadFromStorage();
    void purgeStackFor(long long accountNumber);
    BankAccount& openAccount(const std::string& holderName, AccountType type);  // zero balance

    UserDirectory users_;
    long long firstAccountNumber_ = 1001;
    AccountLinkedList accounts_;
    TransactionHistory history_;
    TransactionStack reversible_;
    CustomerServiceQueue queue_;
    std::set<long long> reversedIds_;
    long long nextAccountNumber_ = 1001;
    long long nextTransactionId_ = 1;
    std::unique_ptr<FileStorage> storage_;
};
