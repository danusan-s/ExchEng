add_executable(MemPoolBenchmark mem_pool_benchmark.cpp)

target_include_directories(MemPoolBenchmark
  PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(MemPoolBenchmark PRIVATE cxx_std_20)

target_compile_options(MemPoolBenchmark PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    -Wall -Wextra -Wpedantic
)
target_link_options(MemPoolBenchmark PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(MemPoolBenchmark PRIVATE
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
  )
endif()
