add_executable(pin_hasher_test
    pin_hasher_test.cpp
    ../backend/src/security/PinHasher.cpp
)

target_include_directories(pin_hasher_test
    PRIVATE
    ../backend/include
)

target_link_libraries(pin_hasher_test
    PRIVATE
    OpenSSL::Crypto
)

add_test(
    NAME PINHasherTest
    COMMAND pin_hasher_test
)


add_executable(session_manager_test
    session_manager_test.cpp
    ../backend/src/security/SessionManager.cpp
)

target_include_directories(session_manager_test
    PRIVATE
    ../backend/include
)

target_link_libraries(session_manager_test
    PRIVATE
    OpenSSL::Crypto
)

add_test(
    NAME SessionManagerTest
    COMMAND session_manager_test
)


add_executable(p1_security_test
    p1_security_test.cpp
    ../backend/src/database/Database.cpp
    ../backend/src/services/AuthService.cpp
    ../backend/src/security/PinHasher.cpp
    ../backend/src/security/RateLimiter.cpp
    ../backend/src/security/SessionManager.cpp
)

target_include_directories(p1_security_test
    PRIVATE
    ../backend/include
    ${VAULTX_PG_INCLUDE_DIRS}
)

if(VAULTX_PG_LINK_DIRS)
    target_link_directories(p1_security_test PRIVATE ${VAULTX_PG_LINK_DIRS})
endif()

target_link_libraries(p1_security_test
    PRIVATE
    ${VAULTX_PG_LIBS}
)

add_test(
    NAME P1SecurityTest
    COMMAND p1_security_test
)
