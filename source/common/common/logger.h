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
  FUNCTION(general)              \
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
 * Base logging macros.
 */
#ifdef NDEBUG
#define log_trace(LOGGER, ...)
#define log_debug(LOGGER, ...)
#else
#define log_trace(LOGGER, ...) LOGGER.trace(__VA_ARGS__)
#define log_debug(LOGGER, ...) LOGGER.debug(__VA_ARGS__)
#endif

#define log_info(LOGGER, ...) LOGGER.info(__VA_ARGS__)
#define log_warn(LOGGER, ...) LOGGER.warn(__VA_ARGS__)
#define log_err(LOGGER, ...) LOGGER.err(__VA_ARGS__)
#define log_critical(LOGGER, ...) LOGGER.critical(__VA_ARGS__)

/**
 * Convenience macros to call the above macros with or without a logger.
 */
#define log_facility(LEVEL, ...) log_##LEVEL(log(), ##__VA_ARGS__)
#define log_to_logger(LOGGER, LEVEL, ...) log_##LEVEL(LOGGER, ##__VA_ARGS__)

/**
 * Convenience macros to log to the general logger.
 */
#define get_general_logger() Logger::Loggable<Logger::Id::general>::log()
#define log_general(LEVEL, ...) log_to_logger(get_general_logger(), LEVEL, ##__VA_ARGS__)

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

} // Envoy
