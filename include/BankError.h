#pragma once
// BankError: one exception type used for every "expected" failure
// (bad input, missing account, insufficient funds, storage problems...).
// The ErrorCode lets the API layer pick an HTTP status without parsing text.
#include <stdexcept>
#include <string>

enum class ErrorCode {
    InvalidInput,
    NotFound,
    InsufficientFunds,
    LimitExceeded,
    Conflict,
    StorageFailure,
    DataCorrupt,
    Unauthorized,   // not signed in (HTTP 401)
    Forbidden,      // signed in but not allowed (HTTP 403)
    RateLimited,    // too many attempts (HTTP 429)
    Internal
};

inline const char* errorCodeName(ErrorCode c) {
    switch (c) {
        case ErrorCode::InvalidInput: return "INVALID_INPUT";
        case ErrorCode::NotFound: return "NOT_FOUND";
        case ErrorCode::InsufficientFunds: return "INSUFFICIENT_FUNDS";
        case ErrorCode::LimitExceeded: return "LIMIT_EXCEEDED";
        case ErrorCode::Conflict: return "CONFLICT";
        case ErrorCode::StorageFailure: return "STORAGE_FAILURE";
        case ErrorCode::DataCorrupt: return "DATA_CORRUPT";
        case ErrorCode::Unauthorized: return "UNAUTHENTICATED";
        case ErrorCode::Forbidden: return "FORBIDDEN";
        case ErrorCode::RateLimited: return "RATE_LIMITED";
        default: return "INTERNAL_ERROR";
    }
}

inline int httpStatusFor(ErrorCode c) {
    switch (c) {
        case ErrorCode::InvalidInput: return 400;
        case ErrorCode::NotFound: return 404;
        case ErrorCode::Conflict: return 409;
        case ErrorCode::Unauthorized: return 401;
        case ErrorCode::Forbidden: return 403;
        case ErrorCode::RateLimited: return 429;
        case ErrorCode::InsufficientFunds:
        case ErrorCode::LimitExceeded: return 422;
        default: return 500;
    }
}

class BankError : public std::runtime_error {
public:
    BankError(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};
