
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
