// Entry point: reads the configuration, loads the bank from the data folder, creates the first
// administrator if requested, and starts the HTTP server.
//
// Usage:  bank_server [--port 8080] [--data data] [--frontend frontend]
//         bank_server --create-admin <username>      (password from BMS_ADMIN_PASSWORD or stdin)
//
// Environment variables (all optional unless noted):
//   PORT                     port to listen on (Render sets this)                  default 8080
//   BMS_HOST                 address to listen on                                  default 127.0.0.1
//   BMS_DATA_DIR             folder for the data files (mount a persistent disk here)  default data
//   BMS_ADMIN_USERNAME       first administrator's user name                       default admin
//   BMS_ADMIN_PASSWORD       first administrator's password (min 10 chars, SECRET)
//   BMS_TRUST_PROXY          1 when running behind a proxy such as Render's
//   BMS_COOKIE_SECURE        1 to force the Secure cookie flag (HTTPS only)
//   BMS_FIRST_ACCOUNT_NUMBER first account number of a brand-new bank              default 1000000001
//   BMS_STORAGE_PERSISTENT   true/false to override automatic disk detection
//   BMS_IN_CONTAINER         set to 1 by the Dockerfile (used for the disk check above)
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "ApiServer.h"
#include "AuthService.h"
#include "Bank.h"
#include "BankService.h"
#include "Security.h"
#include "SessionManager.h"

namespace fs = std::filesystem;

namespace {

ApiServer* g_server = nullptr;
void onSignal(int) { if (g_server) g_server->stop(); }

std::string env(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}
bool envFlag(const char* name) {
    const std::string v = env(name);
    return v == "1" || v == "true" || v == "TRUE" || v == "yes";
}

// Is the data folder on a mounted disk (survives restarts) or on the container's temporary
// file system (erased on every restart/redeploy)? A separate mounted volume has a different
// device id than "/". The Dockerfile sets BMS_IN_CONTAINER=1, so inside our container "same
// device as /" means "temporary". Outside a container (your own PC) ordinary disks persist.
StorageInfo detectStorage(const std::string& dataDir) {
    StorageInfo info;
    const std::string forced = env("BMS_STORAGE_PERSISTENT");
    if (!forced.empty()) { info.known = true; info.persistent = (forced == "1" || forced == "true"); return info; }
#ifndef _WIN32
    struct stat dirStat{}, rootStat{};
    if (::stat(dataDir.c_str(), &dirStat) == 0 && ::stat("/", &rootStat) == 0) {
        info.known = true;
        if (dirStat.st_dev != rootStat.st_dev) info.persistent = true;     // a separate mounted volume
        else info.persistent = !envFlag("BMS_IN_CONTAINER");               // container root = temporary
    }
#endif
    return info;
}

#ifndef _WIN32
// Only ONE server may use a data folder at a time (two writers would corrupt it).
bool lockDataDirectory(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const int fd = ::open((fs::path(dir) / ".lock").c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) return true;  // cannot create the lock file: let the normal storage errors speak
    return ::flock(fd, LOCK_EX | LOCK_NB) == 0;  // fd stays open (and locked) until the process ends
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try { security::init(); }
    catch (const std::exception& e) { std::cerr << "Cannot start: " << e.what() << "\n"; return 2; }

    std::string dataDir = env("BMS_DATA_DIR", "data");
    std::string frontendDir = "frontend";
    const std::string host = env("BMS_HOST", "127.0.0.1");
    int port = 8080;
    try { port = std::stoi(env("PORT", "8080")); } catch (...) { port = 0; }
    if (port < 1 || port > 65535) { std::cerr << "Invalid PORT value\n"; return 2; }
    long long firstAccount = 1000000001LL;
    try { firstAccount = std::stoll(env("BMS_FIRST_ACCOUNT_NUMBER", "1000000001")); } catch (...) { firstAccount = 0; }
    if (firstAccount < 1) { std::cerr << "Invalid BMS_FIRST_ACCOUNT_NUMBER\n"; return 2; }

    std::string createAdminUser;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& target) {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << a << "\n"; std::exit(2); }
            target = argv[++i];
        };
        if (a == "--port") {
            std::string p;
            value(p);
            try { port = std::stoi(p); } catch (...) { port = 0; }
            if (port < 1 || port > 65535) { std::cerr << "Invalid port\n"; return 2; }
        } else if (a == "--data") value(dataDir);
        else if (a == "--frontend") value(frontendDir);
        else if (a == "--create-admin") value(createAdminUser);
        else {
            std::cerr << "Unknown option " << a << "\nUsage: bank_server [--port N] [--data DIR] [--frontend DIR] [--create-admin USER]\n";
            return 2;
        }
    }
    // Convenience: allow running from inside the build folder.
    if (!fs::exists(frontendDir) && fs::exists("../frontend")) frontendDir = "../frontend";

