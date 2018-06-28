#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/non_copyable.h"

#include "absl/strings/string_view.h"
#include "fmt/ostream.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

// clang-format off
#define ALL_LOGGER_IDS(FUNCTION) \
  FUNCTION(admin)                \
  FUNCTION(assert)               \
  FUNCTION(backtrace)            \
  FUNCTION(client)               \
  FUNCTION(config)               \
  FUNCTION(connection)           \
  FUNCTION(misc)                 \
  FUNCTION(file)                 \
  FUNCTION(filter)               \
  FUNCTION(hc)                   \
  FUNCTION(health_checker)       \
  FUNCTION(http)                 \
  FUNCTION(http2)                \
  FUNCTION(hystrix)              \
  FUNCTION(lua)                  \
  FUNCTION(main)                 \
  FUNCTION(mongo)                \
  FUNCTION(pool)                 \
  FUNCTION(redis)                \
  FUNCTION(router)               \
  FUNCTION(runtime)              \
  FUNCTION(testing)              \
  FUNCTION(tracing)              \
  FUNCTION(upstream)             \
  FUNCTION(grpc)                 \
  FUNCTION(stats)                \
  FUNCTION(thrift)

enum class Id {
  ALL_LOGGER_IDS(GENERATE_ENUM)
};
// clang-format on

/**
 * Logger wrapper for a spdlog logger.
 */
class Logger {
public:
  /* This is simple mapping between Logger severity levels and spdlog severity levels.
   * The only reason for this mapping is to go around the fact that spdlog defines level as err
   * but the method to log at err level is called LOGGER.error not LOGGER.err. All other level are
   * fine spdlog::info corresponds to LOGGER.info method.
   */
  typedef enum {
    trace = spdlog::level::trace,
    debug = spdlog::level::debug,
    info = spdlog::level::info,
    warn = spdlog::level::warn,
    error = spdlog::level::err,
    critical = spdlog::level::critical,
    off = spdlog::level::off
  } levels;

  std::string levelString() const { return spdlog::level::level_names[logger_->level()]; }
  std::string name() const { return logger_->name(); }
  void setLevel(spdlog::level::level_enum level) { logger_->set_level(level); }
  spdlog::level::level_enum level() const { return logger_->level(); }

  static const char* DEFAULT_LOG_FORMAT;

private:
  Logger(const std::string& name);

  std::shared_ptr<spdlog::logger> logger_; // Use shared_ptr here to allow static construction
                                           // of constant vector below.
  friend class Registry;
};

class DelegatingLogSink;
typedef std::shared_ptr<DelegatingLogSink> DelegatingLogSinkPtr;

/**
 * Captures a logging sink that can be delegated to for a bounded amount of time.
 * On destruction, logging is reverted to its previous state. SinkDelegates must
 * be allocated/freed as a stack.
 */
class SinkDelegate : NonCopyable {
public:
  explicit SinkDelegate(DelegatingLogSinkPtr log_sink);
  virtual ~SinkDelegate();

  virtual void log(absl::string_view msg) PURE;
  virtual void flush() PURE;

protected:
  SinkDelegate* previous_delegate() { return previous_delegate_; }

private:
  SinkDelegate* previous_delegate_;
  DelegatingLogSinkPtr log_sink_;
};

/**
 * SinkDelegate that writes log messages to stderr.
 */
class StderrSinkDelegate : public SinkDelegate {
public:
  explicit StderrSinkDelegate(DelegatingLogSinkPtr log_sink);

  // SinkDelegate
  void log(absl::string_view msg) override;
  void flush() override;

  bool hasLock() const { return lock_ != nullptr; }
  void setLock(Thread::BasicLockable& lock) { lock_ = &lock; }

private:
  Thread::BasicLockable* lock_{};
};

/**
 * Stacks logging sinks, so you can temporarily override the logging mechanism, restoring
 * the prevoius state when the DelegatingSink is destructed.
 */
class DelegatingLogSink : public spdlog::sinks::sink {
public:
  void setLock(Thread::BasicLockable& lock) { stderr_sink_->setLock(lock); }

  // spdlog::sinks::sink
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override { sink_->flush(); }

  /**
   * @return bool whether a lock has been established.
   */
  bool hasLock() const { return stderr_sink_->hasLock(); }

  // Constructs a new DelegatingLogSink, sets up the default sink to stderr,
  // and returns a shared_ptr to it. A shared_ptr is required for sinks used
  // in spdlog::logger; it would not otherwise be required in Envoy. This method
  // must own the construction process because StderrSinkDelegate needs access to
  // the DelegatingLogSinkPtr, not just the DelegatingLogSink*, and that is only
  // available after construction.
  static DelegatingLogSinkPtr init();

private:
  friend class SinkDelegate;

  DelegatingLogSink() = default;

  void setDelegate(SinkDelegate* sink) { sink_ = sink; }
  SinkDelegate* delegate() { return sink_; }

  SinkDelegate* sink_{nullptr};
  std::unique_ptr<StderrSinkDelegate> stderr_sink_; // Builtin sink to use as a last resort.
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
  static DelegatingLogSinkPtr getSink() {
    static DelegatingLogSinkPtr sink = DelegatingLogSink::init();
    return sink;
  }

  /*
   * Initialize the logging system with the specified lock and log level.
   * This is equivalalent to setLogLevel, setLogFormat, and setLock, which
   * can be called individually as well, e.g. to set the log level without
   * changing the lock or format.
   */
  static void initialize(spdlog::level::level_enum log_level, const std::string& log_format,
                         Thread::BasicLockable& lock);

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

private:
  /*
   * @return std::vector<Logger>& return the installed loggers.
   */
  static std::vector<Logger>& allLoggers();
};

/**
 * Mixin class that allows any class to peform logging with a logger of a particular ID.
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

} // Logger

// Convert the line macro to a string literal for concatenation in log macros.
#define DO_STRINGIZE(x) STRINGIZE(x)
#define STRINGIZE(x) #x
#define LINE_STRING DO_STRINGIZE(__LINE__)
#define LOG_PREFIX __FILE__ ":" LINE_STRING "] "

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
