cmake_minimum_required(VERSION 3.16)

project(openomada_native
    VERSION 0.1.0
    DESCRIPTION "Resource-constrained C++ Omada device agent"
    LANGUAGES CXX
)

option(OPENOMADA_BUILD_TESTS "Build native unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(openomada_core STATIC
    src/crypto/ecsp_auth.cpp
    src/crypto/hash.cpp
    src/domain/mac_address.cpp
    src/protocol/ecsp_message.cpp
    src/protocol/frame_codec.cpp
)

target_include_directories(openomada_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(openomada_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
)

if(APPLE)
    target_compile_definitions(openomada_core PRIVATE OPENOMADA_USE_COMMONCRYPTO=1)
else()
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(openomada_core PRIVATE OPENOMADA_USE_OPENSSL=1)
    target_link_libraries(openomada_core PUBLIC OpenSSL::Crypto)
endif()

add_executable(openomada-agent-native
    src/cli/main.cpp
)

target_link_libraries(openomada-agent-native PRIVATE openomada_core)

target_compile_options(openomada-agent-native PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
)

if(OPENOMADA_BUILD_TESTS)
    enable_testing()
    add_executable(openomada-core-tests
        tests/test_core.cpp
    )
    target_link_libraries(openomada-core-tests PRIVATE openomada_core)
    target_compile_options(openomada-core-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-core-tests COMMAND openomada-core-tests)
endif()
