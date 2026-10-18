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

## Reject incompatible sanitizer configuration:
if(ENABLE_ASAN_UBSAN AND ENABLE_TSAN)
    message(
        FATAL_ERROR
        "ENABLE_ASAN_UBSAN and ENABLE_TSAN cannot be enabled together."
    )
endif()


# INTERFACE-target used only to carry sanitizer usage requirements:
add_library(
    sanitizers
    INTERFACE
    )

## Asan + UBsan:
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

## Tsan:
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

# BoundedBlockingQueue (Header-only):
add_library(
    bounded_blocking_queue
    INTERFACE
)

target_include_directories(
    bounded_blocking_queue
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(
    bounded_blocking_queue
    INTERFACE
    cxx_std_17
)

target_link_libraries(
    bounded_blocking_queue
    INTERFACE
    sanitizers
)

# BinaryCodec (Compiled static library):
add_library(
    binary_codec
    STATIC
    binary_codec.cpp
)

target_include_directories(
    binary_codec
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(
    binary_codec
    PUBLIC
    cxx_std_17
)

target_link_libraries(
    binary_codec
    PUBLIC
    sanitizers
)

# TradingEventCodec (Compiled static library):
add_library(
    trading_event_codec
    STATIC
    trading_event_codec.cpp
)

target_include_directories(
    trading_event_codec
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(
    trading_event_codec
    PUBLIC
    cxx_std_17
)

target_link_libraries(
    trading_event_codec
    PRIVATE
    binary_codec
    PUBLIC
    sanitizers
)

# ==============================================================================
# Main executable
# ==============================================================================

add_executable(
    events_pipeline
    main.cpp
)

target_link_libraries(
    events_pipeline
    PRIVATE
    bounded_blocking_queue
)


# ==============================================================================
# Tests
# ==============================================================================

# CTest:
include(CTest)

if(BUILD_TESTING)

    # ctest:
    add_test(
        NAME EventsPipeline
        COMMAND events_pipeline
    )

    set_tests_properties(
        EventsPipeline
        PROPERTIES
        TIMEOUT 5
    )


    # gtest:
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    FetchContent_MakeAvailable(googletest)

    # BoundedBlockingQueueTests:
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
    
    # BinaryCodec:
    add_executable(
        binary_codec_tests
        tests/binary_codec_tests.cpp
    )

    target_link_libraries(
        binary_codec_tests
        PRIVATE
        binary_codec
        GTest::gtest_main
    )

    # TradingEventCodec:
    add_executable(
        trading_event_codec_tests
        tests/trading_event_codec_tests.cpp
    )

    target_link_libraries(
        trading_event_codec_tests
        PRIVATE
        trading_event_codec
        GTest::gtest_main
    )


    # Register gtest with ctest:
    include(GoogleTest)

    gtest_discover_tests(
    bounded_blocking_queue_tests
    PROPERTIES
    TIMEOUT 5
    )

    gtest_discover_tests(binary_codec_tests)

    gtest_discover_tests(trading_event_codec_tests)

endif()
