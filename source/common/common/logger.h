#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "common/common/base_logger.h"
#include "common/common/fmt.h"
#include "common/common/logger_impl.h"
#include "common/common/macros.h"
#include "common/common/non_copyable.h"

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "fmt/ostream.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

// TODO: find out a way for extensions to register new logger IDs
#define ALL_LOGGER_IDS(FUNCTION)                                                                   \
  FUNCTION(admin)                                                                                  \
  FUNCTION(aws)                                                                                    \
  FUNCTION(assert)                                                                                 \
  FUNCTION(backtrace)                                                                              \
  FUNCTION(cache_filter)                                                                           \
  FUNCTION(client)                                                                                 \
  FUNCTION(config)                                                                                 \
  FUNCTION(connection)                                                                             \
  FUNCTION(conn_handler)                                                                           \
  FUNCTION(decompression)                                                                          \
  FUNCTION(dubbo)                                                                                  \
  FUNCTION(file)                                                                                   \
  FUNCTION(filter)                                                                                 \
  FUNCTION(forward_proxy)                                                                          \
  FUNCTION(grpc)                                                                                   \
  FUNCTION(hc)                                                                                     \
  FUNCTION(health_checker)                                                                         \
  FUNCTION(http)                                                                                   \
  FUNCTION(http2)                                                                                  \
  FUNCTION(hystrix)                                                                                \
  FUNCTION(init)                                                                                   \
  FUNCTION(io)                                                                                     \
  FUNCTION(jwt)                                                                                    \
  FUNCTION(kafka)                                                                                  \
  FUNCTION(lua)                                                                                    \
  FUNCTION(main)                                                                                   \
  FUNCTION(misc)                                                                                   \
  FUNCTION(mongo)                                                                                  \
  FUNCTION(quic)                                                                                   \
  FUNCTION(quic_stream)                                                                            \
  FUNCTION(pool)                                                                                   \
  FUNCTION(rbac)                                                                                   \
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
  FUNCTION(wasm)

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

  virtual void log(absl::string_view msg) PURE;
  virtual void flush() PURE;

protected:
  SinkDelegate* previous_delegate() { return previous_delegate_; }

private:
  SinkDelegate* previous_delegate_;
  DelegatingLogSinkSharedPtr log_sink_;
};

/**
 * SinkDelegate that writes log messages to stderr.
 */
class StderrSinkDelegate : public SinkDelegate {
public:
  explicit StderrSinkDelegate(DelegatingLogSinkSharedPtr log_sink);

  // SinkDelegate
  void log(absl::string_view msg) override;
  void flush() override;

  bool hasLock() const { return lock_ != nullptr; }
  void setLock(Thread::BasicLockable& lock) { lock_ = &lock; }
  void clearLock() { lock_ = nullptr; }
  Thread::BasicLockable* lock() { return lock_; }

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

  // spdlog::sinks::sink
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override { sink_->flush(); }
  void set_pattern(const std::string& pattern) override {
    set_formatter(spdlog::details::make_unique<spdlog::pattern_formatter>(pattern));
  }
  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override;
  void set_should_escape(bool should_escape) { should_escape_ = should_escape; }

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

  void setDelegate(SinkDelegate* sink) { sink_ = sink; }
  SinkDelegate* delegate() { return sink_; }

  SinkDelegate* sink_{nullptr};
  std::unique_ptr<StderrSinkDelegate> stderr_sink_; // Builtin sink to use as a last resort.
  std::unique_ptr<spdlog::formatter> formatter_ ABSL_GUARDED_BY(format_mutex_);
  absl::Mutex format_mutex_; // direct absl reference to break build cycle.
  bool should_escape_{false};
};

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
 */
class Context {
public:
  Context(spdlog::level::level_enum log_level, const std::string& log_format,
          Thread::BasicLockable& lock, bool should_escape);
  ~Context();

private:
  void activate();

  const spdlog::level::level_enum log_level_;
  const std::string log_format_;
  Thread::BasicLockable& lock_;
  bool should_escape_;
  Context* const save_context_;
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
};

