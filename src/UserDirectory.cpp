#include "UserDirectory.h"

#include "BankError.h"

namespace {
std::string padded(const char* prefix, long long n, std::size_t width) {
    std::string d = std::to_string(n);
    if (d.size() < width) d.insert(0, width - d.size(), '0');
    return std::string(prefix) + d;
}
}  // namespace

std::string UserDirectory::makeCustomerId(long long n) const { return padded("CUS", n, 6); }
std::string UserDirectory::makeAdminId(long long n) const { return padded("ADM", n, 4); }

void UserDirectory::clear() {
    customersById_.clear();
    emailToId_.clear();
    accountToId_.clear();
    order_.clear();
    adminsById_.clear();
    usernameToId_.clear();
    adminOrder_.clear();
}

void UserDirectory::addCustomer(const Customer& c) {
    // Check ALL uniqueness rules before inserting anything, so a failure changes nothing.
    if (customersById_.count(c.id)) throw BankError(ErrorCode::Conflict, "Duplicate customer id " + c.id);
    if (emailToId_.count(c.email)) throw BankError(ErrorCode::Conflict, "An account with this email already exists");
    if (accountToId_.count(c.accountNumber))
        throw BankError(ErrorCode::Conflict, "Account number is already linked to a customer");
    customersById_[c.id] = c;
    emailToId_[c.email] = c.id;
    accountToId_[c.accountNumber] = c.id;
    order_.push_back(c.id);
}

const Customer* UserDirectory::findCustomerById(const std::string& id) const {
    const auto it = customersById_.find(id);
    return it == customersById_.end() ? nullptr : &it->second;
}
const Customer* UserDirectory::findCustomerByEmail(const std::string& email) const {
    const auto it = emailToId_.find(email);
    return it == emailToId_.end() ? nullptr : findCustomerById(it->second);
}
const Customer* UserDirectory::findCustomerByAccount(long long n) const {
    const auto it = accountToId_.find(n);
    return it == accountToId_.end() ? nullptr : findCustomerById(it->second);
}
Customer* UserDirectory::mutableCustomer(const std::string& id) {
    const auto it = customersById_.find(id);
    return it == customersById_.end() ? nullptr : &it->second;
}
std::vector<const Customer*> UserDirectory::customers() const {
    std::vector<const Customer*> v;
    v.reserve(order_.size());
    for (const std::string& id : order_) v.push_back(&customersById_.at(id));
    return v;
}

void UserDirectory::addAdmin(const AdminUser& a) {
    if (adminsById_.count(a.id)) throw BankError(ErrorCode::Conflict, "Duplicate admin id " + a.id);
    if (usernameToId_.count(a.username)) throw BankError(ErrorCode::Conflict, "Admin username already exists");
    adminsById_[a.id] = a;
    usernameToId_[a.username] = a.id;
    adminOrder_.push_back(a.id);
}
const AdminUser* UserDirectory::findAdminByUsername(const std::string& u) const {
    const auto it = usernameToId_.find(u);
    return it == usernameToId_.end() ? nullptr : findAdminById(it->second);
}
const AdminUser* UserDirectory::findAdminById(const std::string& id) const {
    const auto it = adminsById_.find(id);
    return it == adminsById_.end() ? nullptr : &it->second;
}
AdminUser* UserDirectory::mutableAdmin(const std::string& id) {
    const auto it = adminsById_.find(id);
    return it == adminsById_.end() ? nullptr : &it->second;
}
std::vector<const AdminUser*> UserDirectory::admins() const {
    std::vector<const AdminUser*> v;
    for (const std::string& id : adminOrder_) v.push_back(&adminsById_.at(id));
    return v;
}
