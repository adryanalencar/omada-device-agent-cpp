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
    src/application/client_state.cpp
    src/application/configuration.cpp
    src/application/configuration_exchange.cpp
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
    src/lifecycle/managed_request_handler.cpp
    src/lifecycle/reconnect.cpp
    src/openwrt/uci.cpp
    src/protocol/ecsp_builders.cpp
    src/protocol/ecsp_message.cpp
    src/protocol/frame_codec.cpp
    src/protocol/json.cpp
    src/persistence/session_state_repository.cpp
    src/platform/capabilities.cpp
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

    add_executable(openomada-client-state-tests
        tests/test_client_state.cpp
    )
    target_link_libraries(openomada-client-state-tests PRIVATE openomada_core)
    target_compile_options(openomada-client-state-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-client-state-tests COMMAND openomada-client-state-tests)

    add_executable(openomada-managed-session-tests
        tests/test_managed_session.cpp
    )
    target_link_libraries(openomada-managed-session-tests PRIVATE openomada_core)
    target_compile_options(openomada-managed-session-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-managed-session-tests COMMAND openomada-managed-session-tests)

    add_executable(openomada-reconnect-tests
        tests/test_reconnect.cpp
    )
    target_link_libraries(openomada-reconnect-tests PRIVATE openomada_core)
    target_compile_options(openomada-reconnect-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-reconnect-tests COMMAND openomada-reconnect-tests)

    add_executable(openomada-configuration-tests
        tests/test_configuration.cpp
    )
    target_link_libraries(openomada-configuration-tests PRIVATE openomada_core)
    target_compile_options(openomada-configuration-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-configuration-tests COMMAND openomada-configuration-tests)

    add_executable(openomada-configuration-exchange-tests
        tests/test_configuration_exchange.cpp
    )
    target_link_libraries(openomada-configuration-exchange-tests PRIVATE openomada_core)
    target_compile_options(openomada-configuration-exchange-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-configuration-exchange-tests COMMAND openomada-configuration-exchange-tests)

    add_executable(openomada-managed-request-tests
        tests/test_managed_request_handler.cpp
    )
    target_link_libraries(openomada-managed-request-tests PRIVATE openomada_core)
    target_compile_options(openomada-managed-request-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-managed-request-tests COMMAND openomada-managed-request-tests)

    add_executable(openomada-platform-capabilities-tests
        tests/test_platform_capabilities.cpp
    )
    target_link_libraries(openomada-platform-capabilities-tests PRIVATE openomada_core)
    target_compile_options(openomada-platform-capabilities-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-platform-capabilities-tests COMMAND openomada-platform-capabilities-tests)

    add_executable(openomada-openwrt-uci-tests
        tests/test_openwrt_uci.cpp
    )
    target_link_libraries(openomada-openwrt-uci-tests PRIVATE openomada_core)
    target_compile_options(openomada-openwrt-uci-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
    )
    add_test(NAME openomada-openwrt-uci-tests COMMAND openomada-openwrt-uci-tests)
endif()
