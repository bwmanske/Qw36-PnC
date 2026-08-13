cmake_minimum_required(VERSION 3.20)
project(ProducerConsumer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── nlohmann/json ────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── zlib (required by libarchive for ZIP support) ────────────────
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

# ── libarchive (ZIP, RAR, 7Z validation) ─────────────────────────
FetchContent_Declare(
    libarchive
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v3.7.9
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_RAR ON CACHE BOOL "" FORCE)
set(ENABLE_FORMAT_7ZIP ON CACHE BOOL "" FORCE)
set(ENABLE_FORMAT_ZIP ON CACHE BOOL "" FORCE)
set(ENABLE_FORMAT_RAR ON CACHE BOOL "" FORCE)
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
set(ENABLE_ICONV OFF CACHE BOOL "" FORCE)
set(ENABLE_CNG ON CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_NETTLE OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_XZ OFF CACHE BOOL "" FORCE)
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_CPPTEST OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libarchive)

# ── Common static library ────────────────────────────────────────
add_library(common STATIC
    src/common/message.cpp
    src/common/queue.cpp
    src/common/socket.cpp
    src/common/signal_handler.cpp
    src/common/checkpoint.cpp
    src/common/util.cpp
    src/common/archive_validator.cpp
)
target_include_directories(common PUBLIC include)
target_include_directories(common PUBLIC ${libarchive_SOURCE_DIR}/libarchive)
target_compile_definitions(common PUBLIC LIBARCHIVE_STATIC)
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(common PUBLIC ${CMAKE_BINARY_DIR}/_deps/libarchive-build/libarchive/Release/archive.lib)

# ── Producer static library (no main) ────────────────────────────
add_library(producer_lib STATIC
    src/producer/producer.cpp
    src/producer/work_tracker.cpp
    src/producer/PWD_NextUnit.cpp
    src/producer/PWD_plugin.cpp
    src/producer/BENCH_plugin.cpp
)
target_include_directories(producer_lib PUBLIC include src/producer)
target_link_libraries(producer_lib PUBLIC common)

# ── Consumer static library (no main) ────────────────────────────
add_library(consumer_lib STATIC
    src/consumer/consumer.cpp
    src/consumer/thread_pool.cpp
    src/consumer/PWD_Handler.cpp
    src/consumer/BENCH_Handler.cpp
    src/consumer/file_result_sink.cpp
)
target_include_directories(consumer_lib PUBLIC include src/consumer)
target_link_libraries(consumer_lib PUBLIC common)

# ── Producer executable ──────────────────────────────────────────
add_executable(producer src/producer/main.cpp)
target_link_libraries(producer PRIVATE producer_lib)

# ── Consumer executable ──────────────────────────────────────────
add_executable(consumer src/consumer/main.cpp)
target_link_libraries(consumer PRIVATE consumer_lib)

# ── Platform-specific linking ────────────────────────────────────
if(WIN32)
    target_link_libraries(common PRIVATE ws2_32)
endif()

# ── Tests ────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit and integration tests" ON)
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()
