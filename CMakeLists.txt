cmake_minimum_required(VERSION 3.10)
project(ExchEng)

set(CMAKE_CXX_STANDARD 20)

# compile_commands.json, needed by clang-tidy and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(SOURCE_FILES
  src/exchange/main.cpp
)

add_executable(ExchEng ${SOURCE_FILES})

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
)
target_link_options(ExchEng PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-flto>
)

target_compile_options(ExchEng PRIVATE
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Werror
)

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

