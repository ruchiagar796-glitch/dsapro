#include "AuthService.h"

#include <map>

#include "Security.h"
#include "Validation.h"

using nlohmann::json;

namespace {

ServiceResult fail(int status, const std::string& code, const std::string& message) {
    return BankService::error(status, code, message);
}

ServiceResult tooMany(int retryAfterSeconds) {
    ServiceResult r = fail(429, "RATE_LIMITED",
                           "Too many attempts. Please wait " + std::to_string((retryAfterSeconds + 59) / 60) +
                               " minute(s) and try again.");
    r.body["error"]["retryAfterSeconds"] = retryAfterSeconds;
    return r;
}

// Field-by-field validation result (so the form can mark each wrong field).
ServiceResult validationFailed(const std::map<std::string, std::string>& fields) {
    ServiceResult r = fail(400, "VALIDATION_FAILED", "Please correct the highlighted fields.");
    json f = json::object();
    for (const auto& kv : fields) f[kv.first] = kv.second;
    r.body["error"]["fields"] = f;
    return r;
}

std::string textOf(const json& body, const char* key) {
    if (!body.is_object() || !body.contains(key) || !body[key].is_string()) return "";
    return body[key].get<std::string>();
}

std::string lowerTrim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    std::string r = s.substr(b, e - b + 1);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

}  // namespace

AuthService::AuthService(BankService& bank, SessionManager& sessions)
    : bank_(bank), sessions_(sessions), loginPerIpAndId_(5, 15 * 60), loginPerId_(30, 15 * 60),
      loginPerIp_(40, 15 * 60), registerPerIp_(10, 60 * 60), passwordChange_(5, 15 * 60) {}

// ------------------------------------------------------------------ registration
AuthResult AuthService::registerCustomer(const json& body, const std::string& ip) {
    AuthResult out;
    int wait = 0;
    if (registerPerIp_.isBlocked(ip, &wait)) { out.result = tooMany(wait); return out; }
    registerPerIp_.recordFailure(ip);  // every attempt counts, successful or not

    if (!body.is_object()) { out.result = fail(400, "INVALID_INPUT", "Request body must be a JSON object"); return out; }
    std::map<std::string, std::string> errors;
    std::string name, email, phone;
    auto check = [&](const char* field, auto&& fn) {
        try { fn(); } catch (const BankError& e) { errors[field] = e.what(); }
    };
    check("fullName", [&] { name = validation::fullName(textOf(body, "fullName")); });
    check("email", [&] { email = validation::email(textOf(body, "email")); });
    check("phone", [&] { phone = validation::phone(textOf(body, "phone")); });
    const std::string password = textOf(body, "password");
    check("password", [&] { validation::password(password, email); });
    if (!errors.count("password") && password != textOf(body, "confirmPassword"))
        errors["confirmPassword"] = "Passwords do not match";
    if (!errors.empty()) { out.result = validationFailed(errors); return out; }

    try {
        const std::string hash = security::hashPassword(password);   // slow on purpose (Argon2id)
        out.result = bank_.registerCustomer(name, email, phone, hash);
    } catch (const BankError& e) {
        out.result = fail(httpStatusFor(e.code()), errorCodeName(e.code()), e.what());
    }
    if (out.result.status == 409) out.result = validationFailed({{"email", "An account with this email already exists"}});
    return out;
}

// ------------------------------------------------------------------ login
AuthResult AuthService::login(Role role, const std::string& identifier, const std::string& password,
                              const std::string& ip) {
    AuthResult out;
    const std::string tag = role == Role::Admin ? "admin:" : "customer:";
    const std::string keyBoth = ip + "|" + tag + identifier;
    const std::string keyId = tag + identifier;
    const std::string keyIp = "ip:" + ip;

    int wait = 0, w2 = 0;
    bool blocked = loginPerIpAndId_.isBlocked(keyBoth, &wait);
    if (loginPerId_.isBlocked(keyId, &w2)) { blocked = true; wait = std::max(wait, w2); }
    if (loginPerIp_.isBlocked(keyIp, &w2)) { blocked = true; wait = std::max(wait, w2); }
    if (blocked) { out.result = tooMany(wait); return out; }

    // Look the person up, but ALWAYS run a password verification (against a decoy hash if the
    // account does not exist) so that timing does not reveal which e-mails are registered.
    std::optional<BankService::Credential> cred =
        role == Role::Admin ? bank_.adminCredential(identifier) : bank_.customerCredential(identifier);
    const bool passwordOk = password.size() <= 128 &&
                            security::verifyPassword(cred ? cred->passwordHash : security::decoyHash(), password);
    if (!cred || !passwordOk) {
        loginPerIpAndId_.recordFailure(keyBoth);
        loginPerId_.recordFailure(keyId);
        loginPerIp_.recordFailure(keyIp);
        out.result = fail(401, "INVALID_CREDENTIALS",
                          role == Role::Admin ? "Invalid username or password." : "Invalid email or password.");
        return out;
    }
    loginPerIpAndId_.reset(keyBoth);
    loginPerId_.reset(keyId);

    out.sessionId = sessions_.create(role, cred->subjectId);   // brand-new random id on every login
    const auto session = sessions_.validate(out.sessionId);
    const std::string display = role == Role::Admin ? bank_.adminName(cred->subjectId) : bank_.customerName(cred->subjectId);
    out.result.status = 200;
    out.result.body = {{"success", true}, {"message", "Signed in"},
                       {"data", {{"role", role == Role::Admin ? "admin" : "customer"},
                                 {"name", display},
                                 {"csrfToken", session ? session->csrfToken : ""},
                                 {"redirect", role == Role::Admin ? "/admin" : "/dashboard"}}}};
    return out;
}