#ifndef _WIN32
    if (!lockDataDirectory(dataDir)) {
        std::cerr << "Another bank_server is already using the data folder '" << dataDir << "'. Stop it first.\n";
        return 1;
    }
#endif

    try {
        Bank bank(dataDir, firstAccount);
        BankService service(bank);
        SessionManager sessions;
        AuthService auth(service, sessions);

        // ---- first administrator ----
        // No default account exists. The password comes from an environment variable (kept secret
        // by the host) or is typed on stdin; it is hashed immediately and never printed or saved.
        if (!createAdminUser.empty()) {
            std::string password = env("BMS_ADMIN_PASSWORD");
            if (password.empty()) {
                std::cerr << "Password for new administrator '" << createAdminUser << "' (min 10 characters): ";
                std::getline(std::cin, password);
            }
            try {
                std::cout << (auth.bootstrapAdmin(createAdminUser, password) ? "Administrator created.\n"
                                                                              : "That administrator already exists; nothing changed.\n");
            } catch (const BankError& e) { std::cerr << "Not created: " << e.what() << "\n"; return 1; }
            return 0;
        }
        const std::string adminPassword = env("BMS_ADMIN_PASSWORD");
        if (!adminPassword.empty()) {
            try {
                if (auth.bootstrapAdmin(env("BMS_ADMIN_USERNAME", "admin"), adminPassword))
                    std::cout << "First administrator '" << env("BMS_ADMIN_USERNAME", "admin") << "' created from BMS_ADMIN_PASSWORD.\n";
            } catch (const BankError& e) {
                std::cerr << "BMS_ADMIN_USERNAME / BMS_ADMIN_PASSWORD rejected: " << e.what() << "\n";
            }
        }
        if (!env("BMS_PASSWORD").empty() || !env("BMS_USER").empty())
            std::cerr << "Note: BMS_USER / BMS_PASSWORD (old site-wide password) are no longer used. "
                         "Use BMS_ADMIN_USERNAME / BMS_ADMIN_PASSWORD instead.\n";
        if (!service.hasAdmin())
            std::cerr << "WARNING: no administrator exists, so nobody can sign in to the admin portal. "
                         "Set BMS_ADMIN_PASSWORD or run: bank_server --create-admin <username>\n";

        ApiConfig cfg;
        cfg.frontendDir = frontendDir;
        cfg.trustProxy = envFlag("BMS_TRUST_PROXY");
        cfg.secureCookies = envFlag("BMS_COOKIE_SECURE");
        cfg.storage = detectStorage(dataDir);
        ApiServer server(service, auth, sessions, cfg);
        g_server = &server;
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        std::cout << "BankPro - Bank Management System (educational simulation)\n"
                  << "  data folder    : " << fs::absolute(dataDir).string() << "\n"
                  << "  storage        : " << (!cfg.storage.known ? "unknown" : (cfg.storage.persistent ? "persistent disk" : "TEMPORARY (data is lost when the service restarts)")) << "\n"
                  << "  frontend folder: " << fs::absolute(frontendDir).string() << "\n"
                  << "  listening on   : " << host << ":" << port << "\n"
                  << "  open in browser: http://localhost:" << port << "\n"
                  << "Press Ctrl+C to stop.\n";
        if (cfg.storage.known && !cfg.storage.persistent)
            std::cerr << "WARNING: the data folder is on temporary storage. Attach a persistent disk and point BMS_DATA_DIR at it (see DEPLOY.md).\n";
        if (!server.listen(host, port)) {
            std::cerr << "Could not listen on port " << port << " (is it already in use? try --port 8081)\n";
            return 1;
        }
    } catch (const BankError& e) {
        std::cerr << "Startup failed: " << e.what() << "\n"
                  << "No data was modified. Fix or restore the data files and try again.\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
