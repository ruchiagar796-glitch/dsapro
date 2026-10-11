#include "FileStorage.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

const char* kAccountsFile = "accounts.txt";
const char* kTransactionsFile = "transactions.txt";
const char* kMetadataFile = "metadata.txt";
const char* kReversedFile = "reversed_transactions.txt";
const char* kQueueFile = "customer_queue.txt";
const char* kCustomersFile = "customers.txt";
const char* kAdminsFile = "admins.txt";
const char* kCommitFile = "COMMIT";

[[noreturn]] void corrupt(const std::string& where, const std::string& what) {
    throw BankError(ErrorCode::DataCorrupt, where + ": " + what);
}

std::string escapeField(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '\\') r += "\\\\";
        else if (c == '|') r += "\\|";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else r += c;
    }
    return r;
}

std::vector<std::string> splitLine(const std::string& line, const std::string& where) {
    std::vector<std::string> fields(1);
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\') {
            if (++i >= line.size()) corrupt(where, "dangling escape character");
            const char n = line[i];
            if (n == '\\' || n == '|') fields.back() += n;
            else if (n == 'n') fields.back() += '\n';
            else if (n == 'r') fields.back() += '\r';
            else corrupt(where, "unknown escape sequence");
        } else if (c == '|') {
            fields.emplace_back();
        } else {
            fields.back() += c;
        }
    }
    return fields;
}

long long toInt(const std::string& s, const std::string& where, const char* field) {
    long long v = 0;
    const char* b = s.data();
    const char* e = b + s.size();
    const auto r = std::from_chars(b, e, v);
    if (s.empty() || r.ec != std::errc() || r.ptr != e)
        corrupt(where, std::string("field '") + field + "' is not a valid integer");
    return v;
}

struct Line { std::size_t number; std::string text; };

std::vector<Line> readLines(const fs::path& path) {
    std::vector<Line> out;
    std::error_code ec;
    if (!fs::exists(path, ec)) return out;  // missing file = empty data
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BankError(ErrorCode::StorageFailure, "Cannot open " + path.string() + " for reading");
    std::string text;
    std::size_t n = 0;
    while (std::getline(in, text)) {
        ++n;
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (text.empty() || text[0] == '#') continue;
        out.push_back({n, text});
    }
    if (in.bad()) throw BankError(ErrorCode::StorageFailure, "Error while reading " + path.string());
    return out;
}

std::string where(const char* file, const Line& l) {
    return std::string(file) + " line " + std::to_string(l.number);
}

// Writes the file and forces it to the disk (fsync) so that a power cut cannot leave an
// empty or half-written file behind a successful rename.
void writeTemp(const fs::path& tmp, const std::string& content) {
#ifndef _WIN32
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) throw BankError(ErrorCode::StorageFailure, "Cannot open " + tmp.string() + " for writing");
    std::size_t done = 0;
    while (done < content.size()) {
        const ssize_t n = ::write(fd, content.data() + done, content.size() - done);
        if (n < 0) {
            ::close(fd);
            throw BankError(ErrorCode::StorageFailure, "Failed to write " + tmp.string());
        }
        done += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        throw BankError(ErrorCode::StorageFailure, "Failed to flush " + tmp.string());
    }
    if (::close(fd) != 0) throw BankError(ErrorCode::StorageFailure, "Failed to close " + tmp.string());
#else
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw BankError(ErrorCode::StorageFailure, "Cannot open " + tmp.string() + " for writing");
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out) throw BankError(ErrorCode::StorageFailure, "Failed to write " + tmp.string());
    out.close();
    if (out.fail()) throw BankError(ErrorCode::StorageFailure, "Failed to close " + tmp.string());
#endif
}

// Makes renames/creations inside the directory durable (no-op on Windows).
void syncDirectory(const fs::path& dir) {
#ifndef _WIN32
    const int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
#else
    (void)dir;
#endif
}

const char* const kAllFiles[] = {"transactions.txt", "reversed_transactions.txt", "customer_queue.txt",
                                 "customers.txt", "admins.txt", "accounts.txt", "metadata.txt"};

void replaceFile(const fs::path& tmp, const fs::path& target) {
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (!ec) return;
    fs::remove(target, ec);  // some platforms refuse to rename over an existing file
    ec.clear();
    fs::rename(tmp, target, ec);
    if (ec) throw BankError(ErrorCode::StorageFailure, "Cannot replace " + target.string() + ": " + ec.message());
}

}  // namespace

FileStorage::FileStorage(std::string directory) : directory_(std::move(directory)) {}

void FileStorage::recover() const {
    const fs::path dir(directory_);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    const fs::path marker = dir / kCommitFile;
    if (fs::exists(marker, ec)) {
        // The previous save reached the point of no return: finish it (roll forward).
        for (const char* name : kAllFiles) {
            const fs::path tmp = dir / (std::string(name) + ".tmp");
            if (fs::exists(tmp, ec)) replaceFile(tmp, dir / name);
        }
        fs::remove(marker, ec);
        syncDirectory(dir);
    } else {
        // The previous save never committed: its temp files are incomplete, drop them.
        for (const char* name : kAllFiles) {
            fs::remove(dir / (std::string(name) + ".tmp"), ec);
            ec.clear();
        }
    }
}

