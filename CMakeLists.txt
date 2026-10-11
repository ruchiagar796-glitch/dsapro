cmake_minimum_required(VERSION 3.16)
project(BankManagementSystem VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Dependencies: two header-only libraries downloaded once into third_party/.
# SHA-256 hashes make sure the downloaded files are exactly the expected ones.
# ---------------------------------------------------------------------------
set(DEP_DIR "${CMAKE_SOURCE_DIR}/third_party")
file(MAKE_DIRECTORY "${DEP_DIR}/nlohmann")

function(bms_fetch url dest hash)
  if(NOT EXISTS "${dest}")
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${dest}" EXPECTED_HASH SHA256=${hash} STATUS dl_status SHOW_PROGRESS)
    list(GET dl_status 0 dl_code)
    if(NOT dl_code EQUAL 0)
      file(REMOVE "${dest}")
      message(FATAL_ERROR "Could not download ${url}\nDownload it manually and save it as ${dest} (see README, section 'Dependencies').")
    endif()
  endif()
endfunction()

bms_fetch("https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h"
          "${DEP_DIR}/httplib.h"
          "a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5")
bms_fetch("https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
          "${DEP_DIR}/nlohmann/json.hpp"
          "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6")

# ---------------------------------------------------------------------------
# libsodium (Argon2id password hashing + secure random numbers).
#   Debian/Ubuntu/Docker: apt install libsodium-dev      macOS: brew install libsodium
#   Windows: vcpkg install libsodium  (then pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake)
# The STATIC library is preferred so the final program needs no extra runtime package.
# ---------------------------------------------------------------------------
find_path(SODIUM_INCLUDE_DIR sodium.h)
find_library(SODIUM_LIBRARY NAMES libsodium.a sodium libsodium)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
  find_package(unofficial-sodium CONFIG QUIET)
endif()
if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
  message(STATUS "libsodium: ${SODIUM_LIBRARY}")
elseif(TARGET unofficial-sodium::sodium)
  message(STATUS "libsodium: vcpkg target")
else()
  message(FATAL_ERROR "libsodium not found. Install it (Debian/Ubuntu: sudo apt install libsodium-dev; macOS: brew install libsodium; Windows: vcpkg install libsodium). See README.")
endif()

# ---------------------------------------------------------------------------
# Core library: all business logic, data structures and file handling.
# No HTTP code in here, so the tests can use it directly.
# ---------------------------------------------------------------------------
add_library(bank_core STATIC
  src/Money.cpp
  src/BankAccount.cpp
  src/Transaction.cpp
  src/AccountLinkedList.cpp
  src/TransactionStack.cpp
  src/CustomerServiceQueue.cpp
  src/TransactionHistory.cpp
  src/FileStorage.cpp
  src/Bank.cpp
  src/UserDirectory.cpp
  src/Validation.cpp
  src/Security.cpp
  src/SessionManager.cpp
  src/RateLimiter.cpp
  src/BankService.cpp
  src/AuthService.cpp
  src/ApiServer.cpp)
target_include_directories(bank_core PUBLIC include "${DEP_DIR}")
if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
  target_include_directories(bank_core PUBLIC "${SODIUM_INCLUDE_DIR}")
  target_link_libraries(bank_core PUBLIC "${SODIUM_LIBRARY}")
  if(SODIUM_LIBRARY MATCHES "\\.a$")
    target_compile_definitions(bank_core PUBLIC SODIUM_STATIC=1)
  endif()
else()
  target_link_libraries(bank_core PUBLIC unofficial-sodium::sodium)
endif()
find_package(Threads REQUIRED)
target_link_libraries(bank_core PUBLIC Threads::Threads)

# HTTP server executable
add_executable(bank_server src/main.cpp)
target_link_libraries(bank_server PRIVATE bank_core)

# Test executable
add_executable(bank_tests
  tests/test_main.cpp
  tests/test_accounts.cpp
  tests/test_transactions.cpp
  tests/test_file_storage.cpp
  tests/test_data_structures.cpp
  tests/test_service.cpp
  tests/test_security.cpp
  tests/test_users.cpp
  tests/test_authorization.cpp
  tests/test_http.cpp)
target_link_libraries(bank_tests PRIVATE bank_core)
# The HTTP tests serve the real frontend files from this folder.
target_compile_definitions(bank_tests PRIVATE FRONTEND_DIR="${CMAKE_SOURCE_DIR}/frontend")

enable_testing()
add_test(NAME bank_tests COMMAND bank_tests)

foreach(target bank_core bank_server bank_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

if(WIN32)
  # cpp-httplib needs Windows 7+ APIs and the Winsock library.
  target_compile_definitions(bank_core PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(bank_core PUBLIC ws2_32)
endif()
