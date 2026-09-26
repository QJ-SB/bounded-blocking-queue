cmake_minimum_required(VERSION 3.14)

project(
    bounded_blocking_queue
    LANGUAGES CXX
)


# ==============================================================================
# Optional build features
# ==============================================================================

option(
    ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

# INTERFACE-target used only to carry sanitizer usage requirements.
add_library(sanitizers INTERFACE)

if(ENABLE_SANITIZERS)
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

    gtest_discover_tests(bounded_blocking_queue_tests)
endif()
