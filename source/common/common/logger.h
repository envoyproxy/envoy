#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

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
  FUNCTION(misc)              \
  FUNCTION(file)                 \
  FUNCTION(filter)               \
  FUNCTION(hc)                   \
  FUNCTION(http)                 \
  FUNCTION(http2)                \
  FUNCTION(main)                 \
  FUNCTION(mongo)                \
  FUNCTION(pool)                 \
  FUNCTION(redis)                \
  FUNCTION(router)               \
  FUNCTION(runtime)              \
  FUNCTION(testing)              \
  FUNCTION(upstream)

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

private:
  Logger(const std::string& name);

  std::shared_ptr<spdlog::logger> logger_; // Use shared_ptr here to allow static construction
                                           // of constant vector below.
  friend class Registry;
};

/**
 * An optionally locking stderr logging sink.
 */
class LockingStderrSink : public spdlog::sinks::sink {
public:
  void setLock(Thread::BasicLockable& lock) { lock_ = &lock; }

  // spdlog::sinks::sink
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override;

private:
  Thread::BasicLockable* lock_{};
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
  static std::shared_ptr<LockingStderrSink> getSink() {
    static std::shared_ptr<LockingStderrSink> sink(new LockingStderrSink());
    return sink;
  }

  /**
   * Initialize the logging system from server options.
   */
  static void initialize(uint64_t log_level, Thread::BasicLockable& lock);

  /**
   * @return const std::vector<Logger>& the installed loggers.
   */
  static const std::vector<Logger>& loggers() { return all_loggers_; }

private:
  static std::vector<Logger> all_loggers_;
};

/**
 * Mixin class that allows any class to peform logging with a logger of a particular ID.
 */
template <Id id> class Loggable {
protected:
  /**
   * @return spdlog::logger& the static log instance to use for class local logging.
   */
  static spdlog::logger& log() {
    static spdlog::logger& instance = Registry::getLog(id);
    return instance;
  }
};

} // Logger

/**
 * Base logging macros.  It is expected that users will use the convenience macros below rather than
 * invoke these directly.
 */
#ifdef NDEBUG
#define log_trace_to_logger(LOGGER, ...)
#define log_debug_to_logger(LOGGER, ...)
#else
#define log_trace_to_logger(LOGGER, ...) LOGGER.trace(__VA_ARGS__)
#define log_debug_to_logger(LOGGER, ...) LOGGER.debug(__VA_ARGS__)
#endif

#define log_info_to_logger(LOGGER, ...) LOGGER.info(__VA_ARGS__)
#define log_warn_to_logger(LOGGER, ...) LOGGER.warn(__VA_ARGS__)
#define log_err_to_logger(LOGGER, ...) LOGGER.err(__VA_ARGS__)
#define log_critical_to_logger(LOGGER, ...) LOGGER.critical(__VA_ARGS__)

/**
 * Convenience macro to log to a user-specified logger.
 */
#define log_to_logger(LOGGER, LEVEL, ...) log_##LEVEL##_to_logger(LOGGER, ##__VA_ARGS__)

/**
 * Convenience macro to log to the facility of the class in which the macro is invoked. Note:
 * facilities are a way of grouping classes within envoy. A facility is specified by Id enum values
 * listed above. The facility_log is enabled within a class when the class inherits from the
 * Loggable class templated by the choice of facility.
 */
#define log_facility(LEVEL, ...) log_to_logger(log(), LEVEL, ##__VA_ARGS__)

/**
 * Convenience macro to log to the misc logger, which allows for logging without of direct access to
 * a logger.
 */
#define get_misc_logger() Logger::Registry::getLog(Logger::Id::misc)
#define log_misc(LEVEL, ...) log_to_logger(get_misc_logger(), LEVEL, ##__VA_ARGS__)

/**
 * Convenience macros for logging with connection ID.
 */
#define conn_log_to_logger(LOGGER, LEVEL, FORMAT, CONNECTION, ...)                                 \
  log_to_logger(LOGGER, LEVEL, "[C{}] " FORMAT, (CONNECTION).id(), ##__VA_ARGS__)

#define conn_log_facility(LEVEL, FORMAT, CONNECTION, ...)                                          \
  conn_log_to_logger(log(), LEVEL, FORMAT, CONNECTION, ##__VA_ARGS__)

/**
 * Convenience macros for logging with a stream ID and a connection ID.
 */
#define stream_log_to_logger(LOGGER, LEVEL, FORMAT, STREAM, ...)                                   \
  log_to_logger(LOGGER, LEVEL, "[C{}][S{}] " FORMAT, (STREAM).connectionId(), (STREAM).streamId(), \
                ##__VA_ARGS__)

#define stream_log_facility(LEVEL, FORMAT, STREAM, ...)                                            \
  stream_log_to_logger(log(), LEVEL, FORMAT, STREAM, ##__VA_ARGS__)

/**
 * DEPRECATED: Logging macros.
 */
#ifdef NDEBUG
#define log_trace(...)
#define log_debug(...)
#else
#define log_trace(...) log().trace(__VA_ARGS__)
#define log_debug(...) log().debug(__VA_ARGS__)
#endif

/**
 * DEPRECATED: Convenience macros for logging with connection ID.
 */
#define conn_log(LOG, LEVEL, FORMAT, CONNECTION, ...)                                              \
  LOG.LEVEL("[C{}] " FORMAT, (CONNECTION).id(), ##__VA_ARGS__)

#ifdef NDEBUG
#define conn_log_trace(...)
#define conn_log_debug(...)
#else
#define conn_log_trace(FORMAT, CONNECTION, ...)                                                    \
  conn_log(log(), trace, FORMAT, CONNECTION, ##__VA_ARGS__)
#define conn_log_debug(FORMAT, CONNECTION, ...)                                                    \
  conn_log(log(), debug, FORMAT, CONNECTION, ##__VA_ARGS__)
#endif

#define conn_log_info(FORMAT, CONNECTION, ...)                                                     \
  conn_log(log(), info, FORMAT, CONNECTION, ##__VA_ARGS__)

/**
 * DEPRECATED: Convenience macros for logging with a stream ID and a connection ID.
 */
#define stream_log(LOG, LEVEL, FORMAT, STREAM, ...)                                                \
  LOG.LEVEL("[C{}][S{}] " FORMAT, (STREAM).connectionId(), (STREAM).streamId(), ##__VA_ARGS__)

#ifdef NDEBUG
#define stream_log_trace(...)
#define stream_log_debug(...)
#else
#define stream_log_trace(FORMAT, STREAM, ...)                                                      \
  stream_log(log(), trace, FORMAT, STREAM, ##__VA_ARGS__)
#define stream_log_debug(FORMAT, STREAM, ...)                                                      \
  stream_log(log(), debug, FORMAT, STREAM, ##__VA_ARGS__)
#endif

#define stream_log_info(FORMAT, STREAM, ...) stream_log(log(), info, FORMAT, STREAM, ##__VA_ARGS__)

} // Envoy
