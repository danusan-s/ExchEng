include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

option(EXCHENG_TEST_ASAN "Build tests with AddressSanitizer (heavy)" OFF)

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/common/*.cpp
)
file(GLOB EXCHANGE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/exchange/matcher/*.cpp
  ${CMAKE_SOURCE_DIR}/src/exchange/market_data/*.cpp
  ${CMAKE_SOURCE_DIR}/src/exchange/order_server/*.cpp
)
file(GLOB TRADING_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/trading/trader/*.cpp
  ${CMAKE_SOURCE_DIR}/src/trading/market_data/*.cpp
  ${CMAKE_SOURCE_DIR}/src/trading/order_gateway/*.cpp
)
list(FILTER EXCHANGE_SOURCES EXCLUDE REGEX ".*/src/exchange/main\\.cpp$")
list(FILTER TRADING_SOURCES EXCLUDE REGEX ".*/src/trading/main\\.cpp$")

set(APP_SOURCES ${COMMON_SOURCES} ${EXCHANGE_SOURCES} ${TRADING_SOURCES})

add_library(excheng_test_app STATIC ${APP_SOURCES})
target_include_directories(excheng_test_app PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(excheng_test_app PUBLIC cxx_std_20)
target_compile_options(excheng_test_app PRIVATE
  -O0 -g
  -Wall -Wextra
)
if(EXCHENG_TEST_ASAN)
  target_compile_options(excheng_test_app PRIVATE -fsanitize=address)
  target_link_options(excheng_test_app PUBLIC -fsanitize=address)
endif()

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
  add_executable(${TEST_NAME} ${TEST_FILE})
  target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
  )
  target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
  target_compile_options(${TEST_NAME} PRIVATE -O0 -g -Wall -Wextra)
  if(EXCHENG_TEST_ASAN)
    target_compile_options(${TEST_NAME} PRIVATE -fsanitize=address)
    target_link_options(${TEST_NAME} PRIVATE -fsanitize=address)
  endif()
  target_link_libraries(${TEST_NAME} PRIVATE excheng_test_app gtest_main)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  set_tests_properties(${TEST_NAME} PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test-logs)
  list(APPEND TEST_EXE_TARGETS ${TEST_NAME})
endforeach()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-logs)

add_custom_target(build-tests DEPENDS ${TEST_EXE_TARGETS})
