add_executable(atm_backend
    src/main.cpp
    src/database/Database.cpp
    src/security/PinHasher.cpp
    src/security/SessionManager.cpp
    src/security/RateLimiter.cpp
    src/services/AuthService.cpp
)

find_package(OpenSSL REQUIRED)

# Discover libpqxx + libpq cleanly across macOS (~/.local/pg) and Linux (Debian/Ubuntu)
if(APPLE AND EXISTS "$ENV{HOME}/.local/pg")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/pg")
endif()

find_package(libpqxx QUIET CONFIG)

target_include_directories(atm_backend
    PRIVATE
    include
    external/httplib
    external
)

if(TARGET libpqxx::pqxx)
    target_link_libraries(atm_backend PRIVATE libpqxx::pqxx OpenSSL::SSL OpenSSL::Crypto)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBPQXX QUIET libpqxx)
    endif()

    if(LIBPQXX_FOUND)
        target_include_directories(atm_backend PRIVATE ${LIBPQXX_INCLUDE_DIRS})
        target_link_directories(atm_backend PRIVATE ${LIBPQXX_LIBRARY_DIRS})
        target_link_libraries(atm_backend PRIVATE ${LIBPQXX_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
    else()
        find_path(LIBPQXX_INCLUDE_DIR pqxx/pqxx REQUIRED)
        find_path(LIBPQ_INCLUDE_DIR libpq-fe.h REQUIRED)
        find_library(LIBPQXX_LIBRARY pqxx REQUIRED)
        find_library(LIBPQ_LIBRARY pq REQUIRED)

        target_include_directories(atm_backend PRIVATE ${LIBPQXX_INCLUDE_DIR} ${LIBPQ_INCLUDE_DIR})
        target_link_libraries(atm_backend PRIVATE ${LIBPQXX_LIBRARY} ${LIBPQ_LIBRARY} OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

if(WIN32)
    target_link_libraries(atm_backend PRIVATE ws2_32)
endif()
