#pragma once

#include <fstream>
#include <string>

#include "lf_queue.hpp"
#include "macros.hpp"
#include "thread_utils.hpp"
#include "time_utils.hpp"

namespace common {

inline constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

enum class LogType : int8_t {
  CHAR = 0,
  INTEGER = 1,
  LONG_INTEGER = 2,
  LONG_LONG_INTEGER = 3,
  UNSIGNED_INTEGER = 4,
  UNSIGNED_LONG_INTEGER = 5,
  UNSIGNED_LONG_LONG_INTEGER = 6,
  FLOAT = 7,
  DOUBLE = 8
};

struct LogElement {
  LogType type_ = LogType::CHAR;
  union {
    char c;
    int i;
    long l;
    long long ll;
    unsigned u;
    unsigned long ul;
    unsigned long long ull;
    float f;
    double d;
  } u_;
};

class Logger final {
public:
  auto flushQueue() noexcept {
    while (m_running) {

      for (auto next = m_queue.getNextToRead(); m_queue.size() && next;
           next = m_queue.getNextToRead()) {
        switch (next->type_) {
          case LogType::CHAR:
            m_file << next->u_.c;
            break;
          case LogType::INTEGER:
            m_file << next->u_.i;
            break;
          case LogType::LONG_INTEGER:
            m_file << next->u_.l;
            break;
          case LogType::LONG_LONG_INTEGER:
            m_file << next->u_.ll;
            break;
          case LogType::UNSIGNED_INTEGER:
            m_file << next->u_.u;
            break;
          case LogType::UNSIGNED_LONG_INTEGER:
            m_file << next->u_.ul;
            break;
          case LogType::UNSIGNED_LONG_LONG_INTEGER:
            m_file << next->u_.ull;
            break;
          case LogType::FLOAT:
            m_file << next->u_.f;
            break;
          case LogType::DOUBLE:
            m_file << next->u_.d;
            break;
        }
        m_queue.updateReadIndex();
      }
      m_file.flush();

      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(10ms);
    }
  }

  explicit Logger(const std::string &file_name)
      : m_fileName(file_name), m_queue(LOG_QUEUE_SIZE) {
    m_file.open(file_name);
    ASSERT(m_file.is_open(), "Could not open log file:" + file_name);
    m_loggerThread = createAndStartThread(-1, "Common/Logger " + m_fileName,
                                          [this]() { flushQueue(); });
    ASSERT(m_loggerThread != nullptr, "Failed to start Logger thread.");
  }

  ~Logger() {
    std::string time_str;
    std::cerr << common::getCurrentTimeStr(&time_str)
              << " Flushing and closing Logger for " << m_fileName << std::endl;

    while (m_queue.size()) {
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(1s);
    }
    m_running = false;
    m_loggerThread->join();

    m_file.close();
    std::cerr << common::getCurrentTimeStr(&time_str) << " Logger for "
              << m_fileName << " exiting." << std::endl;
  }

  auto pushValue(const LogElement &log_element) noexcept {
    *(m_queue.getNextToWriteTo()) = log_element;
    m_queue.updateWriteIndex();
  }

  auto pushValue(const char value) noexcept {
    pushValue(LogElement{LogType::CHAR, {.c = value}});
  }

  auto pushValue(const int value) noexcept {
    pushValue(LogElement{LogType::INTEGER, {.i = value}});
  }

  auto pushValue(const long value) noexcept {
    pushValue(LogElement{LogType::LONG_INTEGER, {.l = value}});
  }

  auto pushValue(const long long value) noexcept {
    pushValue(LogElement{LogType::LONG_LONG_INTEGER, {.ll = value}});
  }

  auto pushValue(const unsigned value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_INTEGER, {.u = value}});
  }

  auto pushValue(const unsigned long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_INTEGER, {.ul = value}});
  }

  auto pushValue(const unsigned long long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_LONG_INTEGER, {.ull = value}});
  }

  auto pushValue(const float value) noexcept {
    pushValue(LogElement{LogType::FLOAT, {.f = value}});
  }

  auto pushValue(const double value) noexcept {
    pushValue(LogElement{LogType::DOUBLE, {.d = value}});
  }

  auto pushValue(const char *value) noexcept {
    while (*value) {
      pushValue(*value);
      ++value;
    }
  }

  auto pushValue(const std::string &value) noexcept {
    pushValue(value.c_str());
  }

  template <typename T, typename... A>
  auto log(const char *s, const T &value, A... args) noexcept {
    while (*s) {
      if (*s == '%') {
        if (*(s + 1) == '%')
            [[unlikely]] { // to allow %% -> % escape character.
          ++s;
        } else {
          pushValue(
              value); // substitute % with the value specified in the arguments.
          log(s + 1, args...); // pop an argument and call self recursively.
          return;
        }
      }
      pushValue(*s++);
    }
    FATAL("extra arguments provided to log()");
  }

  // note that this is overloading not specialization. gcc does not allow inline
  // specializations.
  auto log(const char *s) noexcept {
    while (*s) {
      if (*s == '%') {
        if (*(s + 1) == '%')
            [[unlikely]] { // to allow %% -> % escape character.
          ++s;
        } else {
          FATAL("missing arguments to log()");
        }
      }
      pushValue(*s++);
    }
  }

  // Deleted default, copy & move constructors and assignment-operators.
  Logger() = delete;
  Logger(const Logger &) = delete;
  Logger(const Logger &&) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger &operator=(const Logger &&) = delete;

private:
  const std::string m_fileName;
  std::ofstream m_file;

  LFQueue<LogElement> m_queue;
  std::atomic<bool> m_running = {true};
  std::thread *m_loggerThread = nullptr;
};
} // namespace common
