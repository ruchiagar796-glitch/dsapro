#include "BankService.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>

#include "SortingAlgorithms.h"
#include "Validation.h"

using nlohmann::json;
using money::Paise;

namespace {

// ------------------------------------------------------------------ small helpers
ServiceResult ok(const std::string& message, json data, int status = 200) {
    ServiceResult r;
    r.status = status;
    r.body = {{"success", true}, {"message", message}, {"data", std::move(data)}};
    return r;
}

[[noreturn]] void bad(const std::string& message) { throw BankError(ErrorCode::InvalidInput, message); }

long long parseAccountNumber(const std::string& text, const char* label = "Account number") {
    if (text.empty() || text.size() > 15) bad(std::string(label) + " must be a positive whole number");
    long long v = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) bad(std::string(label) + " must be a positive whole number");
        v = v * 10 + (c - '0');
    }
    if (v <= 0) bad(std::string(label) + " must be a positive whole number");
    return v;
}

void requireObject(const json& body) {
    if (!body.is_object()) bad("Request body must be a JSON object");
}

long long accountField(const json& body, const char* key) {
    requireObject(body);
    if (!body.contains(key) || body[key].is_null()) bad(std::string("Field '") + key + "' is required");
    const json& v = body[key];
    if (v.is_number_unsigned() || v.is_number_integer()) return parseAccountNumber(v.dump(), key);
    if (v.is_string()) return parseAccountNumber(v.get<std::string>(), key);
    bad(std::string("Field '") + key + "' must be a whole number");
}

std::string stringField(const json& body, const char* key, bool required) {
    requireObject(body);
    if (!body.contains(key) || body[key].is_null()) {
        if (required) bad(std::string("Field '") + key + "' is required");
        return "";
    }
    if (!body[key].is_string()) bad(std::string("Field '") + key + "' must be text");
    return body[key].get<std::string>();
}

Paise amountField(const json& body, const char* key) {
    requireObject(body);
    if (!body.contains(key) || body[key].is_null()) bad(std::string("Field '") + key + "' is required");
    const json& v = body[key];
    std::string text;
    if (v.is_string()) text = v.get<std::string>();
    else if (v.is_number()) text = v.dump();  // e.g. 100 or 100.5 ; huge values dump as 1e+30 and are rejected
    else bad(std::string("Field '") + key + "' must be a number");
    Paise out = 0;
    std::string err;
    if (!money::parseRupees(text, out, err)) bad(err);
    return out;
}

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsNoCase(const std::string& haystack, const std::string& lowerNeedle) {
    return lowerCase(haystack).find(lowerNeedle) != std::string::npos;
}

std::string isoUtc(std::time_t t) {
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) return "";
    return buf;
}

// "Ruchi Agarwal" -> "R**** A******" : enough for a sender to confirm the recipient without
// revealing the whole name of a stranger.
std::string maskName(const std::string& name) {
    std::string out;
    bool atWordStart = true;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == ' ') { out += ' '; atWordStart = true; continue; }
        if (c >= 0x80 && c < 0xC0) continue;  // stray UTF-8 continuation byte
        if (atWordStart) out += static_cast<char>(c);
        else out += '*';
        atWordStart = false;
        if (c >= 0xC0) {  // skip (or copy, for the first letter) the rest of a multi-byte character
            std::size_t extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
            const bool first = out.back() == static_cast<char>(c);
            while (extra-- && i + 1 < name.size()) {
                ++i;
                if (first) out += name[i];
            }
        }
    }
    return out;
}

std::string maskAccount(long long n) {
    const std::string s = std::to_string(n);
    if (s.size() <= 4) return s;
    return std::string(s.size() - 4, 'X') + s.substr(s.size() - 4);
}

// Query-string helpers --------------------------------------------------------------
std::string param(const Params& p, const char* key) {
    const auto it = p.find(key);
    return it == p.end() ? "" : it->second;
}

long long optionalInt(const Params& p, const char* key, long long fallback, long long lo, long long hi) {
    const std::string t = param(p, key);
    if (t.empty()) return fallback;
    if (t.size() > 15) bad(std::string("Parameter '") + key + "' is not valid");
    long long v = 0;
    for (char c : t) {
        if (!std::isdigit(static_cast<unsigned char>(c))) bad(std::string("Parameter '") + key + "' must be a whole number");
        v = v * 10 + (c - '0');
    }
    if (v < lo || v > hi) bad(std::string("Parameter '") + key + "' is out of range");
    return v;
}

