#pragma once

#include <bitset>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "source/common/common/base_logger.h"
#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger_impl.h"
#include "source/common/common/macros.h"
#include "source/common/common/non_copyable.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "fmt/ostream.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

#ifdef ENVOY_DISABLE_LOGGING
const static bool should_log = false;
#else
const static bool should_log = true;
#endif

// TODO: find a way for extensions to register new logger IDs
#define ALL_LOGGER_IDS(FUNCTION)                                                                   \
  FUNCTION(admin)                                                                                  \
  FUNCTION(alternate_protocols_cache)                                                              \
  FUNCTION(aws)                                                                                    \
  FUNCTION(assert)                                                                                 \
  FUNCTION(backtrace)                                                                              \
  FUNCTION(basic_auth)                                                                             \
  FUNCTION(cache_filter)                                                                           \
  FUNCTION(client)                                                                                 \
  FUNCTION(config)                                                                                 \
  FUNCTION(connection)                                                                             \
  FUNCTION(conn_handler)                                                                           \
  FUNCTION(decompression)                                                                          \
  FUNCTION(dns)                                                                                    \
  FUNCTION(dubbo)                                                                                  \
  FUNCTION(envoy_bug)                                                                              \
  FUNCTION(ext_authz)                                                                              \
  FUNCTION(ext_proc)                                                                               \
  FUNCTION(rocketmq)                                                                               \
  FUNCTION(file)                                                                                   \
  FUNCTION(filter)                                                                                 \
  FUNCTION(forward_proxy)                                                                          \
  FUNCTION(geolocation)                                                                            \
  FUNCTION(grpc)                                                                                   \
  FUNCTION(happy_eyeballs)                                                                         \
  FUNCTION(hc)                                                                                     \
  FUNCTION(health_checker)                                                                         \
  FUNCTION(http)                                                                                   \
  FUNCTION(http2)                                                                                  \
  FUNCTION(hystrix)                                                                                \
  FUNCTION(init)                                                                                   \
  FUNCTION(io)                                                                                     \
  FUNCTION(jwt)                                                                                    \
  FUNCTION(kafka)                                                                                  \
  FUNCTION(key_value_store)                                                                        \
  FUNCTION(lua)                                                                                    \
  FUNCTION(main)                                                                                   \
  FUNCTION(matcher)                                                                                \
  FUNCTION(misc)                                                                                   \
  FUNCTION(mongo)                                                                                  \
  FUNCTION(multi_connection)                                                                       \
  FUNCTION(oauth2)                                                                                 \
  FUNCTION(quic)                                                                                   \
  FUNCTION(quic_stream)                                                                            \
  FUNCTION(pool)                                                                                   \
  FUNCTION(rate_limit_quota)                                                                       \
  FUNCTION(rbac)                                                                                   \
  FUNCTION(rds)                                                                                    \
  FUNCTION(redis)                                                                                  \
  FUNCTION(router)                                                                                 \
  FUNCTION(runtime)                                                                                \
  FUNCTION(stats)                                                                                  \
  FUNCTION(secret)                                                                                 \
  FUNCTION(tap)                                                                                    \
  FUNCTION(testing)                                                                                \
  FUNCTION(thrift)                                                                                 \
  FUNCTION(tracing)                                                                                \
  FUNCTION(upstream)                                                                               \
  FUNCTION(udp)                                                                                    \
  FUNCTION(wasm)                                                                                   \
  FUNCTION(websocket)                                                                              \
  FUNCTION(golang)

// clang-format off
enum class Id {
  ALL_LOGGER_IDS(GENERATE_ENUM)
};
// clang-format on

/**
 * Logger that uses the DelegatingLogSink.
 */
class StandardLogger : public Logger {
private:
  StandardLogger(const std::string& name);

  friend class Registry;
};

class DelegatingLogSink;
using DelegatingLogSinkSharedPtr = std::shared_ptr<DelegatingLogSink>;

/**
 * Captures a logging sink that can be delegated to for a bounded amount of time.
 * On destruction, logging is reverted to its previous state. SinkDelegates must
 * be allocated/freed as a stack.
 */
