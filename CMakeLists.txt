cmake_minimum_required(VERSION 3.10)
project(ExchEng)

set(CMAKE_CXX_STANDARD 20)

# compile_commands.json, needed by clang-tidy and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()


file(GLOB_RECURSE EXCHANGE_SOURCE_FILES CONFIGURE_DEPENDS
  src/common/*.cpp
  src/exchange/*.cpp
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

file(GLOB_RECURSE TRADING_SOURCE_FILES CONFIGURE_DEPENDS
  src/common/*.cpp
  src/trading/*.cpp
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

enable_testing()
add_subdirectory(tests EXCLUDE_FROM_ALL)