// A filter shared by the customer and admin transaction lists.
struct TxFilter {
    std::string type = "all";   // all | deposit | withdrawal | transfer | reversal
    long long from = -1;        // epoch seconds, inclusive (-1 = none)
    long long to = -1;
    long long account = 0;      // admin only: restrict to one account
    std::string search;         // admin only: reference / name / account fragment
    std::size_t limit = 20;
    std::size_t offset = 0;
};

TxFilter parseFilter(const Params& q, bool admin) {
    TxFilter f;
    const std::string t = param(q, "type");
    if (!t.empty()) {
        if (t != "all" && t != "deposit" && t != "withdrawal" && t != "transfer" && t != "reversal")
            bad("Type must be all, deposit, withdrawal, transfer or reversal");
        f.type = t;
    }
    f.from = optionalInt(q, "from", -1, 0, 253402300799LL);
    f.to = optionalInt(q, "to", -1, 0, 253402300799LL);
    if (f.from >= 0 && f.to >= 0 && f.from > f.to) bad("The start date must not be after the end date");
    f.limit = static_cast<std::size_t>(optionalInt(q, "limit", 20, 1, 100));
    f.offset = static_cast<std::size_t>(optionalInt(q, "offset", 0, 0, 10000000));
    if (admin) {
        f.account = optionalInt(q, "account", 0, 0, 999999999999999LL);
        f.search = lowerCase(param(q, "search"));
        if (f.search.size() > 60) bad("Search text is too long");
    }
    return f;
}

bool matchesType(const Transaction& t, const std::string& type) {
    if (type == "all") return true;
    if (type == "deposit") return t.type() == TransactionType::Deposit;
    if (type == "withdrawal") return t.type() == TransactionType::Withdrawal;
    if (type == "transfer") return t.type() == TransactionType::TransferIn || t.type() == TransactionType::TransferOut;
    return t.type() == TransactionType::Reversal;
}

// Is this transaction money coming IN (credit) or going OUT (debit) for its account?
bool isCredit(const Bank& bank, const Transaction& t) {
    switch (t.type()) {
        case TransactionType::Deposit:
        case TransactionType::TransferIn: return true;
        case TransactionType::Withdrawal:
        case TransactionType::TransferOut: return false;
        default: {  // a reversal undoes the original: reversing a deposit takes money out
            const Transaction* original = bank.findTransaction(t.relatedId());
            return original && original->type() == TransactionType::Withdrawal;
        }
    }
}

const char* typeLabel(TransactionType t) {
    switch (t) {
        case TransactionType::Deposit: return "Deposit";
        case TransactionType::Withdrawal: return "Withdrawal";
        case TransactionType::TransferOut: return "Transfer sent";
        case TransactionType::TransferIn: return "Transfer received";
        default: return "Reversal";
    }
}

json txJson(const Bank& bank, const Transaction& t) {
    return {{"id", t.id()},
            {"reference", t.reference()},
            {"type", toString(t.type())},
            {"label", typeLabel(t.type())},
            {"direction", isCredit(bank, t) ? "credit" : "debit"},
            {"amount", money::format(t.amount())},
            {"amountPaise", t.amount()},
            {"balanceAfter", money::format(t.balanceAfter())},
            {"timestamp", static_cast<long long>(t.timestamp())},
            {"timestampIso", isoUtc(t.timestamp())},
            {"description", t.description()},
            {"status", bank.isReversed(t.id()) ? "Reversed" : "Completed"}};
}

// Admin view of a transaction also names the account and its holder.
json adminTxJson(const Bank& bank, const Transaction& t) {
    json j = txJson(bank, t);
    j["accountNumber"] = t.accountNumber();
    const BankAccount* a = bank.findAccount(t.accountNumber());
    j["holderName"] = a ? json(a->holderName()) : json(nullptr);
    const Customer* c = bank.users().findCustomerByAccount(t.accountNumber());
    j["customerId"] = c ? json(c->id) : json(nullptr);
    return j;
}