class SinkDelegate : NonCopyable {
public:
  explicit SinkDelegate(DelegatingLogSinkSharedPtr log_sink);
  virtual ~SinkDelegate();

  /**
   * Called to log a single log line.
   * @param formatted_msg The final, formatted message.
   * @param the original log message, including additional metadata.
   */
  virtual void log(absl::string_view msg, const spdlog::details::log_msg& log_msg) PURE;

  /**
   * Called to log a single log line with a stable name.
   * @param stable_name stable name of this log line.
   * @param level the string representation of the log level for this log line.
   * @param component the component this log was logged via.
   * @param msg the log line to log.
   */
  virtual void logWithStableName(absl::string_view stable_name, absl::string_view level,
                                 absl::string_view component, absl::string_view msg);

  /**
   * Called to flush the log sink.
   */
  virtual void flush() PURE;

protected:
  // Swap the current thread local log sink delegate for this one. This should be called by the
  // derived class constructor immediately before returning. This is required to match
  // restoreTlsDelegate(), otherwise it's possible for the previous delegate to get set in the base
  // class constructor, the derived class constructor throws, and cleanup becomes broken.
  void setTlsDelegate();

  // Swap the current *global* log sink delegate for this one. This behaves as setTlsDelegate, but
  // operates on the global log sink instead of the thread local one.
  void setDelegate();

  // Swap the current thread local log sink (this) for the previous one. This should be called by
  // the derived class destructor in the body. This is critical as otherwise it's possible for a log
  // message to get routed to a partially destructed sink.
  void restoreTlsDelegate();

  // Swap the current *global* log sink delegate for the previous one. This behaves as
  // restoreTlsDelegate, but operates on the global sink instead of the thread local one.
  void restoreDelegate();

  SinkDelegate* previousDelegate() { return previous_delegate_; }

private:
  SinkDelegate* previous_delegate_{nullptr};
  SinkDelegate* previous_tls_delegate_{nullptr};
  DelegatingLogSinkSharedPtr log_sink_;
};

/**
 * SinkDelegate that writes log messages to stderr.
 */
class StderrSinkDelegate : public SinkDelegate {
public:
  explicit StderrSinkDelegate(DelegatingLogSinkSharedPtr log_sink);
  ~StderrSinkDelegate() override;

  // SinkDelegate
  void log(absl::string_view msg, const spdlog::details::log_msg& log_msg) override;
  void flush() override;

  bool hasLock() const { return lock_ != nullptr; }
  void setLock(Thread::BasicLockable& lock) { lock_ = &lock; }
  void clearLock() { lock_ = nullptr; }

private:
  Thread::BasicLockable* lock_{};
};

/**
 * Stacks logging sinks, so you can temporarily override the logging mechanism, restoring
 * the previous state when the DelegatingSink is destructed.
 */
class DelegatingLogSink : public spdlog::sinks::sink {
public:
  void setLock(Thread::BasicLockable& lock) { stderr_sink_->setLock(lock); }
  void clearLock() { stderr_sink_->clearLock(); }

  template <class FmtStr, class... Args>
  void logWithStableName(absl::string_view stable_name, absl::string_view level,
                         absl::string_view component, FmtStr fmt_str, Args... msg) {
    auto tls_sink = tlsDelegate();
    if (tls_sink != nullptr) {
      tls_sink->logWithStableName(stable_name, level, component,
                                  fmt::format(fmt::runtime(fmt_str), msg...));
      return;
    }
    absl::ReaderMutexLock sink_lock(&sink_mutex_);
    sink_->logWithStableName(stable_name, level, component,
                             fmt::format(fmt::runtime(fmt_str), msg...));
  }
  // spdlog::sinks::sink
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override;
  void set_pattern(const std::string& pattern) override {
    set_formatter(spdlog::details::make_unique<spdlog::pattern_formatter>(pattern));
  }
  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override;
  void setShouldEscape(bool should_escape) { should_escape_ = should_escape; }

