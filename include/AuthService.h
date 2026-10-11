#pragma once
// AuthService: registration, login, logout and password changes.
//
// Security rules implemented here:
//  * passwords are only ever handled as Argon2id hashes (Security.h); they are never logged,
//    never stored and never returned
//  * the error for "unknown e-mail" and "wrong password" is identical, and a decoy hash makes
//    the response time identical too (no user enumeration through login)
//  * brute-force protection with sliding-window limiters (RateLimiter.h) per ip+account,
//    per account and per ip
//  * a successful login creates a NEW random server-side session (SessionManager.h)
//  * changing a password signs out every other session of that person
#include <string>

#include <nlohmann/json.hpp>

#include "BankService.h"
#include "RateLimiter.h"
#include "SessionManager.h"

struct AuthResult {
    ServiceResult result;
    std::string sessionId;  // non-empty only after a successful login (ApiServer puts it in a cookie)
};

class AuthService {
public:
    AuthService(BankService& bank, SessionManager& sessions);

    AuthResult registerCustomer(const nlohmann::json& body, const std::string& clientIp);
    AuthResult loginCustomer(const nlohmann::json& body, const std::string& clientIp);
    AuthResult loginAdmin(const nlohmann::json& body, const std::string& clientIp);
    ServiceResult sessionInfo(const Session& session);
    // Customers and admins both use this; the role decides which password is changed.
    ServiceResult changePassword(const Session& session, const nlohmann::json& body);

    // First-administrator set-up. Creates the admin only if that username does not exist yet.
    // Returns true if a new admin was created; throws BankError(InvalidInput) for weak input.
    bool bootstrapAdmin(const std::string& username, const std::string& password);

    static constexpr std::size_t kAdminMinPassword = 10;

private:
    AuthResult login(Role role, const std::string& identifier, const std::string& password,
                     const std::string& clientIp);

    BankService& bank_;
    SessionManager& sessions_;
    RateLimiter loginPerIpAndId_;   // 5 failures / 15 min
    RateLimiter loginPerId_;        // 30 failures / 15 min (caps distributed guessing)
    RateLimiter loginPerIp_;        // 40 failures / 15 min
    RateLimiter registerPerIp_;     // 10 attempts / hour
    RateLimiter passwordChange_;    // 5 failures / 15 min per person
};