json tokenJson(const QueueToken& q) {
    return {{"token", q.number}, {"customerName", q.customerName}, {"purpose", q.purpose},
            {"issuedAt", static_cast<long long>(q.issuedAt)}, {"issuedAtIso", isoUtc(q.issuedAt)}};
}

struct Totals {
    Paise deposits = 0, withdrawals = 0, transfersIn = 0, transfersOut = 0;
    std::size_t depositCount = 0, withdrawalCount = 0;
};

// Sums only transactions that have not been reversed (a reversed deposit no longer counts).
void addToTotals(const Bank& bank, const Transaction& t, Totals& s) {
    if (bank.isReversed(t.id())) return;
    switch (t.type()) {
        case TransactionType::Deposit: s.deposits += t.amount(); ++s.depositCount; break;
        case TransactionType::Withdrawal: s.withdrawals += t.amount(); ++s.withdrawalCount; break;
        case TransactionType::TransferIn: s.transfersIn += t.amount(); break;
        case TransactionType::TransferOut: s.transfersOut += t.amount(); break;
        default: break;
    }
}

json totalsJson(const Totals& s) {
    return {{"totalDeposits", money::format(s.deposits)},
            {"totalWithdrawals", money::format(s.withdrawals)},
            {"totalTransfersIn", money::format(s.transfersIn)},
            {"totalTransfersOut", money::format(s.transfersOut)},
            {"depositCount", s.depositCount},
            {"withdrawalCount", s.withdrawalCount}};
}

// What the OWNER sees about their own account.
json ownAccountJson(const BankAccount& a) {
    return {{"accountNumber", a.accountNumber()},
            {"accountNumberMasked", maskAccount(a.accountNumber())},
            {"holderName", a.holderName()},
            {"accountType", a.typeName()},
            {"balance", money::format(a.balance())},
            {"balancePaise", a.balance()},
            {"status", a.active() ? "active" : "deactivated"},
            {"openedAt", static_cast<long long>(a.createdAt())},
            {"maxWithdrawalPerTransaction", money::format(a.maxWithdrawalPerTransaction())},
            {"maxTransactionAmount", money::format(money::kMaxTransactionPaise)}};
}

// Customer profile: NEVER includes the password hash.
json profileJson(const Customer& c) {
    return {{"customerId", c.id},
            {"fullName", c.fullName},
            {"email", c.email},
            {"phone", c.phone},
            {"memberSince", static_cast<long long>(c.createdAt)}};
}

// Applies a filter to a list that is already newest-first and returns one page.
template <typename ToJson>
json paginate(const Bank& bank, const std::vector<Transaction>& all, const TxFilter& f, ToJson toJson) {
    std::vector<const Transaction*> hits;
    for (const Transaction& t : all) {
        if (!matchesType(t, f.type)) continue;
        if (f.from >= 0 && static_cast<long long>(t.timestamp()) < f.from) continue;
        if (f.to >= 0 && static_cast<long long>(t.timestamp()) > f.to) continue;
        if (f.account != 0 && t.accountNumber() != f.account) continue;
        if (!f.search.empty()) {
            const BankAccount* a = bank.findAccount(t.accountNumber());
            const bool hit = containsNoCase(t.reference(), f.search) ||
                             std::to_string(t.accountNumber()).find(f.search) != std::string::npos ||
                             (a && containsNoCase(a->holderName(), f.search));
            if (!hit) continue;
        }
        hits.push_back(&t);
    }
    json items = json::array();
    for (std::size_t i = f.offset; i < hits.size() && items.size() < f.limit; ++i) items.push_back(toJson(bank, *hits[i]));
    return {{"items", items}, {"total", hits.size()}, {"limit", f.limit}, {"offset", f.offset}};
}

bool validIdempotencyKey(const std::string& k) {
    if (k.size() < 8 || k.size() > 64) return false;
    for (char c : k)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;
}

// Finds "me". The id comes from the session; if the customer no longer exists the session is
// treated as invalid (HTTP 401).
const Customer& me(const Bank& bank, const std::string& customerId) {
    const Customer* c = bank.users().findCustomerById(customerId);
    if (!c) throw BankError(ErrorCode::Unauthorized, "Your session is no longer valid. Please sign in again.");
    return *c;
}
const BankAccount& myAccount(const Bank& bank, const Customer& c) {
    const BankAccount* a = bank.findAccount(c.accountNumber);
    if (!a) throw BankError(ErrorCode::Internal, "Customer account is missing");
    return *a;
}