  /**
   * @return bool whether a lock has been established.
   */
  bool hasLock() const { return stderr_sink_->hasLock(); }

  /**
   * Constructs a new DelegatingLogSink, sets up the default sink to stderr,
   * and returns a shared_ptr to it.
   *
   * A shared_ptr is required for sinks used
   * in spdlog::logger; it would not otherwise be required in Envoy. This method
   * must own the construction process because StderrSinkDelegate needs access to
   * the DelegatingLogSinkSharedPtr, not just the DelegatingLogSink*, and that is only
   * available after construction.
   */
  static DelegatingLogSinkSharedPtr init();

  /**
   * Give a log line with trailing whitespace, this will escape all c-style
   * escape sequences except for the trailing whitespace.
   * This allows logging escaped messages, but preserves end-of-line characters.
   *
   * @param source the log line with trailing whitespace
   * @return a string with all c-style escape sequences escaped, except trailing whitespace
   */
  static std::string escapeLogLine(absl::string_view source);

private:
  friend class SinkDelegate;

  DelegatingLogSink() = default;

  void setDelegate(SinkDelegate* sink) {
    absl::WriterMutexLock lock(&sink_mutex_);
    sink_ = sink;
  }
  SinkDelegate* delegate() {
    absl::ReaderMutexLock lock(&sink_mutex_);
    return sink_;
  }
  SinkDelegate** tlsSink();
  void setTlsDelegate(SinkDelegate* sink);
  SinkDelegate* tlsDelegate();

  SinkDelegate* sink_ ABSL_GUARDED_BY(sink_mutex_){nullptr};
  absl::Mutex sink_mutex_;
  std::unique_ptr<StderrSinkDelegate> stderr_sink_; // Builtin sink to use as a last resort.
  std::unique_ptr<spdlog::formatter> formatter_ ABSL_GUARDED_BY(format_mutex_);
  absl::Mutex format_mutex_;
  bool should_escape_{false};
};

enum class LoggerMode { Envoy, FineGrainLog };

/**
 * Defines a scope for the logging system with the specified lock and log level.
 * This is equivalent to setLogLevel, setLogFormat, and setLock, which can be
 * called individually as well, e.g. to set the log level without changing the
 * lock or format.
 *
 * Contexts can be nested. When a nested context is destroyed, the previous
 * context is restored. When all contexts are destroyed, the lock is cleared,
 * and logging will remain unlocked, the same state it is in prior to
 * instantiating a Context.
 *
 * Settings for Fine-Grain Logger, a file level logger without explicit implementation
 * of Envoy::Logger:Loggable, are integrated here, as they should be updated when
 * context switch occurs.
 */
class Context {
public:
  Context(spdlog::level::level_enum log_level, const std::string& log_format,
          Thread::BasicLockable& lock, bool should_escape, bool enable_fine_grain_logging = false);
  ~Context();

  /**
   * Same as before, with boolean returned to use in log macros.
   */
  static bool useFineGrainLogger();

  // Change the log level for all loggers (fine grained or otherwise) to the level provided.
  static void changeAllLogLevels(spdlog::level::level_enum level);

  static void enableFineGrainLogger();
  static void disableFineGrainLogger();

  static std::string getFineGrainLogFormat();
  static spdlog::level::level_enum getFineGrainDefaultLevel();

private:
  void activate();

  const spdlog::level::level_enum log_level_;
  const std::string log_format_;
  Thread::BasicLockable& lock_;
  bool should_escape_;
  bool enable_fine_grain_logging_;
  Context* const save_context_;

  std::string fine_grain_log_format_;
  spdlog::level::level_enum fine_grain_default_level_ = spdlog::level::info;
};

/**
 * A registry of all named loggers in envoy. Usable for adjusting levels of each logger
 * individually.
 */
class Registry {
public:
  /**
   * @param id supplies the fixed ID of the logger to create.
   * @return spdlog::logger& a logger with system specified sinks for a given ID.
   */
  static spdlog::logger& getLog(Id id);

