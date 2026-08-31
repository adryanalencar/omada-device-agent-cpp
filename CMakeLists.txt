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

add_executable(openomada-agent-native
    src/cli/main.cpp
)

target_include_directories(openomada-agent-native
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(openomada-agent-native PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion>
)

if(OPENOMADA_BUILD_TESTS)
    enable_testing()
endif()