json adminCustomerRow(const Bank& bank, const Customer& c) {
    const BankAccount* a = bank.findAccount(c.accountNumber);
    return {{"customerId", c.id},
            {"fullName", c.fullName},
            {"email", c.email},
            {"phone", c.phone},
            {"accountNumber", c.accountNumber},
            {"balance", a ? money::format(a->balance()) : "0.00"},
            {"balancePaise", a ? a->balance() : 0},
            {"status", a && !a->active() ? "deactivated" : "active"},
            {"memberSince", static_cast<long long>(c.createdAt)}};
}
json adminAccountRow(const Bank& bank, const BankAccount& a) {
    const Customer* c = bank.users().findCustomerByAccount(a.accountNumber());
    return {{"accountNumber", a.accountNumber()},
            {"holderName", a.holderName()},
            {"accountType", a.typeName()},
            {"balance", money::format(a.balance())},
            {"balancePaise", a.balance()},
            {"status", a.active() ? "active" : "deactivated"},
            {"openedAt", static_cast<long long>(a.createdAt())},
            {"customerId", c ? json(c->id) : json(nullptr)},
            {"email", c ? json(c->email) : json(nullptr)}};
}
std::string statusFilter(const Params& q) {
    const std::string s = param(q, "status");
    if (!s.empty() && s != "all" && s != "active" && s != "deactivated") bad("Status must be all, active or deactivated");
    return s.empty() ? "all" : s;
}

}  // namespace

// ============================================================== plumbing
ServiceResult BankService::error(int status, const std::string& code, const std::string& message) {
    ServiceResult r;
    r.status = status;
    r.body = {{"success", false}, {"error", {{"code", code}, {"message", message}}}};
    return r;
}

template <typename F>
ServiceResult BankService::run(F&& action) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return action();
    } catch (const BankError& e) {
        // Storage / internal problems get a generic text so file names and internals are never
        // leaked to the browser.
        if (e.code() == ErrorCode::StorageFailure || e.code() == ErrorCode::DataCorrupt || e.code() == ErrorCode::Internal) {
            std::cerr << "[bank] " << errorCodeName(e.code()) << ": " << e.what() << "\n";  // server log only
            return error(500, errorCodeName(e.code()), "The server could not complete this request. Please try again later.");
        }
        return error(httpStatusFor(e.code()), errorCodeName(e.code()), e.what());
    } catch (const std::exception&) {
        return error(500, "INTERNAL_ERROR", "Unexpected server error");
    }
}

std::optional<ServiceResult> BankService::replayIfDuplicate(const std::string& customerId, const json& body,
                                                            const std::string& fingerprint) {
    if (!body.is_object() || !body.contains("idempotencyKey") || body["idempotencyKey"].is_null()) return std::nullopt;
    if (!body["idempotencyKey"].is_string() || !validIdempotencyKey(body["idempotencyKey"].get<std::string>()))
        bad("The request key is not valid. Please reload the page and try again");
    const auto it = replays_.find(customerId + "|" + body["idempotencyKey"].get<std::string>());
    if (it == replays_.end()) return std::nullopt;
    if (it->second.fingerprint != fingerprint)
        throw BankError(ErrorCode::Conflict, "This request key was already used for a different request");
    ServiceResult again = it->second.result;
    again.body["replayed"] = true;
    return again;
}

void BankService::rememberResult(const std::string& customerId, const json& body, const std::string& fingerprint,
                                 const ServiceResult& result) {
    if (!body.is_object() || !body.contains("idempotencyKey") || !body["idempotencyKey"].is_string()) return;
    const std::time_t now = std::time(nullptr);
    if (replays_.size() > 5000)  // forget entries older than 15 minutes
        for (auto it = replays_.begin(); it != replays_.end();) {
            if (now - it->second.at > 900) it = replays_.erase(it);
            else ++it;
        }
    replays_[customerId + "|" + body["idempotencyKey"].get<std::string>()] = {result, fingerprint, now};
}