StoredState FileStorage::load() const {
    StoredState st;
    const fs::path dir(directory_);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw BankError(ErrorCode::StorageFailure, "Cannot create data directory: " + ec.message());

    for (const Line& l : readLines(dir / kAccountsFile)) {
        const std::string w = where(kAccountsFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 5 && f.size() != 6) corrupt(w, "expected 5 or 6 fields");
        AccountRecord r;
        r.accountNumber = toInt(f[0], w, "accountNumber");
        r.holderName = f[1];
        if (!parseAccountType(f[2], r.type)) corrupt(w, "unknown account type");
        r.balance = toInt(f[3], w, "balance");
        r.createdAt = static_cast<std::time_t>(toInt(f[4], w, "createdAt"));
        if (f.size() == 6) {
            if (f[5] == "active") r.active = true;
            else if (f[5] == "deactivated") r.active = false;
            else corrupt(w, "unknown account status");
        }
        if (r.accountNumber <= 0) corrupt(w, "account number must be positive");
        if (r.balance < 0 || r.balance > money::kMaxBalancePaise) corrupt(w, "balance out of range");
        if (r.createdAt < 0) corrupt(w, "invalid creation time");
        st.accounts.push_back(std::move(r));
    }

    for (const Line& l : readLines(dir / kTransactionsFile)) {
        const std::string w = where(kTransactionsFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 8) corrupt(w, "expected 8 fields");
        TransactionType type;
        if (!parseTransactionType(f[2], type)) corrupt(w, "unknown transaction type");
        const long long id = toInt(f[0], w, "id");
        const long long acc = toInt(f[1], w, "accountNumber");
        const long long amount = toInt(f[3], w, "amount");
        const long long ts = toInt(f[4], w, "timestamp");
        const long long after = toInt(f[5], w, "balanceAfter");
        const long long related = toInt(f[6], w, "relatedId");
        if (id <= 0 || acc <= 0) corrupt(w, "id and account number must be positive");
        if (amount <= 0 || amount > money::kMaxTransactionPaise) corrupt(w, "amount out of range");
        if (after < 0 || after > money::kMaxBalancePaise) corrupt(w, "balanceAfter out of range");
        if (ts < 0 || related < 0) corrupt(w, "negative timestamp or related id");
        st.transactions.emplace_back(id, acc, type, amount, static_cast<std::time_t>(ts), after, f[7], related);
    }

    for (const Line& l : readLines(dir / kCustomersFile)) {
        const std::string w = where(kCustomersFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 7) corrupt(w, "expected 7 fields");
        Customer c;
        c.id = f[0];
        c.fullName = f[1];
        c.email = f[2];
        c.phone = f[3];
        c.passwordHash = f[4];
        c.accountNumber = toInt(f[5], w, "accountNumber");
        c.createdAt = static_cast<std::time_t>(toInt(f[6], w, "createdAt"));
        if (c.id.empty() || c.fullName.empty() || c.email.find('@') == std::string::npos ||
            c.passwordHash.empty() || c.accountNumber <= 0 || c.createdAt < 0)
            corrupt(w, "invalid customer record");
        st.customers.push_back(std::move(c));
    }

    for (const Line& l : readLines(dir / kAdminsFile)) {
        const std::string w = where(kAdminsFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 4) corrupt(w, "expected 4 fields");
        AdminUser a;
        a.id = f[0];
        a.username = f[1];
        a.passwordHash = f[2];
        a.createdAt = static_cast<std::time_t>(toInt(f[3], w, "createdAt"));
        if (a.id.empty() || a.username.empty() || a.passwordHash.empty() || a.createdAt < 0)
            corrupt(w, "invalid admin record");
        st.admins.push_back(std::move(a));
    }

    for (const Line& l : readLines(dir / kReversedFile)) {
        const std::string w = where(kReversedFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 1) corrupt(w, "expected 1 field");
        const long long id = toInt(f[0], w, "transactionId");
        if (id <= 0) corrupt(w, "transaction id must be positive");
        st.reversedIds.push_back(id);
    }

    for (const Line& l : readLines(dir / kQueueFile)) {
        const std::string w = where(kQueueFile, l);
        const auto f = splitLine(l.text, w);
        if (f.size() != 4) corrupt(w, "expected 4 fields");
        QueueToken t;
        t.number = toInt(f[0], w, "token");
        t.customerName = f[1];
        t.purpose = f[2];
        t.issuedAt = static_cast<std::time_t>(toInt(f[3], w, "issuedAt"));
        if (t.number <= 0 || t.issuedAt < 0 || t.customerName.empty()) corrupt(w, "invalid token record");
        st.queue.push_back(std::move(t));
    }

    for (const Line& l : readLines(dir / kMetadataFile)) {
        const std::string w = where(kMetadataFile, l);
        const auto eq = l.text.find('=');
        if (eq == std::string::npos) corrupt(w, "expected key=value");
        const std::string key = l.text.substr(0, eq);
        const long long v = toInt(l.text.substr(eq + 1), w, key.c_str());
        if (v <= 0) corrupt(w, "counter must be positive");
        if (key == "next_account_number") st.counters.nextAccountNumber = v;
        else if (key == "next_transaction_id") st.counters.nextTransactionId = v;
        else if (key == "next_token_number") st.counters.nextTokenNumber = v;
        else if (key == "next_customer_number") st.counters.nextCustomerNumber = v;
        else if (key == "next_admin_number") st.counters.nextAdminNumber = v;
        // unknown keys are ignored so newer files do not break older builds
    }
    return st;
}

