
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

add_subdirectory(backend)
add_subdirectory(tests)