// ============================================================== identity support
std::optional<BankService::Credential> BankService::customerCredential(const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Customer* c = bank_.users().findCustomerByEmail(email);
    if (!c) return std::nullopt;
    return Credential{c->id, c->passwordHash};
}
std::optional<BankService::Credential> BankService::adminCredential(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    const AdminUser* a = bank_.users().findAdminByUsername(username);
    if (!a) return std::nullopt;
    return Credential{a->id, a->passwordHash};
}
bool BankService::customerExists(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return bank_.users().findCustomerById(id) != nullptr;
}
bool BankService::adminExists(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return bank_.users().findAdminById(id) != nullptr;
}
std::string BankService::customerName(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Customer* c = bank_.users().findCustomerById(id);
    return c ? c->fullName : "";
}
std::string BankService::adminName(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const AdminUser* a = bank_.users().findAdminById(id);
    return a ? a->username : "";
}
bool BankService::hasAdmin() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bank_.users().hasAdmin();
}
std::string BankService::customerPasswordHash(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Customer* c = bank_.users().findCustomerById(id);
    return c ? c->passwordHash : "";
}
std::string BankService::adminPasswordHash(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const AdminUser* a = bank_.users().findAdminById(id);
    return a ? a->passwordHash : "";
}
void BankService::changeCustomerPasswordHash(const std::string& id, const std::string& hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    bank_.setCustomerPasswordHash(id, hash);
}
void BankService::changeAdminPasswordHash(const std::string& id, const std::string& hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    bank_.setAdminPasswordHash(id, hash);
}
bool BankService::ensureAdmin(const std::string& username, const std::string& hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bank_.users().findAdminByUsername(username)) return false;
    bank_.createAdmin(username, hash);
    return true;
}

ServiceResult BankService::registerCustomer(const std::string& fullName, const std::string& email,
                                            const std::string& phone, const std::string& passwordHash) {
    return run([&] {
        const Customer& c = bank_.registerCustomer(fullName, email, phone, passwordHash);
        return ok("Your account has been created. You can now sign in.",
                  {{"customerId", c.id},
                   {"accountNumber", c.accountNumber},
                   {"fullName", c.fullName},
                   {"email", c.email}},
                  201);
    });
}

// ============================================================== customer operations
ServiceResult BankService::customerOverview(const std::string& customerId) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const BankAccount& a = myAccount(bank_, c);
        const std::vector<Transaction> mine = bank_.transactionsFor(a.accountNumber());
        Totals totals;
        for (const Transaction& t : mine) addToTotals(bank_, t, totals);
        json recent = json::array();
        for (std::size_t i = 0; i < mine.size() && i < 5; ++i) recent.push_back(txJson(bank_, mine[i]));
        json data = {{"profile", profileJson(c)}, {"account", ownAccountJson(a)},
                     {"summary", totalsJson(totals)}, {"transactionCount", mine.size()},
                     {"recentTransactions", recent}};
        return ok("Overview loaded", data);
    });
}

ServiceResult BankService::customerAccount(const std::string& customerId) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const BankAccount& a = myAccount(bank_, c);
        const std::vector<Transaction> mine = bank_.transactionsFor(a.accountNumber());
        Totals totals;
        for (const Transaction& t : mine) addToTotals(bank_, t, totals);
        json data = ownAccountJson(a);
        data["summary"] = totalsJson(totals);
        data["transactionCount"] = mine.size();
        return ok("Account loaded", data);
    });
}

ServiceResult BankService::customerProfile(const std::string& customerId) {
    return run([&] { return ok("Profile loaded", profileJson(me(bank_, customerId))); });
}

ServiceResult BankService::updateProfile(const std::string& customerId, const json& body) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        requireObject(body);
        // Only these two fields can be edited. Anything else in the body (role, email, accountNumber,
        // customerId ...) is ignored on purpose.
        const std::string name = validation::fullName(stringField(body, "fullName", true));
        const std::string phone = validation::phone(stringField(body, "phone", true));
        bank_.updateCustomerProfile(c.id, name, phone);
        return ok("Profile updated", profileJson(me(bank_, customerId)));
    });
}

ServiceResult BankService::deposit(const std::string& customerId, const json& body) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const Paise amount = amountField(body, "amount");
        const std::string fp = "deposit|" + std::to_string(amount);
        if (auto again = replayIfDuplicate(c.id, body, fp)) return *again;
        const Transaction t = bank_.deposit(c.accountNumber, amount);
        ServiceResult r = ok("Deposited Rs " + money::format(amount) + " successfully",
                             {{"transaction", txJson(bank_, t)}, {"account", ownAccountJson(myAccount(bank_, c))}}, 201);
        rememberResult(c.id, body, fp, r);
        return r;
    });
}