/**
 * Mixin class that allows any class to perform logging with a logger of a particular ID.
 */
template <Id id> class Loggable {
protected:
  /**
   * Do not use this directly, use macros defined below.
   * @return spdlog::logger& the static log instance to use for class local logging.
   */
  static spdlog::logger& __log_do_not_use_read_comment() {
    static spdlog::logger& instance = Registry::getLog(id);
    return instance;
  }
};

} // namespace Logger

// Convert the line macro to a string literal for concatenation in log macros.
#define DO_STRINGIZE(x) STRINGIZE(x)
#define STRINGIZE(x) #x
#define LINE_STRING DO_STRINGIZE(__LINE__)
#define LOG_PREFIX "[" __FILE__ ":" LINE_STRING "] "

/**
 * Base logging macros. It is expected that users will use the convenience macros below rather than
 * invoke these directly.
 */

#define ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)                                                        \
  (static_cast<spdlog::level::level_enum>(Envoy::Logger::Logger::LEVEL) >= LOGGER.level())

// Compare levels before invoking logger. This is an optimization to avoid
// executing expressions computing log contents when they would be suppressed.
// The same filtering will also occur in spdlog::logger.
#define ENVOY_LOG_COMP_AND_LOG(LOGGER, LEVEL, ...)                                                 \
  do {                                                                                             \
    if (ENVOY_LOG_COMP_LEVEL(LOGGER, LEVEL)) {                                                     \
      LOGGER.LEVEL(LOG_PREFIX __VA_ARGS__);                                                        \
    }                                                                                              \
  } while (0)

#define ENVOY_LOG_CHECK_LEVEL(LEVEL) ENVOY_LOG_COMP_LEVEL(ENVOY_LOGGER(), LEVEL)

/**
 * Convenience macro to log to a user-specified logger.
 * Maps directly to ENVOY_LOG_COMP_AND_LOG - it could contain macro logic itself, without
 * redirection, but left in case various implementations are required in the future (based on log
 * level for example).
 */
#define ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, LEVEL, ##__VA_ARGS__)

/**
 * Convenience macro to get logger.
 */
#define ENVOY_LOGGER() __log_do_not_use_read_comment()

/**
 * Convenience macro to flush logger.
 */
#define ENVOY_FLUSH_LOG() ENVOY_LOGGER().flush()

/**
 * Convenience macro to log to the class' logger.
 */
#define ENVOY_LOG(LEVEL, ...) ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, ##__VA_ARGS__)

/**
 * Convenience macro to log to the misc logger, which allows for logging without of direct access to
 * a logger.
 */
#define GET_MISC_LOGGER() Logger::Registry::getLog(Logger::Id::misc)
#define ENVOY_LOG_MISC(LEVEL, ...) ENVOY_LOG_TO_LOGGER(GET_MISC_LOGGER(), LEVEL, ##__VA_ARGS__)

/**
 * Convenience macros for logging with connection ID.
 */
#define ENVOY_CONN_LOG_TO_LOGGER(LOGGER, LEVEL, FORMAT, CONNECTION, ...)                           \
  ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, "[C{}] " FORMAT, (CONNECTION).id(), ##__VA_ARGS__)

#define ENVOY_CONN_LOG(LEVEL, FORMAT, CONNECTION, ...)                                             \
  ENVOY_CONN_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, FORMAT, CONNECTION, ##__VA_ARGS__)

/**
 * Convenience macros for logging with a stream ID and a connection ID.
 */
#define ENVOY_STREAM_LOG_TO_LOGGER(LOGGER, LEVEL, FORMAT, STREAM, ...)                             \
  ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, "[C{}][S{}] " FORMAT,                                         \
                      (STREAM).connection() ? (STREAM).connection()->id() : 0,                     \
                      (STREAM).streamId(), ##__VA_ARGS__)

#define ENVOY_STREAM_LOG(LEVEL, FORMAT, STREAM, ...)                                               \
  ENVOY_STREAM_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, FORMAT, STREAM, ##__VA_ARGS__)

// TODO(danielhochman): macros(s)/function(s) for logging structures that support iteration.

} // namespace Envoy