void FileStorage::save(const StoredState& st) const {
    const fs::path dir(directory_);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw BankError(ErrorCode::StorageFailure, "Cannot create data directory: " + ec.message());

    std::ostringstream accounts, transactions, reversed, queue, meta, customers, admins;
    accounts << "# accountNumber|holderName|type|balancePaise|createdAt(epoch)|status\n";
    for (const auto& a : st.accounts)
        accounts << a.accountNumber << '|' << escapeField(a.holderName) << '|' << toString(a.type)
                 << '|' << a.balance << '|' << static_cast<long long>(a.createdAt) << '|'
                 << (a.active ? "active" : "deactivated") << '\n';

    customers << "# customerId|fullName|email|phone|passwordHash(Argon2id)|accountNumber|createdAt(epoch)\n";
    for (const auto& c : st.customers)
        customers << escapeField(c.id) << '|' << escapeField(c.fullName) << '|' << escapeField(c.email) << '|'
                  << escapeField(c.phone) << '|' << escapeField(c.passwordHash) << '|' << c.accountNumber << '|'
                  << static_cast<long long>(c.createdAt) << '\n';

    admins << "# adminId|username|passwordHash(Argon2id)|createdAt(epoch)\n";
    for (const auto& a : st.admins)
        admins << escapeField(a.id) << '|' << escapeField(a.username) << '|' << escapeField(a.passwordHash) << '|'
               << static_cast<long long>(a.createdAt) << '\n';

    transactions << "# id|accountNumber|type|amountPaise|timestamp(epoch)|balanceAfterPaise|relatedId|description\n";
    for (const auto& t : st.transactions)
        transactions << t.id() << '|' << t.accountNumber() << '|' << toString(t.type()) << '|'
                     << t.amount() << '|' << static_cast<long long>(t.timestamp()) << '|'
                     << t.balanceAfter() << '|' << t.relatedId() << '|' << escapeField(t.description()) << '\n';

    reversed << "# transactionId of every transaction that has already been reversed\n";
    for (long long id : st.reversedIds) reversed << id << '\n';

    queue << "# token|customerName|purpose|issuedAt(epoch)\n";
    for (const auto& q : st.queue)
        queue << q.number << '|' << escapeField(q.customerName) << '|' << escapeField(q.purpose) << '|'
              << static_cast<long long>(q.issuedAt) << '\n';

    meta << "# counters that keep IDs unique across restarts\n"
         << "next_account_number=" << st.counters.nextAccountNumber << '\n'
         << "next_transaction_id=" << st.counters.nextTransactionId << '\n'
         << "next_token_number=" << st.counters.nextTokenNumber << '\n'
         << "next_customer_number=" << st.counters.nextCustomerNumber << '\n'
         << "next_admin_number=" << st.counters.nextAdminNumber << '\n';

    const std::vector<std::pair<const char*, std::string>> files = {
        {kTransactionsFile, transactions.str()}, {kReversedFile, reversed.str()},
        {kQueueFile, queue.str()},               {kCustomersFile, customers.str()},
        {kAdminsFile, admins.str()},             {kAccountsFile, accounts.str()},
        {kMetadataFile, meta.str()}};

    // Phase 1: write + fsync every temp file. If anything fails, the real files are untouched.
    std::vector<fs::path> temps;
    try {
        for (const auto& f : files) {
            fs::path tmp = dir / (std::string(f.first) + ".tmp");
            temps.push_back(tmp);
            writeTemp(tmp, f.second);
        }
    } catch (...) {
        for (const auto& t : temps) { std::error_code e; fs::remove(t, e); }
        throw;
    }
    // Phase 2: publish the COMMIT marker atomically. From here on the NEW state wins,
    // even if the process is killed before all renames below are done (see recover()).
    const fs::path markerTmp = dir / (std::string(kCommitFile) + ".tmp");
    try {
        writeTemp(markerTmp, "commit\n");
        replaceFile(markerTmp, dir / kCommitFile);
        syncDirectory(dir);
    } catch (...) {
        for (const auto& t : temps) { std::error_code e; fs::remove(t, e); }
        std::error_code e;
        fs::remove(markerTmp, e);
        throw;
    }
    // Phase 3: replace the real files, then retire the marker.
    for (std::size_t i = 0; i < files.size(); ++i) replaceFile(temps[i], dir / files[i].first);
    std::error_code e;
    fs::remove(dir / kCommitFile, e);
    syncDirectory(dir);
}