  /**
   * @return the singleton sink to use for all loggers.
   */
  static DelegatingLogSinkSharedPtr getSink() {
    static DelegatingLogSinkSharedPtr sink = DelegatingLogSink::init();
    return sink;
  }

  /**
   * Sets the minimum log severity required to print messages.
   * Messages below this loglevel will be suppressed.
   */
  static void setLogLevel(spdlog::level::level_enum log_level);

  /**
   * Sets the log format.
   */
  static void setLogFormat(const std::string& log_format);

  /**
   * Sets the log format from a struct as a JSON string.
   */
  static absl::Status setJsonLogFormat(const Protobuf::Message& log_format_struct);

  /**
   * @return true if JSON log format was set using setJsonLogFormat.
   */
  static bool jsonLogFormatSet() { return json_log_format_set_; }

  /**
   * @return std::vector<Logger>& the installed loggers.
   */
  static std::vector<Logger>& loggers() { return allLoggers(); }

  /**
   * @Return bool whether the registry has been initialized.
   */
  static bool initialized() { return getSink()->hasLock(); }

  static Logger* logger(const std::string& log_name);

private:
  /*
   * @return std::vector<Logger>& return the installed loggers.
   */
  static std::vector<Logger>& allLoggers();

  static bool json_log_format_set_;
};

/**
 * Mixin class that allows any class to perform logging with a logger of a particular ID.
 */
template <Id id> class Loggable {
protected:
  /**
   * Do not use this directly, use macros defined below.
   * See source/docs/logging.md for more details.
   * @return spdlog::logger& the static log instance to use for class local logging.
   */
  static spdlog::logger& __log_do_not_use_read_comment() { // NOLINT(readability-identifier-naming)
    static spdlog::logger& instance = Registry::getLog(id);
    return instance;
  }
};

namespace Utility {

constexpr static absl::string_view TagsPrefix = "[Tags: ";
constexpr static absl::string_view TagsSuffix = "] ";
constexpr static absl::string_view TagsSuffixForSearch = "\"] ";

/**
 * Sets the log format for a specific logger.
 */
void setLogFormatForLogger(spdlog::logger& logger, const std::string& log_format);

/**
 * Serializes custom log tags to a string that will be prepended to the log message.
 * In case JSON logging is enabled, the keys and values will be serialized with JSON escaping.
 */
std::string serializeLogTags(const std::map<std::string, std::string>& tags);

/**
 * Escapes the payload to a JSON string and writes the output to the destination buffer.
 */
void escapeMessageJsonString(absl::string_view payload, spdlog::memory_buf_t& dest);

} // namespace Utility

// Contains custom flags to introduce user defined flags in log pattern. Reference:
// https://github.com/gabime/spdlog#user-defined-flags-in-the-log-pattern.
namespace CustomFlagFormatter {

/**
 * When added to a formatter, this adds '_' as a user defined flag in the log pattern that escapes
 * newlines.
 */
class EscapeMessageNewLine : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg& msg, const std::tm& tm,
              spdlog::memory_buf_t& dest) override;

  std::unique_ptr<custom_flag_formatter> clone() const override {
    return spdlog::details::make_unique<EscapeMessageNewLine>();
  }

  constexpr static char Placeholder = '_';

private:
  using ReplacementMap = absl::flat_hash_map<std::string, std::string>;
  const static ReplacementMap& replacements() {
    CONSTRUCT_ON_FIRST_USE(ReplacementMap, ReplacementMap{{"\n", "\\n"}});
  }
};

/**
 * When added to a formatter, this adds 'j' as a user defined flag in the log pattern that makes a
 * log payload message a valid JSON escaped string.
 */
class EscapeMessageJsonString : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg& msg, const std::tm& tm,
              spdlog::memory_buf_t& dest) override;

  std::unique_ptr<custom_flag_formatter> clone() const override {
    return spdlog::details::make_unique<EscapeMessageJsonString>();
  }

  constexpr static char Placeholder = 'j';
};

class ExtractedTags : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg& msg, const std::tm& tm,
              spdlog::memory_buf_t& dest) override;

  std::unique_ptr<custom_flag_formatter> clone() const override {
    return spdlog::details::make_unique<ExtractedTags>();
  }

  constexpr static char Placeholder = '*';
  constexpr static char JsonPropertyDeimilter = ',';
};

