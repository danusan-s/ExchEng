cmake_minimum_required(VERSION 3.10)
project(ExchEng)

set(CMAKE_CXX_STANDARD 20)

# compile_commands.json, needed by clang-tidy and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(EXCHANGE_SOURCE_FILES
  src/common/tcp_server.cpp
  src/common/tcp_socket.cpp
  src/common/multicast_socket.cpp
  src/exchange/matcher/matching_engine.cpp
  src/exchange/matcher/me_order.cpp
  src/exchange/matcher/me_order_book.cpp
  src/exchange/order_server/order_server.cpp
  src/exchange/order_server/fifo_sequencer.cpp
  src/exchange/market_data/market_data_publisher.cpp
  src/exchange/market_data/snapshot_synthesizer.cpp
  src/exchange/main.cpp
)

add_executable(ExchEng ${EXCHANGE_SOURCE_FILES})

target_include_directories(ExchEng
  PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(ExchEng PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Werror
)
target_link_options(ExchEng PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-flto>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(ExchEng PRIVATE
    "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/="
  )
endif()

set(TRADING_SOURCE_FILES
  src/common/tcp_server.cpp
  src/common/tcp_socket.cpp
  src/common/multicast_socket.cpp
  src/trading/market_data/market_data_consumer.cpp
  src/trading/main.cpp
)

add_executable(TradingClient ${TRADING_SOURCE_FILES})

target_include_directories(TradingClient
  PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(TradingClient PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Werror
)
target_link_options(TradingClient PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-flto>
)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(TradingClient PRIVATE
    "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/="
  )
endif()

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executables - one per test file
enable_testing()