ServiceResult BankService::withdraw(const std::string& customerId, const json& body) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const Paise amount = amountField(body, "amount");
        const std::string fp = "withdraw|" + std::to_string(amount);
        if (auto again = replayIfDuplicate(c.id, body, fp)) return *again;
        const Transaction t = bank_.withdraw(c.accountNumber, amount);
        ServiceResult r = ok("Withdrew Rs " + money::format(amount) + " successfully",
                             {{"transaction", txJson(bank_, t)}, {"account", ownAccountJson(myAccount(bank_, c))}}, 201);
        rememberResult(c.id, body, fp, r);
        return r;
    });
}

ServiceResult BankService::transfer(const std::string& customerId, const json& body) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const long long to = accountField(body, "toAccount");
        const Paise amount = amountField(body, "amount");
        const std::string fp = "transfer|" + std::to_string(to) + "|" + std::to_string(amount);
        if (auto again = replayIfDuplicate(c.id, body, fp)) return *again;
        if (to == c.accountNumber) bad("You cannot transfer money to your own account");
        if (!bank_.findAccount(to)) throw BankError(ErrorCode::NotFound, "Recipient account not found. Check the account number and try again");
        // Bank::transfer validates BOTH accounts before changing either one, then saves all files
        // in one atomic commit (see FileStorage). The sender is always the session's account.
        const auto legs = bank_.transfer(c.accountNumber, to, amount);
        ServiceResult r = ok("Transferred Rs " + money::format(amount) + " successfully",
                             {{"transaction", txJson(bank_, legs.first)},
                              {"reference", legs.first.reference()},
                              {"account", ownAccountJson(myAccount(bank_, c))}}, 201);
        rememberResult(c.id, body, fp, r);
        return r;
    });
}

ServiceResult BankService::lookupRecipient(const std::string& customerId, const std::string& text) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const long long n = parseAccountNumber(text, "Recipient account number");
        if (n == c.accountNumber) bad("That is your own account. Choose a different recipient");
        const BankAccount* a = bank_.findAccount(n);
        if (!a) throw BankError(ErrorCode::NotFound, "Recipient account not found");
        if (!a->active()) throw BankError(ErrorCode::Conflict, "The recipient account cannot receive transfers right now");
        // Only a masked name: confirms the right person without exposing a stranger's details.
        return ok("Recipient found", {{"accountNumber", a->accountNumber()}, {"maskedName", maskName(a->holderName())}});
    });
}

ServiceResult BankService::customerTransactions(const std::string& customerId, const Params& query) {
    return run([&] {
        const Customer& c = me(bank_, customerId);
        const TxFilter f = parseFilter(query, /*admin=*/false);
        // transactionsFor(account) returns ONLY this account's records, newest first.
        return ok("Transactions loaded", paginate(bank_, bank_.transactionsFor(c.accountNumber), f, txJson));
    });
}

// ============================================================== administrator operations
ServiceResult BankService::adminOverview(const StorageInfo& storage) {
    return run([&] {
        Totals totals;
        for (const Transaction& t : bank_.allTransactions()) addToTotals(bank_, t, totals);
        std::size_t active = 0, inactive = 0;
        for (const BankAccount* a : bank_.allAccounts()) {
            if (a->active()) ++active;
            else ++inactive;
        }
        const Bank::Summary s = bank_.summary();
        json recent = json::array();
        for (const Transaction& t : bank_.recentTransactions(8)) recent.push_back(adminTxJson(bank_, t));
        json data = {{"totalCustomers", bank_.users().customerCount()},
                     {"totalAccounts", s.accountCount},
                     {"activeAccounts", active},
                     {"deactivatedAccounts", inactive},
                     {"totalDeposits", money::format(totals.deposits)},
                     {"totalWithdrawals", money::format(totals.withdrawals)},
                     {"totalTransactions", s.transactionCount},
                     {"totalBalance", money::format(s.totalBalance)},
                     {"customersWaiting", s.customersWaiting},
                     {"recentTransactions", recent},
                     {"storage", {{"known", storage.known}, {"persistent", storage.persistent}}}};
        return ok("Overview loaded", data);
    });
}

