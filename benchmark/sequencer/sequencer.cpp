#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "common/perf_utils.hpp"

struct Request {
  uint64_t value;
  char padding[34]; // To mimic client request size
  Request(uint64_t v) : value(v) {
  }
};

std::vector<Request> generateRequests(size_t count, uint64_t interval_ns = 1000,
                                      size_t swaps = 10) {
  std::vector<Request> requests;
  requests.reserve(count);

  // Generate monotonically increasing receive timestamps.
  for (size_t i = 0; i < count; ++i) {
    requests.push_back(Request(i * interval_ns));
  }

  // Introduce occasional out-of-order timestamps.
  std::mt19937_64 rng(42);
  if (count > 1) {
    std::uniform_int_distribution<size_t> index_dist(0, count - 1);

    for (size_t i = 0; i < swaps; ++i) {
      size_t a = index_dist(rng);
      size_t b = index_dist(rng);
      std::swap(requests[a].value, requests[b].value);
    }
  }

  return requests;
}

size_t benchmarkStdNearlySorted(size_t numElems) {
  std::vector<Request> vec = generateRequests(numElems, 1000, 10);

  const auto start = common::rdtsc();
  // std sort
  std::sort(vec.begin(), vec.end(), [](const Request &a, const Request &b) {
    return a.value < b.value;
  });

  const auto end = common::rdtsc();

  // print so that compiler doesn't optimize away the sort
  std::cout << "First element after insertion sort: " << vec[0].value
            << std::endl;

  return end - start;
}

size_t benchmarkInsertionNearlySorted(size_t numElems) {
  std::vector<Request> requests = generateRequests(numElems, 1000, 10);

  const auto start = common::rdtsc();
  // Insertion sort
  for (size_t i = 1; i < requests.size(); ++i) {
    Request key = std::move(requests[i]);
    size_t j = i;

    while (j > 0 && requests[j - 1].value > key.value) {
      requests[j] = std::move(requests[j - 1]);
      --j;
    }

    requests[j] = std::move(key);
  }
  const auto end = common::rdtsc();

  // print so that compiler doesn't optimize away the sort
  std::cout << "First element after insertion sort: " << requests[0].value
            << std::endl;

  return end - start;
}

int main(int, char **) {
  size_t numRuns = 5;
  size_t numVariants = 8;
  for (size_t j = 0; j < numVariants; ++j) {
    size_t numElems =
        (rand() + 32ll) % 1024; // Random number of elements between 32 and 1024
    size_t totalStdCycles = 0;
    size_t totalInsertionCycles = 0;
    for (size_t i = 0; i < numRuns; ++i) {
      {
        size_t cycles = benchmarkStdNearlySorted(numElems);
        totalStdCycles += cycles;
        std::cout << "std::sort cycles for " << numElems
                  << " nearly sorted elements: " << cycles << std::endl;
      }

      {
        size_t cycles = benchmarkInsertionNearlySorted(numElems);
        totalInsertionCycles += cycles;
        std::cout << "Insertion sort cycles for " << numElems
                  << " nearly sorted elements: " << cycles << std::endl;
      }
    }

    std::cout << "Number of elements: " << numElems << std::endl;
    std::cout << "Average std::sort cycles for " << numElems
              << " nearly sorted elements: " << totalStdCycles / numRuns
              << std::endl;
    std::cout << "Average insertion sort cycles for " << numElems
              << " nearly sorted elements: " << totalInsertionCycles / numRuns
              << std::endl;
  }

  exit(EXIT_SUCCESS);
}
