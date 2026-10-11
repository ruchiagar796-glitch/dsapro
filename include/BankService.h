#pragma once
// BankService: the layer between HTTP and Bank. It validates request data, calls Bank, and
// returns a ServiceResult {HTTP status, JSON body}. It has no dependency on the HTTP library.
//
// AUTHORIZATION DESIGN (important for the viva):
//   * Customer methods take the customer id as a C++ argument. That id comes from the
//     server-side SESSION, never from the JSON body or the URL. A customer therefore has no way
//     to name somebody else's account: "my account" is looked up from "who am I".
//   * The only place a customer supplies another account number is the transfer recipient, and
//     that is treated purely as a destination to credit.
//   * Admin methods are only reachable through routes that ApiServer guards with the Admin role.
//
// CONCURRENCY: one mutex serialises every call, so each banking operation is atomic with respect
// to all others (like a database "serializable" transaction). Simple and provably correct for
// this project's scale.
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "Bank.h"

struct ServiceResult {
    int status = 200;
    nlohmann::json body;
};

using Params = std::map<std::string, std::string>;  // query-string parameters

// What the storage layer looks like, reported on the admin dashboard.
struct StorageInfo {
    bool known = false;       // false when the host could not tell us
    bool persistent = false;  // true when the data folder is on a mounted disk/volume
};

class BankService {
public:
    explicit BankService(Bank& bank) : bank_(bank) {}

    // ---- identity support for AuthService ----
    struct Credential { std::string subjectId; std::string passwordHash; };
    std::optional<Credential> customerCredential(const std::string& email);
    std::optional<Credential> adminCredential(const std::string& username);
    bool customerExists(const std::string& customerId);
    bool adminExists(const std::string& adminId);
    std::string customerName(const std::string& customerId);
    std::string adminName(const std::string& adminId);
    // Both may throw BankError (Conflict for a duplicate e-mail).
    ServiceResult registerCustomer(const std::string& fullName, const std::string& email,
                                   const std::string& phone, const std::string& passwordHash);
    bool ensureAdmin(const std::string& username, const std::string& passwordHash);  // true if created
    bool hasAdmin();
    void changeCustomerPasswordHash(const std::string& customerId, const std::string& hash);
    void changeAdminPasswordHash(const std::string& adminId, const std::string& hash);
    std::string customerPasswordHash(const std::string& customerId);
    std::string adminPasswordHash(const std::string& adminId);

    // ---- customer operations (customerId comes from the session) ----
    ServiceResult customerOverview(const std::string& customerId);
    ServiceResult customerAccount(const std::string& customerId);
    ServiceResult customerProfile(const std::string& customerId);
    ServiceResult updateProfile(const std::string& customerId, const nlohmann::json& body);
    ServiceResult deposit(const std::string& customerId, const nlohmann::json& body);
    ServiceResult withdraw(const std::string& customerId, const nlohmann::json& body);
    ServiceResult transfer(const std::string& customerId, const nlohmann::json& body);
    ServiceResult lookupRecipient(const std::string& customerId, const std::string& accountNumberText);
    ServiceResult customerTransactions(const std::string& customerId, const Params& query);

    // ---- administrator operations ----
    ServiceResult adminOverview(const StorageInfo& storage);
    ServiceResult adminCustomers(const Params& query);
    ServiceResult adminCustomerDetail(const std::string& customerId);
    ServiceResult adminAccounts(const Params& query);
    ServiceResult adminAccountDetail(const std::string& accountNumberText);
    ServiceResult setAccountStatus(const std::string& accountNumberText, const nlohmann::json& body);
    ServiceResult adminTransactions(const Params& query);
    ServiceResult nextReversible();
    ServiceResult reverse(const nlohmann::json& body);
    ServiceResult queue();
    ServiceResult issueToken(const nlohmann::json& body);
    ServiceResult serveNext();

    static ServiceResult error(int status, const std::string& code, const std::string& message);

private:
    template <typename F>
    ServiceResult run(F&& action);

    struct Replay {
        ServiceResult result;
        std::string fingerprint;
        std::time_t at = 0;
    };
    // Idempotency: a repeated form submission with the same key returns the first result
    // instead of moving the money a second time. Call only while holding the mutex.
    std::optional<ServiceResult> replayIfDuplicate(const std::string& customerId, const nlohmann::json& body,
                                                   const std::string& fingerprint);
    void rememberResult(const std::string& customerId, const nlohmann::json& body,
                        const std::string& fingerprint, const ServiceResult& result);

    Bank& bank_;
    std::mutex mutex_;
    std::map<std::string, Replay> replays_;
};
