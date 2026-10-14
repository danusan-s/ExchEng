#pragma once

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "common/lf_queue.hpp"
#include "common/macros.hpp"
#include "common/thread_utils.hpp"
#include "common/time_utils.hpp"

namespace common {

inline constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;
inline constexpr size_t LOG_BUFFERED_STRING_SIZE =
    8; // Memory to speed tradeoff, 8 is no wasted mem but still faster than
       // old single byte char, higher value waste mem but faster

enum class LogLevel : int8_t {
  NONE =
      0, // Used to indicate no log level, for example when logging a timestamp
  DATA = 1,
  INFO = 2,
  DEBUG = 3,
  WARN = 4,
  ERROR = 5,
};

inline const char *getLogLevelString(int i) {
  LogLevel level = static_cast<LogLevel>(i);
  switch (level) {
    case LogLevel::DATA:
      return " \033[36m[DATA]\033[0m ";
    case LogLevel::INFO:
      return " \033[32m[INFO]\033[0m ";
    case LogLevel::DEBUG:
      return " \033[90m[DEBUG]\033[0m ";
    case LogLevel::WARN:
      return " \033[33m[WARN]\033[0m ";
    case LogLevel::ERROR:
      return " \033[31m[ERROR]\033[0m ";
    case LogLevel::NONE:
      return "";
    default:
      return "UNKNOWN";
  }
}

enum class LogType : int8_t {
  TIMESTAMP = 0,
  CHAR = 1,
  INTEGER = 2,
  LONG_INTEGER = 3,
  LONG_LONG_INTEGER = 4,
  UNSIGNED_INTEGER = 5,
  UNSIGNED_LONG_INTEGER = 6,
  UNSIGNED_LONG_LONG_INTEGER = 7,
  FLOAT = 8,
  DOUBLE = 9,
  STRING = 10,
  LOG_LEVEL = 11, // Seperate because ansi coding has
                  // \0 terminator so can treat as string
};

struct LogElement {
  LogType type_ = LogType::CHAR;
  union {
    Nanos t;
    char c;
    char s[LOG_BUFFERED_STRING_SIZE];
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
  explicit Logger(const std::string &file_name)
      : m_fileName("logs/" + file_name), m_queue(LOG_QUEUE_SIZE) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(m_fileName).parent_path(), ec);
    ASSERT(!ec, "Could not create logs directory: " + ec.message());
    m_file.open(m_fileName);
    ASSERT(m_file.is_open(), "Could not open log file:" + m_fileName);
    m_loggerThread = createAndStartThread(-1, "Common/Logger " + m_fileName,
                                          [this]() { flushQueue(); });
    ASSERT(m_loggerThread != nullptr, "Failed to start Logger thread.");
  }

  ~Logger() {
    std::string time_str;
    std::cerr << common::getCurrentTimeStr(time_str)
              << " Flushing and closing Logger for " << m_fileName << std::endl;

    while (m_queue.size()) {
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(1s);
    }
    m_running = false;
    m_loggerThread->join();

    m_file.close();
    std::cerr << common::getCurrentTimeStr(time_str) << " Logger for "
              << m_fileName << " exiting." << std::endl;
  }

  template <typename... A>
  void log(LogLevel level, const char *s, A... args) noexcept {
    pushValue(LogElement{LogType::TIMESTAMP, {.t = getCurrentNanos()}});
    pushValue(LogElement{LogType::LOG_LEVEL, {.i = static_cast<int>(level)}});
    log(s, args...);
  }

  template <typename... A> void logInfo(const char *s, A... args) noexcept {
    log(LogLevel::INFO, s, args...);
  }

  template <typename... A> void logData(const char *s, A... args) noexcept {
    log(LogLevel::DATA, s, args...);
  }

  template <typename... A> void logDebug(const char *s, A... args) noexcept {
    log(LogLevel::DEBUG, s, args...);
  }

  template <typename... A> void logWarn(const char *s, A... args) noexcept {
    log(LogLevel::WARN, s, args...);
  }

  template <typename... A> void logError(const char *s, A... args) noexcept {
    log(LogLevel::ERROR, s, args...);
  }