ServiceResult BankService::adminCustomers(const Params& query) {
    return run([&] {
        const std::string search = lowerCase(param(query, "search"));
        if (search.size() > 60) bad("Search text is too long");
        const std::string status = statusFilter(query);
        const std::string sortBy = param(query, "sort").empty() ? "created" : param(query, "sort");
        const std::string order = param(query, "order").empty() ? "asc" : param(query, "order");
        if (sortBy != "name" && sortBy != "balance" && sortBy != "created") bad("Sort must be name, balance or created");
        if (order != "asc" && order != "desc") bad("Order must be asc or desc");
        const bool asc = order == "asc";

        std::vector<const Customer*> rows;
        for (const Customer* c : bank_.users().customers()) {
            const BankAccount* a = bank_.findAccount(c->accountNumber);
            const bool active = a ? a->active() : true;
            if (status == "active" && !active) continue;
            if (status == "deactivated" && active) continue;
            if (!search.empty()) {
                const bool hit = containsNoCase(c->fullName, search) || containsNoCase(c->email, search) ||
                                 containsNoCase(c->phone, search) || containsNoCase(c->id, search) ||
                                 std::to_string(c->accountNumber).find(search) != std::string::npos;
                if (!hit) continue;
            }
            rows.push_back(c);
        }
        // Merge sort (hand-written in SortingAlgorithms.h): stable, O(n log n).
        sorting::mergeSort(rows, [&](const Customer* x, const Customer* y) {
            int cmp = 0;
            if (sortBy == "name") {
                const std::string a = lowerCase(x->fullName), b = lowerCase(y->fullName);
                cmp = a < b ? -1 : (a > b ? 1 : 0);
            } else if (sortBy == "balance") {
                const Paise a = bank_.findAccount(x->accountNumber)->balance(), b = bank_.findAccount(y->accountNumber)->balance();
                cmp = a < b ? -1 : (a > b ? 1 : 0);
            } else {
                cmp = x->createdAt < y->createdAt ? -1 : (x->createdAt > y->createdAt ? 1 : 0);
            }
            if (cmp == 0) return x->id < y->id;
            return asc ? cmp < 0 : cmp > 0;
        });
        json items = json::array();
        for (const Customer* c : rows) items.push_back(adminCustomerRow(bank_, *c));
        return ok("Customers loaded", {{"items", items}, {"total", rows.size()}});
    });
}

ServiceResult BankService::adminCustomerDetail(const std::string& customerId) {
    return run([&] {
        const Customer* c = bank_.users().findCustomerById(customerId);
        if (!c) throw BankError(ErrorCode::NotFound, "Customer not found");
        const BankAccount& a = myAccount(bank_, *c);
        const std::vector<Transaction> txs = bank_.transactionsFor(a.accountNumber());
        Totals totals;
        for (const Transaction& t : txs) addToTotals(bank_, t, totals);
        json recent = json::array();
        for (std::size_t i = 0; i < txs.size() && i < 15; ++i) recent.push_back(adminTxJson(bank_, txs[i]));
        // The password hash is deliberately absent: administrators can never see credentials.
        json data = {{"customer", {{"customerId", c->id}, {"fullName", c->fullName}, {"email", c->email},
                                   {"phone", c->phone}, {"memberSince", static_cast<long long>(c->createdAt)}}},
                     {"account", adminAccountRow(bank_, a)},
                     {"summary", totalsJson(totals)},
                     {"transactionCount", txs.size()},
                     {"recentTransactions", recent}};
        return ok("Customer loaded", data);
    });
}

ServiceResult BankService::adminAccounts(const Params& query) {
    return run([&] {
        const std::string search = lowerCase(param(query, "search"));
        if (search.size() > 60) bad("Search text is too long");
        const std::string status = statusFilter(query);
        const std::string sortBy = param(query, "sort");
        if (!sortBy.empty() && sortBy != "number" && sortBy != "name" && sortBy != "balance") bad("Sort must be number, name or balance");
        const std::string order = param(query, "order");
        if (!order.empty() && order != "asc" && order != "desc") bad("Order must be asc or desc");
        std::vector<const BankAccount*> list =
            (sortBy == "name") ? bank_.sortedAccounts(SortKey::Name, order != "desc")
            : (sortBy == "balance") ? bank_.sortedAccounts(SortKey::Balance, order != "desc")
                                    : bank_.allAccounts();
        if ((sortBy.empty() || sortBy == "number") && order == "desc") std::reverse(list.begin(), list.end());
        json items = json::array();
        for (const BankAccount* a : list) {
            if (status == "active" && !a->active()) continue;
            if (status == "deactivated" && a->active()) continue;
            if (!search.empty() && !(std::to_string(a->accountNumber()).find(search) != std::string::npos ||
                                     containsNoCase(a->holderName(), search)))
                continue;
            items.push_back(adminAccountRow(bank_, *a));
        }
        return ok("Accounts loaded", {{"items", items}, {"total", items.size()}});
    });
}

