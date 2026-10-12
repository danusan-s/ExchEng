# Tests for ExchEng (Saurav Ghosh "Building Low Latency Applications with C++", Ch.10 logic).
# One executable per component test file, discovered via CTest.

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/common/*.cpp
)
file(GLOB EXCHANGE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/exchange/matcher/*.cpp
  ${CMAKE_SOURCE_DIR}/src/exchange/market_data/*.cpp
  ${CMAKE_SOURCE_DIR}/src/exchange/order_server/*.cpp
)
file(GLOB TRADING_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/trading/strategy/*.cpp
  ${CMAKE_SOURCE_DIR}/src/trading/market_data/*.cpp
  ${CMAKE_SOURCE_DIR}/src/trading/order_gateway/*.cpp
)
# Exclude the two production mains (they define main()).
list(FILTER EXCHANGE_SOURCES EXCLUDE REGEX ".*/src/exchange/main\\.cpp$")
list(FILTER TRADING_SOURCES EXCLUDE REGEX ".*/src/trading/main\\.cpp$")

set(APP_SOURCES ${COMMON_SOURCES} ${EXCHANGE_SOURCES} ${TRADING_SOURCES})

set(TEST_FILES
  test_lf_queue.cpp
  test_mem_pool.cpp
  test_types.cpp
  test_me_order_book.cpp
  test_matching_engine.cpp
  test_fifo_sequencer.cpp
  test_snapshot_synthesizer.cpp
  test_market_order_book.cpp
  test_position_keeper.cpp
  test_risk_manager.cpp
  test_feature_engine.cpp
  test_order_manager.cpp
  test_market_maker.cpp
  test_liquidity_taker.cpp
  test_trade_engine.cpp
)

foreach(TEST_FILE ${TEST_FILES})
  get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_FILE} ${APP_SOURCES})
  target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
  )
  target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
  target_compile_options(${TEST_NAME} PRIVATE
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-O2>
  )
  target_link_options(${TEST_NAME} PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
  )
  target_link_libraries(${TEST_NAME} PRIVATE gtest_main)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  # Logger resolves "logs/..." relative to the process working directory and
  # truncates on open, and several log names are hardcoded in source ctors
  # (matching_engine.log, snapshot_synthesizer.log, trading_engine_<id>.log).
  # Run tests in an isolated CWD so `ctest` never touches the real ./logs/.
  set_tests_properties(${TEST_NAME} PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test-logs)
endforeach()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-logs)
