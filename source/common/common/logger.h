#pragma once

#include "macros.h"

#include "envoy/thread/thread.h"

namespace Logger {

// clang-format off
#define ALL_LOGGER_IDS(FUNCTION) \
  FUNCTION(admin)                \
  FUNCTION(assert)               \
  FUNCTION(client)               \
  FUNCTION(config)               \
  FUNCTION(connection)           \
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

#ifdef NDEBUG
#define log_trace(...)
#define log_debug(...)
#else
#define log_trace(...) log().trace(__VA_ARGS__)
#define log_debug(...) log().debug(__VA_ARGS__)
#endif

/**
 * Convenience macros for logging with connection ID.
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
 * Convenience macros for logging with a stream ID and a connection ID.
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