ServiceResult BankService::adminAccountDetail(const std::string& text) {
    return run([&] {
        const long long n = parseAccountNumber(text);
        const BankAccount* a = bank_.findAccount(n);
        if (!a) throw BankError(ErrorCode::NotFound, "Account " + std::to_string(n) + " not found");
        const std::vector<Transaction> txs = bank_.transactionsFor(n);
        Totals totals;
        for (const Transaction& t : txs) addToTotals(bank_, t, totals);
        json recent = json::array();
        for (std::size_t i = 0; i < txs.size() && i < 15; ++i) recent.push_back(adminTxJson(bank_, txs[i]));
        return ok("Account loaded", {{"account", adminAccountRow(bank_, *a)}, {"summary", totalsJson(totals)},
                                     {"transactionCount", txs.size()}, {"recentTransactions", recent}});
    });
}

ServiceResult BankService::setAccountStatus(const std::string& text, const json& body) {
    return run([&] {
        const long long n = parseAccountNumber(text);
        const std::string status = stringField(body, "status", true);
        if (status != "active" && status != "deactivated") bad("Status must be 'active' or 'deactivated'");
        if (!bank_.findAccount(n)) throw BankError(ErrorCode::NotFound, "Account " + std::to_string(n) + " not found");
        bank_.setAccountActive(n, status == "active");
        return ok(std::string("Account ") + std::to_string(n) + (status == "active" ? " activated" : " deactivated"),
                  adminAccountRow(bank_, *bank_.findAccount(n)));
    });
}

ServiceResult BankService::adminTransactions(const Params& query) {
    return run([&] {
        const TxFilter f = parseFilter(query, /*admin=*/true);
        return ok("Transactions loaded", paginate(bank_, bank_.recentTransactions(0), f, adminTxJson));
    });
}

ServiceResult BankService::nextReversible() {
    return run([&] {
        const auto t = bank_.peekReversible();
        return ok(t ? "Most recent reversible transaction" : "Nothing to reverse",
                  {{"transaction", t ? adminTxJson(bank_, *t) : json(nullptr)}, {"stackSize", bank_.reversibleCount()}});
    });
}

ServiceResult BankService::reverse(const json& body) {
    return run([&] {
        long long id = 0;
        if (body.is_object() && body.contains("transactionId") && !body["transactionId"].is_null())
            id = accountField(body, "transactionId");
        else if (!body.is_object() && !body.is_null()) bad("Request body must be a JSON object");
        const Transaction t = bank_.reverseLastTransaction(id);
        return ok("Transaction #" + std::to_string(t.relatedId()) + " reversed",
                  {{"reversal", adminTxJson(bank_, t)}}, 201);
    });
}

ServiceResult BankService::queue() {
    return run([&] {
        json arr = json::array();
        for (const QueueToken& q : bank_.queueSnapshot()) arr.push_back(tokenJson(q));
        return ok("Queue loaded", {{"waiting", arr}, {"count", arr.size()}, {"nextTokenNumber", bank_.nextTokenNumber()}});
    });
}

ServiceResult BankService::issueToken(const json& body) {
    return run([&] {
        const std::string name = stringField(body, "customerName", true);
        const std::string purpose = stringField(body, "purpose", false);
        const QueueToken t = bank_.issueToken(name, purpose);
        return ok("Token " + std::to_string(t.number) + " issued", tokenJson(t), 201);
    });
}

ServiceResult BankService::serveNext() {
    return run([&] {
        const QueueToken t = bank_.serveNextCustomer();
        return ok("Now serving token " + std::to_string(t.number), tokenJson(t));
    });
}
