#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "common/logging.hpp"
#include "common/perf_utils.hpp"
#include "old_logger.hpp"

std::string random_string(size_t length) {
  auto randchar = []() -> char {
    const char charset[] = "0123456789"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

template <typename T> size_t benchmarkLogging(T *logger) {
  constexpr size_t loop_count = 100000;
  size_t total_rdtsc = 0;
  for (size_t i = 0; i < loop_count; ++i) {
    const auto s = random_string(128);
    const auto start = common::rdtsc();
    logger->logInfo("%\n", s);
    total_rdtsc += (common::rdtsc() - start);
  }
  for (size_t i = 0; i < loop_count; ++i) {
    const auto s = random_string(128);
    const auto start = common::rdtsc();
    logger->logDebug("%\n", s.c_str());
    total_rdtsc += (common::rdtsc() - start);
  }

  return (total_rdtsc / loop_count);
}

int main(int, char **) {
  using namespace std::literals::chrono_literals;

  {
    common::Logger logger("logger_benchmark_new.log");
    const auto cycles = benchmarkLogging(&logger);
    std::cout << "NEW LOGGER " << cycles << " CLOCK CYCLES PER OPERATION."
              << std::endl;
    std::this_thread::sleep_for(10s);
  }

  {
    old_common::OldLogger old_logger("logger_benchmark_old.log");
    const auto cycles = benchmarkLogging(&old_logger);
    std::cout << "ORIGINAL LOGGER " << cycles << " CLOCK CYCLES PER OPERATION."
              << std::endl;
    std::this_thread::sleep_for(10s);
  }

  exit(EXIT_SUCCESS);
}