class ExtractedMessage : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg& msg, const std::tm& tm,
              spdlog::memory_buf_t& dest) override;

  std::unique_ptr<custom_flag_formatter> clone() const override {
    return spdlog::details::make_unique<ExtractedMessage>();
  }

  constexpr static char Placeholder = '+';
};

} // namespace CustomFlagFormatter
} // namespace Logger

/**
 * Base logging macros. It is expected that users will use the convenience macros below rather than
 * invoke these directly.
 */

#define ENVOY_SPDLOG_LEVEL(LEVEL)                                                                  \
  (static_cast<spdlog::level::level_enum>(Envoy::Logger::Logger::LEVEL))

#define ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL) (ENVOY_SPDLOG_LEVEL(LEVEL) >= (LOGGER).level())

// Compare levels before invoking logger. This is an optimization to avoid
// executing expressions computing log contents when they would be suppressed.
// The same filtering will also occur in spdlog::logger.
#define ENVOY_LOG_COMP_AND_LOG(LOGGER, LEVEL, ...)                                                 \
  do {                                                                                             \
    if (Envoy::Logger::should_log && ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                        \
      LOGGER.log(::spdlog::source_loc{__FILE__, __LINE__, __func__}, ENVOY_SPDLOG_LEVEL(LEVEL),    \
                 __VA_ARGS__);                                                                     \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_CHECK_LEVEL(LEVEL) ENVOY_LOG_COMP_LEVEL(ENVOY_LOGGER(), LEVEL)

/**
 * Convenience macro to log to a user-specified logger. When fine-grain logging is used, the
 * specific logger is ignored and instead the file-specific logger is used.
 */
#define ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ...)                                                    \
  do {                                                                                             \
    if (Envoy::Logger::should_log && Envoy::Logger::Context::useFineGrainLogger()) {               \
      FINE_GRAIN_LOG(LEVEL, ##__VA_ARGS__);                                                        \
    } else {                                                                                       \
      ENVOY_LOG_COMP_AND_LOG(LOGGER, LEVEL, ##__VA_ARGS__);                                        \
    }                                                                                              \
  } while (0)

/**
 * Convenience macro to get logger.
 */
#define ENVOY_LOGGER() __log_do_not_use_read_comment()

/**
 * Convenience macro to log to the misc logger, which allows for logging without of direct access to
 * a logger.
 */
#define GET_MISC_LOGGER() ::Envoy::Logger::Registry::getLog(::Envoy::Logger::Id::misc)
#define ENVOY_LOG_MISC(LEVEL, ...) ENVOY_LOG_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, ##__VA_ARGS__)

// TODO(danielhochman): macros(s)/function(s) for logging structures that support iteration.

/**
 * Command line options for log macros: use Fine-Grain Logger or not.
 */
#define ENVOY_LOG(LEVEL, ...) ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, ##__VA_ARGS__)

#define ENVOY_TAGGED_LOG_TO_LOGGER(LOGGER, LEVEL, TAGS, FORMAT, ...)                               \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL,                                                           \
                          fmt::runtime(::Envoy::Logger::Utility::serializeLogTags(TAGS) + FORMAT), \
                          ##__VA_ARGS__);                                                          \
    }                                                                                              \
  } while (0)

#define ENVOY_TAGGED_CONN_LOG_TO_LOGGER(LOGGER, LEVEL, TAGS, CONNECTION, FORMAT, ...)              \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      std::map<std::string, std::string> log_tags = TAGS;                                          \
      log_tags.emplace("ConnectionId", std::to_string((CONNECTION).id()));                         \
      ENVOY_LOG_TO_LOGGER(                                                                         \
          LOGGER, LEVEL,                                                                           \
          fmt::runtime(::Envoy::Logger::Utility::serializeLogTags(log_tags) + FORMAT),             \
          ##__VA_ARGS__);                                                                          \
    }                                                                                              \
  } while (0)

#define ENVOY_TAGGED_STREAM_LOG_TO_LOGGER(LOGGER, LEVEL, TAGS, STREAM, FORMAT, ...)                \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      std::map<std::string, std::string> log_tags = TAGS;                                          \
      log_tags.emplace("ConnectionId",                                                             \
                       (STREAM).connection() ? std::to_string((STREAM).connection()->id()) : "0"); \
      log_tags.emplace("StreamId", std::to_string((STREAM).streamId()));                           \
      ENVOY_LOG_TO_LOGGER(                                                                         \
          LOGGER, LEVEL,                                                                           \
          fmt::runtime(::Envoy::Logger::Utility::serializeLogTags(log_tags) + FORMAT),             \
          ##__VA_ARGS__);                                                                          \
    }                                                                                              \
  } while (0)

