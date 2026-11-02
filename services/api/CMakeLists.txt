cmake_minimum_required(VERSION 3.16)
project(adcontrol_api VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
# res_query() lives in libresolv on glibc platforms; on newer glibc it may be
# provided by libc directly. Link it when present so DNS TXT verification works.
find_library(RESOLV_LIBRARY resolv)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

# Common library sources
set(COMMON_SOURCES
    src/db.cpp
    src/billing_db.cpp
    src/conversion_db.cpp
    src/automation_db.cpp
    src/automation_engine.cpp
    src/crm_db.cpp
    src/seo_db.cpp
    src/experiments_db.cpp
    src/learning_db.cpp
    src/handlers.cpp
    src/feature_handlers.cpp
    src/smtp.cpp
)

# Main Executable
add_executable(adcontrol-api
    src/main.cpp
    ${COMMON_SOURCES}
)

target_compile_definitions(adcontrol-api PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(adcontrol-api PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
)
if(RESOLV_LIBRARY)
    target_link_libraries(adcontrol-api PRIVATE ${RESOLV_LIBRARY})
endif()

# Tests Executable
add_executable(adcontrol-tests
    tests/test_main.cpp
    src/db.cpp
    src/billing_db.cpp
    src/conversion_db.cpp
    src/automation_db.cpp
    src/automation_engine.cpp
    src/crm_db.cpp
    src/seo_db.cpp
    src/experiments_db.cpp
    src/learning_db.cpp
    src/smtp.cpp
)

target_compile_definitions(adcontrol-tests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(adcontrol-tests PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
)

# The test suite uses assert(); Release builds define NDEBUG, which would make
# every assertion a no-op. Force assertions on for the test target only.
if(MSVC)
    target_compile_options(adcontrol-tests PRIVATE /UNDEBUG)
else()
    target_compile_options(adcontrol-tests PRIVATE -UNDEBUG)
endif()

enable_testing()
add_test(NAME adcontrol_tests COMMAND adcontrol-tests)