AuthResult AuthService::loginCustomer(const json& body, const std::string& ip) {
    return login(Role::Customer, lowerTrim(textOf(body, "email")), textOf(body, "password"), ip);
}
AuthResult AuthService::loginAdmin(const json& body, const std::string& ip) {
    return login(Role::Admin, lowerTrim(textOf(body, "username")), textOf(body, "password"), ip);
}

ServiceResult AuthService::sessionInfo(const Session& s) {
    const std::string name = s.role == Role::Admin ? bank_.adminName(s.subjectId) : bank_.customerName(s.subjectId);
    ServiceResult r;
    r.body = {{"success", true}, {"message", "Session active"},
              {"data", {{"authenticated", true}, {"role", s.role == Role::Admin ? "admin" : "customer"},
                        {"name", name}, {"csrfToken", s.csrfToken}}}};
    return r;
}

// ------------------------------------------------------------------ password change
ServiceResult AuthService::changePassword(const Session& s, const json& body) {
    if (!body.is_object()) return fail(400, "INVALID_INPUT", "Request body must be a JSON object");
    const bool admin = s.role == Role::Admin;
    const std::string key = (admin ? "admin:" : "customer:") + s.subjectId;
    int wait = 0;
    if (passwordChange_.isBlocked(key, &wait)) return tooMany(wait);

    const std::string current = textOf(body, "currentPassword");
    const std::string next = textOf(body, "newPassword");
    const std::string current_hash = admin ? bank_.adminPasswordHash(s.subjectId) : bank_.customerPasswordHash(s.subjectId);
    if (current_hash.empty()) return fail(401, "UNAUTHENTICATED", "Please sign in again.");
    if (current.size() > 128 || !security::verifyPassword(current_hash, current)) {
        passwordChange_.recordFailure(key);
        return validationFailed({{"currentPassword", "Your current password is not correct"}});
    }
    std::map<std::string, std::string> errors;
    try {
        std::string identity = admin ? bank_.adminName(s.subjectId) : "";
        if (!admin) {
            const ServiceResult p = bank_.customerProfile(s.subjectId);
            if (p.body.contains("data")) identity = p.body["data"].value("email", "");
        }
        validation::password(next, identity, admin ? AuthService::kAdminMinPassword : 8);
    } catch (const BankError& e) { errors["newPassword"] = e.what(); }
    if (errors.empty() && next == current) errors["newPassword"] = "Choose a password different from the current one";
    if (errors.empty() && next != textOf(body, "confirmPassword")) errors["confirmPassword"] = "Passwords do not match";
    if (!errors.empty()) return validationFailed(errors);

    try {
        const std::string hash = security::hashPassword(next);
        if (admin) bank_.changeAdminPasswordHash(s.subjectId, hash);
        else bank_.changeCustomerPasswordHash(s.subjectId, hash);
    } catch (const BankError& e) {
        return fail(httpStatusFor(e.code()), errorCodeName(e.code()), "The password could not be saved. Please try again.");
    }
    passwordChange_.reset(key);
    sessions_.destroyAllFor(s.role, s.subjectId, s.id);   // every OTHER device is signed out
    ServiceResult r;
    r.body = {{"success", true}, {"message", "Password changed. Other devices have been signed out."}, {"data", json::object()}};
    return r;
}

bool AuthService::bootstrapAdmin(const std::string& rawUsername, const std::string& password) {
    const std::string username = validation::username(rawUsername);
    validation::password(password, username, kAdminMinPassword);
    if (bank_.adminCredential(username)) return false;   // already there: never overwrite
    return bank_.ensureAdmin(username, security::hashPassword(password));
}
