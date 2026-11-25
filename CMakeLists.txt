cmake_minimum_required(VERSION 3.20)

project(VAULTX_Banking_Simulation
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "/Users/aditya/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/poppler")
endif()

find_package(OpenSSL REQUIRED)

# Discover libpqxx + libpq cleanly across macOS (~/.local/pg) and Linux (Debian/Ubuntu)
if(APPLE AND EXISTS "$ENV{HOME}/.local/pg")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/pg")
endif()

find_package(libpqxx QUIET CONFIG)

if(TARGET libpqxx::pqxx)
    set(VAULTX_PG_LIBS libpqxx::pqxx OpenSSL::SSL OpenSSL::Crypto)
    set(VAULTX_PG_INCLUDE_DIRS "")
    set(VAULTX_PG_LINK_DIRS "")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBPQXX QUIET libpqxx)
    endif()

    if(LIBPQXX_FOUND)
        set(VAULTX_PG_INCLUDE_DIRS ${LIBPQXX_INCLUDE_DIRS})
        set(VAULTX_PG_LINK_DIRS ${LIBPQXX_LIBRARY_DIRS})
        set(VAULTX_PG_LIBS ${LIBPQXX_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
    else()
        find_path(LIBPQXX_INCLUDE_DIR pqxx/pqxx REQUIRED)
        find_path(LIBPQ_INCLUDE_DIR libpq-fe.h REQUIRED)
        find_library(LIBPQXX_LIBRARY pqxx REQUIRED)
        find_library(LIBPQ_LIBRARY pq REQUIRED)

        set(VAULTX_PG_INCLUDE_DIRS ${LIBPQXX_INCLUDE_DIR} ${LIBPQ_INCLUDE_DIR})
        set(VAULTX_PG_LINK_DIRS "")
        set(VAULTX_PG_LIBS ${LIBPQXX_LIBRARY} ${LIBPQ_LIBRARY} OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

add_subdirectory(backend)
add_subdirectory(tests)