#define ENVOY_CONN_LOG_TO_LOGGER(LOGGER, LEVEL, FORMAT, CONNECTION, ...)                           \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      std::map<std::string, std::string> log_tags;                                                 \
      log_tags.emplace("ConnectionId", std::to_string((CONNECTION).id()));                         \
      ENVOY_LOG_TO_LOGGER(                                                                         \
          LOGGER, LEVEL,                                                                           \
          fmt::runtime(::Envoy::Logger::Utility::serializeLogTags(log_tags) + FORMAT),             \
          ##__VA_ARGS__);                                                                          \
    }                                                                                              \
  } while (0)

#define ENVOY_STREAM_LOG_TO_LOGGER(LOGGER, LEVEL, FORMAT, STREAM, ...)                             \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      std::map<std::string, std::string> log_tags;                                                 \
      log_tags.emplace("ConnectionId",                                                             \
                       (STREAM).connection() ? std::to_string((STREAM).connection()->id()) : "0"); \
      log_tags.emplace("StreamId", std::to_string((STREAM).streamId()));                           \
      ENVOY_LOG_TO_LOGGER(                                                                         \
          LOGGER, LEVEL,                                                                           \
          fmt::runtime(::Envoy::Logger::Utility::serializeLogTags(log_tags) + FORMAT),             \
          ##__VA_ARGS__);                                                                          \
    }                                                                                              \
  } while (0)

/**
 * Log with tags which are a map of key and value strings. When ENVOY_TAGGED_LOG is used, the tags
 * are serialized and prepended to the log message.
 * For example, the map {{"key1","val1","key2","val2"}} would be serialized to:
 * [Tags: "key1":"val1","key2":"val2"]. The serialization pattern is defined by
 * Envoy::Logger::Utility::serializeLogTags function.
 */
#define ENVOY_TAGGED_LOG(LEVEL, TAGS, FORMAT, ...)                                                 \
  ENVOY_TAGGED_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, TAGS, FORMAT, ##__VA_ARGS__)

/**
 * Log with tags which are a map of key and value strings. Has the same operation as
 * ENVOY_TAGGED_LOG, only this macro will also add the connection ID to the tags, if the provided
 * tags do not already have a ConnectionId key existing.
 */
#define ENVOY_TAGGED_CONN_LOG(LEVEL, TAGS, CONNECTION, FORMAT, ...)                                \
  ENVOY_TAGGED_CONN_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, TAGS, CONNECTION, FORMAT, ##__VA_ARGS__)

/**
 * Log with tags which are a map of key and value strings. Has the same operation as
 * ENVOY_TAGGED_LOG, only this macro will also add the connection and stream ID to the tags,
 * if the provided tags do not already have a ConnectionId or StreamId keys existing.
 */
#define ENVOY_TAGGED_STREAM_LOG(LEVEL, TAGS, STREAM, FORMAT, ...)                                  \
  ENVOY_TAGGED_STREAM_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, TAGS, STREAM, FORMAT, ##__VA_ARGS__)

#define ENVOY_CONN_LOG(LEVEL, FORMAT, CONNECTION, ...)                                             \
  ENVOY_CONN_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, FORMAT, CONNECTION, ##__VA_ARGS__);

