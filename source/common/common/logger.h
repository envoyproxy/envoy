#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

#include "common/common/fmt.h"
#include "common/common/macros.h"

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
  FUNCTION(http)                 \
  FUNCTION(http2)                \
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
  FUNCTION(grpc)

enum class Id {
  ALL_LOGGER_IDS(GENERATE_ENUM)
};
// clang-format on

/**
 * Logger wrapper for a spdlog logger.
 */
class Logger {
public:
  std::string levelString() const { return spdlog::level::level_names[logger_->level()]; }
  std::string name() const { return logger_->name(); }
  void setLevel(spdlog::level::level_enum level) const { logger_->set_level(level); }

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

private:
  Logger(const std::string& name);

  std::shared_ptr<spdlog::logger> logger_; // Use shared_ptr here to allow static construction
                                           // of constant vector below.
  friend class Registry;
};

/**
 * An optionally locking stderr or file logging sink.
 *
 * This sink outputs to either stderr or to a file. It shares both implementations (instead of
 * being two separate classes) because we can't setup file logging until after the AccessLogManager
 * is available, but by that time some loggers have cached their logger from the registry already,
 * so we need to be able switch implementations without replacing the object.
 */
class LockingStderrOrFileSink : public spdlog::sinks::sink {
public:
  void setLock(Thread::BasicLockable& lock) { lock_ = &lock; }

  /**
   * Configure this object to log to stderr.
   *
   * @note This method is not thread-safe and can only be called when no other threads
   * are logging.
   */
  void logToStdErr();

  /**
   * Configure this object to log to a file.
   *
   * @note This method is not thread-safe and can only be called when no other threads
   * are logging.
   */
  void logToFile(const std::string& log_path, AccessLog::AccessLogManager& log_manager);

  // spdlog::sinks::sink
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override;

  /**
   * @return bool whether a lock has been established.
   */
  bool hasLock() const { return lock_ != nullptr; }

private:
  Thread::BasicLockable* lock_{};
  Filesystem::FileSharedPtr log_file_;
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
  static std::shared_ptr<LockingStderrOrFileSink> getSink() {
    static std::shared_ptr<LockingStderrOrFileSink> sink(new LockingStderrOrFileSink());
    return sink;
  }

  /**
   * Initialize the logging system from server options.
   */
  static void initialize(uint64_t log_level, Thread::BasicLockable& lock);

  /**
   * @return const std::vector<Logger>& the installed loggers.
   */
  static const std::vector<Logger>& loggers() { return allLoggers(); }

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
// Compare levels before invoking logger
#define ENVOY_LOG_COMP_AND_LOG(LOGGER, LEVEL, ...)                                                 \
  do {                                                                                             \
    if (static_cast<spdlog::level::level_enum>(Envoy::Logger::Logger::LEVEL) >= LOGGER.level()) {  \
      LOGGER.LEVEL(LOG_PREFIX __VA_ARGS__);                                                        \
    }                                                                                              \
  } while (0)

#ifdef NVLOG
#define ENVOY_LOG_trace_TO_LOGGER(LOGGER, ...)
#define ENVOY_LOG_debug_TO_LOGGER(LOGGER, ...)
#else
#define ENVOY_LOG_trace_TO_LOGGER(LOGGER, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, trace, ##__VA_ARGS__)
#define ENVOY_LOG_debug_TO_LOGGER(LOGGER, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, debug, ##__VA_ARGS__)
#endif

#define ENVOY_LOG_info_TO_LOGGER(LOGGER, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, info, ##__VA_ARGS__)
#define ENVOY_LOG_warn_TO_LOGGER(LOGGER, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, warn, ##__VA_ARGS__)
#define ENVOY_LOG_error_TO_LOGGER(LOGGER, ...) ENVOY_LOG_COMP_AND_LOG(LOGGER, error, ##__VA_ARGS__)
#define ENVOY_LOG_critical_TO_LOGGER(LOGGER, ...)                                                  \
  ENVOY_LOG_COMP_AND_LOG(LOGGER, critical, ##__VA_ARGS__)

/**
 * Convenience macro to log to a user-specified logger.
 */
#define ENVOY_LOG_TO_LOGGER(LOGGER, LEVEL, ...) ENVOY_LOG_##LEVEL##_TO_LOGGER(LOGGER, ##__VA_ARGS__)

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
