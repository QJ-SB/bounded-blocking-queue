cmake_minimum_required(VERSION 3.14)

project(
    bounded_blocking_queue
    LANGUAGES CXX
)


# ==============================================================================
# Optional build features
# ==============================================================================

option(
    ENABLE_ASAN_UBSAN
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

option(
    ENABLE_TSAN
    "Enable ThreadSanitizer"
    OFF
)

## Reject incompatible sanitizer configuration.
if(ENABLE_ASAN_UBSAN AND ENABLE_TSAN)
    message(
        FATAL_ERROR
        "ENABLE_ASAN_UBSAN and ENABLE_TSAN cannot be enabled together."
    )
endif()


# INTERFACE-target used only to carry sanitizer usage requirements.
add_library(sanitizers INTERFACE)

## Asan + UBsan
if(ENABLE_ASAN_UBSAN)
    target_compile_options(
        sanitizers
        INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )

    target_link_options(
        sanitizers
        INTERFACE
        -fsanitize=address,undefined
    )
endif()

## Tsan
if(ENABLE_TSAN)
    target_compile_options(
        sanitizers
        INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
        -g
    )

    target_link_options(
        sanitizers
        INTERFACE
        -fsanitize=thread
    )
endif()


# ==============================================================================
# Core library
# ==============================================================================

add_library(
    bounded_blocking_queue
    INTERFACE
)

target_include_directories(
    bounded_blocking_queue
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    bounded_blocking_queue
    INTERFACE
    sanitizers
)


# ==============================================================================
# Main executable
# ==============================================================================


# ==============================================================================
# Tests
# ==============================================================================

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    FetchContent_MakeAvailable(googletest)

    add_executable(
        bounded_blocking_queue_tests
        tests/bounded_blocking_queue_tests.cpp
    )

    target_link_libraries(
        bounded_blocking_queue_tests
        PRIVATE
        bounded_blocking_queue
        GTest::gtest_main
    )

    include(GoogleTest)

    gtest_discover_tests(
    bounded_blocking_queue_tests
    PROPERTIES
    TIMEOUT 5
)
endif()
