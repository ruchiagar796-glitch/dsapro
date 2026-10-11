#pragma once
// UserDirectory: the in-memory "address book" of customers and administrators.
//
// DSA note: three std::unordered_map hash tables give O(1) average lookups
//   customer id -> Customer        (the primary table)
//   e-mail      -> customer id     (login + duplicate-email check)
//   account no. -> customer id     (ownership checks, "who owns this account?")
// A plain vector remembers the registration order so lists come out in a stable order.
// Hash tables are the right tool here: we always look people up by an exact key.
//
// The directory contains only DATA. Password HASHES are stored (never passwords).
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

struct Customer {
    std::string id;            // "CUS000001"
    std::string fullName;
    std::string email;         // always lower-case
    std::string phone;
    std::string passwordHash;  // Argon2id string produced by security::hashPassword
    long long accountNumber = 0;
    std::time_t createdAt = 0;
};

struct AdminUser {
    std::string id;            // "ADM0001"
    std::string username;      // always lower-case
    std::string passwordHash;
    std::time_t createdAt = 0;
};

class UserDirectory {
public:
    // ---- customers ----
    // Throws BankError(Conflict) if the id, e-mail or account number is already used.
    void addCustomer(const Customer& c);
    const Customer* findCustomerById(const std::string& id) const;
    const Customer* findCustomerByEmail(const std::string& email) const;
    const Customer* findCustomerByAccount(long long accountNumber) const;
    Customer* mutableCustomer(const std::string& id);
    std::vector<const Customer*> customers() const;  // registration order
    std::size_t customerCount() const noexcept { return order_.size(); }

    // ---- administrators ----
    void addAdmin(const AdminUser& a);               // Conflict on duplicate id/username
    const AdminUser* findAdminByUsername(const std::string& username) const;
    const AdminUser* findAdminById(const std::string& id) const;
    AdminUser* mutableAdmin(const std::string& id);
    std::vector<const AdminUser*> admins() const;
    bool hasAdmin() const noexcept { return !adminOrder_.empty(); }

    void clear();

    // Id generation ("CUS" + 6 digits, "ADM" + 4 digits). Counters never go backwards.
    long long nextCustomerNumber = 1;
    long long nextAdminNumber = 1;
    std::string makeCustomerId(long long n) const;
    std::string makeAdminId(long long n) const;

private:
    std::unordered_map<std::string, Customer> customersById_;
    std::unordered_map<std::string, std::string> emailToId_;
    std::unordered_map<long long, std::string> accountToId_;
    std::vector<std::string> order_;

    std::unordered_map<std::string, AdminUser> adminsById_;
    std::unordered_map<std::string, std::string> usernameToId_;
    std::vector<std::string> adminOrder_;
};
