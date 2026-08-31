cmake_minimum_required(VERSION 3.16)

project(openomada_native
    VERSION 0.1.0
    DESCRIPTION "Resource-constrained C++ Omada device agent"
    LANGUAGES CXX
)

option(OPENOMADA_BUILD_TESTS "Build native unit tests" ON)
option(OPENOMADA_ENABLE_TLS "Build OpenSSL TLS transport" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(openomada_core STATIC
    src/application/inform.cpp
    src/crypto/ecsp_auth.cpp
    src/crypto/hash.cpp
    src/crypto/random.cpp
    src/domain/device_profile.cpp
    src/domain/mac_address.cpp
    src/lifecycle/session.cpp
    src/lifecycle/adoption.cpp
    src/lifecycle/discovery.cpp
    src/lifecycle/managed_session.cpp
    src/protocol/ecsp_builders.cpp
    src/protocol/ecsp_message.cpp
    src/protocol/frame_codec.cpp
    src/protocol/json.cpp
    src/persistence/session_state_repository.cpp
    src/transport/socket.cpp
    src/transport/tls_client.cpp
    src/transport/udp_discovery.cpp
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

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
target_link_libraries(openomada_core PUBLIC PkgConfig::JSONC)

if(NOT OPENOMADA_ENABLE_TLS)
    message(FATAL_ERROR "Phase 2 requires OPENOMADA_ENABLE_TLS=ON for TLS adoption and secure random generation")
endif()
pkg_check_modules(OPENSSL_TLS REQUIRED IMPORTED_TARGET openssl)
target_compile_definitions(openomada_core PUBLIC OPENOMADA_ENABLE_TLS=1)
target_link_libraries(openomada_core PUBLIC PkgConfig::OPENSSL_TLS)

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

    add_executable(openomada-lifecycle-tests
        tests/test_lifecycle_phase2.cpp
    )
    target_link_libraries(openomada-lifecycle-tests PRIVATE openomada_core)
    target_compile_options(openomada-lifecycle-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-lifecycle-tests COMMAND openomada-lifecycle-tests)

    add_executable(openomada-persistence-tests
        tests/test_persistence.cpp
    )
    target_link_libraries(openomada-persistence-tests PRIVATE openomada_core)
    target_compile_options(openomada-persistence-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-persistence-tests COMMAND openomada-persistence-tests)

    add_executable(openomada-inform-tests
        tests/test_inform.cpp
    )
    target_link_libraries(openomada-inform-tests PRIVATE openomada_core)
    target_compile_options(openomada-inform-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-inform-tests COMMAND openomada-inform-tests)

    add_executable(openomada-managed-session-tests
        tests/test_managed_session.cpp
    )
    target_link_libraries(openomada-managed-session-tests PRIVATE openomada_core)
    target_compile_options(openomada-managed-session-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-managed-session-tests COMMAND openomada-managed-session-tests)
endif()