  Logger() = delete;
  Logger(const Logger &) = delete;
  Logger(Logger &&) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger &operator=(Logger &&) = delete;

private:
  void flushQueue() noexcept {
    while (m_running) {
      bool written = false;
      for (auto next = m_queue.getNextToRead(); m_queue.size() && next;
           next = m_queue.getNextToRead()) {
        written = true;
        switch (next->type_) {
          case LogType::TIMESTAMP:
            m_file << common::formatTimestamp(next->u_.t, m_timeStr);
            break;
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
          case LogType::STRING: {
            for (size_t i = 0;
                 i < LOG_BUFFERED_STRING_SIZE && next->u_.s[i] != '\0'; ++i) {
              m_file << next->u_.s[i];
            }
          } break;
          case LogType::LOG_LEVEL:
            auto logLevelStr = getLogLevelString(next->u_.i);
            m_file << logLevelStr;
            break;
        }
        m_queue.updateReadIndex();
      }
      if (written) {
        m_file.flush();
      }

      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(10ms);
    }
  }

  void pushValue(const LogElement &logElement) noexcept {
    *(m_queue.getNextToWriteTo()) = logElement;
    m_queue.updateWriteIndex();
  }

  void pushValue(const char value) noexcept {
    pushValue(LogElement{LogType::CHAR, {.c = value}});
  }

  void pushValue(const int value) noexcept {
    pushValue(LogElement{LogType::INTEGER, {.i = value}});
  }

  void pushValue(const long value) noexcept {
    pushValue(LogElement{LogType::LONG_INTEGER, {.l = value}});
  }

  void pushValue(const long long value) noexcept {
    pushValue(LogElement{LogType::LONG_LONG_INTEGER, {.ll = value}});
  }

  void pushValue(const unsigned value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_INTEGER, {.u = value}});
  }

  void pushValue(const unsigned long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_INTEGER, {.ul = value}});
  }

  void pushValue(const unsigned long long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_LONG_INTEGER, {.ull = value}});
  }

  void pushValue(const float value) noexcept {
    pushValue(LogElement{LogType::FLOAT, {.f = value}});
  }

  void pushValue(const double value) noexcept {
    pushValue(LogElement{LogType::DOUBLE, {.d = value}});
  }

  void pushValue(const char *value, size_t length) noexcept {
    size_t i = 0;
    for (; i + LOG_BUFFERED_STRING_SIZE < length;
         i += LOG_BUFFERED_STRING_SIZE) {
      LogElement logElement{LogType::STRING, {}};
      std::memcpy(logElement.u_.s, value + i, LOG_BUFFERED_STRING_SIZE);
      pushValue(logElement);
    }

    LogElement logElement{LogType::STRING, {}};
    std::memcpy(logElement.u_.s, value + i, length - i);
    logElement.u_.s[length - i] = '\0'; // Null terminate the string
    pushValue(logElement);
  }

  void pushValue(const char *value) noexcept {
    pushValue(value, std::strlen(value));
  }

  void pushValue(const std::string &value) noexcept {
    pushValue(value.c_str(), value.size());
  }

  template <typename T, typename... A>
  void log(const char *s, const T &value, A... args) noexcept {
    size_t len = 0;
    while (*(s + len)) {
      if (*(s + len) == '%') {
        pushValue(s, len); // push the string before the % to the log queue.
        s += len;          // move the pointer to the % character.
        len = 0;           // reset the length counter to 0.
        if (*(s + len + 1) == '%')
            [[unlikely]] { // to allow %% -> % escape character.
          ++s;
        } else {
          pushValue(value);    // substitute % with the value specified in the
                               // arguments.
          log(s + 1, args...); // pop an argument and call self recursively.
          return;
        }
      }
      ++len;
    }
    if (len > 0) {
      pushValue(s, len); // push the remaining string to the log queue.
    };
    pushValue("ERROR: extra arguments provided to log()\n");
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);
    FATAL("extra arguments provided to log(), "
          "check logs to narrow down the issue.");
  }

  void log(const char *s) noexcept {
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

private:
  const std::string m_fileName;
  std::ofstream m_file;

  std::string m_timeStr;

  LFQueue<LogElement> m_queue;
  std::atomic<bool> m_running = {true};
  std::unique_ptr<std::thread> m_loggerThread = nullptr;
};

} // namespace common
