add_executable(SequencerBenchmark sequencer.cpp)

target_include_directories(SequencerBenchmark
  PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(SequencerBenchmark PRIVATE cxx_std_20)

target_compile_options(SequencerBenchmark PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    -Wall -Wextra -Wpedantic
)
target_link_options(SequencerBenchmark PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(SequencerBenchmark PRIVATE
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
  )
endif()
