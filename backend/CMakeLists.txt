add_executable(atm_backend
    src/main.cpp
    src/database/Database.cpp
    src/security/PinHasher.cpp
    src/security/PhoneUtils.cpp
    src/security/SessionManager.cpp
    src/security/RateLimiter.cpp
    src/services/AuthService.cpp
)

target_include_directories(atm_backend
    PRIVATE
    include
    external/httplib
    external
    ${VAULTX_PG_INCLUDE_DIRS}
)

if(VAULTX_PG_LINK_DIRS)
    target_link_directories(atm_backend PRIVATE ${VAULTX_PG_LINK_DIRS})
endif()

target_link_libraries(atm_backend
    PRIVATE
    ${VAULTX_PG_LIBS}
)

if(WIN32)
    target_link_libraries(atm_backend PRIVATE ws2_32)
endif()
