cmake_minimum_required(VERSION 3.20)
project(ProducerConsumer LANGUAGES CXX)

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

# ── Common static library ────────────────────────────────────────
add_library(common STATIC
    src/common/message.cpp
    src/common/queue.cpp
    src/common/socket.cpp
    src/common/signal_handler.cpp
    src/common/checkpoint.cpp
    src/common/util.cpp
)
target_include_directories(common PUBLIC include)
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)

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
