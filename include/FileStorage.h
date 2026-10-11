#pragma once
// FileStorage: reads and writes the plain-text data files.
// It knows nothing about Bank; it only converts a StoredState <-> files.
//
// File format: one record per line, fields separated by '|'.
// Inside a field:  '\' -> "\\",  '|' -> "\|",  newline -> "\n",  CR -> "\r".
// Lines starting with '#' are comments. Money is integer paise.
//
// Files (all inside the data directory):
//   accounts.txt      accountNumber|holderName|type|balancePaise|createdAt[|status]
//   transactions.txt  id|accountNumber|type|amountPaise|timestamp|balanceAfterPaise|relatedId|description
//   customers.txt     customerId|fullName|email|phone|passwordHash|accountNumber|createdAt
//   admins.txt        adminId|username|passwordHash|createdAt
//   metadata.txt      key=value counters that keep ids unique across restarts
//   reversed_transactions.txt, customer_queue.txt   (see the headers inside the files)
// "status" is "active" or "deactivated"; old 5-field account lines mean "active".
//
// CRASH SAFETY (all files change together or not at all):
//   1. every file is written to "<name>.tmp" and fsync'ed
//   2. a tiny COMMIT marker file is written atomically  <- the point of no return
//   3. the .tmp files are renamed over the real files, then COMMIT is removed
// If the process dies after step 2, recover() finishes step 3 on the next start (roll
// forward). If it dies before step 2, recover() throws the half-written .tmp files away and
// the old, consistent files are used.
#include <ctime>
#include <string>
#include <vector>

#include "BankAccount.h"
#include "CustomerServiceQueue.h"
#include "Transaction.h"
#include "UserDirectory.h"

struct AccountRecord {
    long long accountNumber = 0;
    std::string holderName;
    AccountType type = AccountType::Savings;
    money::Paise balance = 0;
    std::time_t createdAt = 0;
    bool active = true;
};

struct Counters {
    long long nextAccountNumber = 1001;
    long long nextTransactionId = 1;
    long long nextTokenNumber = 1;
    long long nextCustomerNumber = 1;
    long long nextAdminNumber = 1;
};

struct StoredState {
    std::vector<AccountRecord> accounts;
    std::vector<Transaction> transactions;
    std::vector<long long> reversedIds;
    std::vector<QueueToken> queue;
    std::vector<Customer> customers;
    std::vector<AdminUser> admins;
    Counters counters;
};

class FileStorage {
public:
    explicit FileStorage(std::string directory);

    // Finishes or discards an interrupted save (see CRASH SAFETY above). Call once at start-up,
    // BEFORE load(). Safe to call when nothing is pending.
    void recover() const;

    // Missing/empty files -> empty state. Malformed content -> BankError(DataCorrupt).
    // Never modifies any file.
    StoredState load() const;

    // Writes every file to "<name>.tmp", checks success, then renames over the
    // real file. Throws BankError(StorageFailure) on any problem.
    void save(const StoredState& state) const;

    const std::string& directory() const noexcept { return directory_; }

private:
    std::string directory_;
};
