
add_executable(atm_backend
    src/main.cpp
    src/database/Database.cpp
    src/security/PinHasher.cpp
    src/security/SessionManager.cpp
    src/security/RateLimiter.cpp
    src/services/AuthService.cpp
)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

target_include_directories(atm_backend
    PRIVATE
    include
    external/httplib
    external
)

target_link_libraries(atm_backend
    PRIVATE
    SQLite3::SQLite3
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(atm_backend PRIVATE ws2_32)
endif()