#define ENVOY_STREAM_LOG(LEVEL, FORMAT, STREAM, ...)                                               \
  ENVOY_STREAM_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, FORMAT, STREAM, ##__VA_ARGS__);

/**
 * Log with a stable event name. This allows emitting a log line with a stable name in addition to
 * the standard log line. The stable log line is passed to custom sinks that may want to intercept
 * these log messages.
 *
 * By default these named logs are not handled, but a custom log sink may intercept them by
 * implementing the logWithStableName function.
 */
#define ENVOY_LOG_EVENT(LEVEL, EVENT_NAME, ...)                                                    \
  ENVOY_LOG_EVENT_TO_LOGGER(ENVOY_LOGGER(), LEVEL, EVENT_NAME, ##__VA_ARGS__)

#define ENVOY_LOG_EVENT_TO_LOGGER(LOGGER, LEVEL, EVENT_NAME, ...)                                  \
  do {                                                                                             \
    ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      ::Envoy::Logger::Registry::getSink()->logWithStableName(EVENT_NAME, #LEVEL, (LOGGER).name(), \
                                                              ##__VA_ARGS__);                      \
    }                                                                                              \
  } while (0)

#define ENVOY_CONN_LOG_EVENT(LEVEL, EVENT_NAME, FORMAT, CONNECTION, ...)                           \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(ENVOY_LOGGER(), LEVEL)) {                                             \
      std::map<std::string, std::string> log_tags;                                                 \
      log_tags.emplace("ConnectionId", std::to_string((CONNECTION).id()));                         \
      const auto combined_format = ::Envoy::Logger::Utility::serializeLogTags(log_tags) + FORMAT;  \
      ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, fmt::runtime(combined_format), ##__VA_ARGS__);    \
      ::Envoy::Logger::Registry::getSink()->logWithStableName(                                     \
          EVENT_NAME, #LEVEL, (ENVOY_LOGGER()).name(), combined_format, ##__VA_ARGS__);            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_FIRST_N_TO_LOGGER(LOGGER, LEVEL, N, ...)                                         \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      static auto* countdown = new std::atomic<uint64_t>();                                        \
      if (countdown->fetch_add(1) < N) {                                                           \
        ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                         \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_FIRST_N_TO_LOGGER_IF(LOGGER, LEVEL, N, CONDITION, ...)                           \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL) && (CONDITION)) {                                      \
      static auto* countdown = new std::atomic<uint64_t>();                                        \
      if (countdown->fetch_add(1) < N) {                                                           \
        ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                         \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_FIRST_N(LEVEL, N, ...)                                                           \
  ENVOY_LOG_FIRST_N_TO_LOGGER(ENVOY_LOGGER(), LEVEL, N, ##__VA_ARGS__)

#define ENVOY_LOG_FIRST_N_IF(LEVEL, N, CONDITION, ...)                                             \
  ENVOY_LOG_FIRST_N_TO_LOGGER_IF(ENVOY_LOGGER(), LEVEL, N, (CONDITION), ##__VA_ARGS__)

#define ENVOY_LOG_FIRST_N_MISC(LEVEL, N, ...)                                                      \
  ENVOY_LOG_FIRST_N_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, N, ##__VA_ARGS__)

#define ENVOY_LOG_FIRST_N_MISC_IF(LEVEL, N, CONDITION, ...)                                        \
  ENVOY_LOG_FIRST_N_TO_LOGGER_IF(GET_MISC_LOGGER(), LEVEL, N, (CONDITION), ##__VA_ARGS__)

#define ENVOY_LOG_ONCE_TO_LOGGER(LOGGER, LEVEL, ...)                                               \
  ENVOY_LOG_FIRST_N_TO_LOGGER(LOGGER, LEVEL, 1, ##__VA_ARGS__)

#define ENVOY_LOG_ONCE_TO_LOGGER_IF(LOGGER, LEVEL, CONDITION, ...)                                 \
  ENVOY_LOG_FIRST_N_TO_LOGGER_IF(LOGGER, LEVEL, 1, (CONDITION), ##__VA_ARGS__)

#define ENVOY_LOG_ONCE(LEVEL, ...) ENVOY_LOG_ONCE_TO_LOGGER(ENVOY_LOGGER(), LEVEL, ##__VA_ARGS__)

#define ENVOY_LOG_ONCE_IF(LEVEL, CONDITION, ...)                                                   \
  ENVOY_LOG_ONCE_TO_LOGGER_IF(ENVOY_LOGGER(), LEVEL, (CONDITION), ##__VA_ARGS__)

#define ENVOY_LOG_ONCE_MISC(LEVEL, ...)                                                            \
  ENVOY_LOG_ONCE_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, ##__VA_ARGS__)

#define ENVOY_LOG_ONCE_MISC_IF(LEVEL, CONDITION, ...)                                              \
  ENVOY_LOG_ONCE_TO_LOGGER_IF(GET_MISC_LOGGER(), LEVEL, (CONDITION), ##__VA_ARGS__)

#define ENVOY_LOG_EVERY_NTH_TO_LOGGER(LOGGER, LEVEL, N, ...)                                       \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      static auto* count = new std::atomic<uint64_t>();                                            \
      if ((count->fetch_add(1) % N) == 0) {                                                        \
        ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                         \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_EVERY_NTH(LEVEL, N, ...)                                                         \
  ENVOY_LOG_EVERY_NTH_TO_LOGGER(ENVOY_LOGGER(), LEVEL, N, ##__VA_ARGS__)

#define ENVOY_LOG_EVERY_NTH_MISC(LEVEL, N, ...)                                                    \
  ENVOY_LOG_EVERY_NTH_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, N, ##__VA_ARGS__)

#define ENVOY_LOG_EVERY_POW_2_TO_LOGGER(LOGGER, LEVEL, ...)                                        \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      static auto* count = new std::atomic<uint64_t>();                                            \
      if (std::bitset<64>(1 /* for the first hit*/ + count->fetch_add(1)).count() == 1) {          \
        ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                         \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_EVERY_POW_2(LEVEL, ...)                                                          \
  ENVOY_LOG_EVERY_POW_2_TO_LOGGER(ENVOY_LOGGER(), LEVEL, ##__VA_ARGS__)

#define ENVOY_LOG_EVERY_POW_2_MISC(LEVEL, ...)                                                     \
  ENVOY_LOG_EVERY_POW_2_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, ##__VA_ARGS__)

// This is to get us to pass the format check. We reference a real-world time source here.
// We'd have to introduce a singleton for a time source here, and consensus was that avoiding
// that is preferable.
using t_logclock = std::chrono::steady_clock; // NOLINT

#define ENVOY_LOG_PERIODIC_TO_LOGGER(LOGGER, LEVEL, CHRONO_DURATION, ...)                          \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      static auto* last_hit = new std::atomic<int64_t>();                                          \
      auto last = last_hit->load();                                                                \
      const auto now = t_logclock::now().time_since_epoch().count();                               \
      if ((now - last) >                                                                           \
              std::chrono::duration_cast<std::chrono::nanoseconds>(CHRONO_DURATION).count() &&     \
          last_hit->compare_exchange_strong(last, now)) {                                          \
        ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ##__VA_ARGS__);                                         \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_PERIODIC(LEVEL, CHRONO_DURATION, ...)                                            \
  ENVOY_LOG_PERIODIC_TO_LOGGER(ENVOY_LOGGER(), LEVEL, CHRONO_DURATION, ##__VA_ARGS__)

#define ENVOY_LOG_PERIODIC_MISC(LEVEL, CHRONO_DURATION, ...)                                       \
  ENVOY_LOG_PERIODIC_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, CHRONO_DURATION, ##__VA_ARGS__)

#define ENVOY_FLUSH_LOG()                                                                          \
  do {                                                                                             \
    if (Envoy::Logger::Context::useFineGrainLogger()) {                                            \
      FINE_GRAIN_FLUSH_LOG();                                                                      \
    } else {                                                                                       \
      ENVOY_LOGGER().flush();                                                                      \
    }                                                                                              \
  } while (0)

} // namespace Envoy
