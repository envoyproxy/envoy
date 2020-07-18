#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "spdlog/spdlog.h"

namespace Envoy {

using FancyMap = absl::flat_hash_map<std::string, std::shared_ptr<spdlog::logger>>;
using FancyMapPtr = std::shared_ptr<FancyMap>;
using SpdLoggerPtr = std::shared_ptr<spdlog::logger>;

/**
 * Stores the lock and functions used by Fancy Logger's macro so that we don't need to declare
 * them globally. Functions are provided to initialize a logger, set log level, flush a logger.
 */
class FancyContext {
public:
  /**
   * Gets a logger from map given the key (e.g. file name).
   */
  static SpdLoggerPtr getFancyLogEntry(std::string key) ABSL_LOCKS_EXCLUDED(fancy_log_lock_);

  /**
   * Initializes Fancy Logger and register it in global map if not done.
   */
  static void initFancyLogger(std::string key, std::atomic<spdlog::logger*>& logger)
      ABSL_LOCKS_EXCLUDED(fancy_log_lock_);

  /**
   * Sets log level. If not found, return false.
   */
  static bool setFancyLogger(std::string key, spdlog::level::level_enum log_level)
      ABSL_LOCKS_EXCLUDED(fancy_log_lock_);

  /**
   * Sets the default logger level and format when updating context.
   */
  static void setDefaultFancyLevelFormat(spdlog::level::level_enum level, std::string format)
      ABSL_LOCKS_EXCLUDED(fancy_log_lock_);

  /**
   * Lists keys and levels of all loggers in a string for admin page usage.
   */
  static std::string listFancyLoggers() ABSL_LOCKS_EXCLUDED(fancy_log_lock_);

private:
  /**
   * Initializes sink for the initialization of loggers, needed only in benchmark test.
   */
  static void initSink();

  /**
   * Creates a logger given key and log level, and add it to map.
   * Key is the log component name, e.g. file name now.
   */
  static spdlog::logger* createLogger(std::string key, int level = -1)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(fancy_log_lock_);

  /**
   * Lock for the following map (not for the corresponding loggers).
   */
  ABSL_CONST_INIT static absl::Mutex fancy_log_lock_;

  /**
   * Map that stores <key, logger> pairs, key can be the file name.
   */
  static FancyMapPtr fancy_log_map_ ABSL_GUARDED_BY(fancy_log_lock_);
};

#define FANCY_KEY std::string(__FILE__)

/**
 * Macro for fancy logger.
 * Uses a global map to store logger and take use of thread-safe spdlog::logger.
 * The local pointer is used to avoid another load() when logging. Here we use
 * spdlog::logger* as atomic<shared_ptr> is a C++20 feature.
 */
#define FANCY_LOG(LEVEL, ...)                                                                      \
  do {                                                                                             \
    static std::atomic<spdlog::logger*> flogger{0};                                                \
    spdlog::logger* local_flogger = flogger.load(std::memory_order_relaxed);                       \
    if (!local_flogger) {                                                                          \
      FancyContext::initFancyLogger(FANCY_KEY, flogger);                                           \
      local_flogger = flogger.load(std::memory_order_relaxed);                                     \
    }                                                                                              \
    local_flogger->log(spdlog::source_loc{__FILE__, __LINE__, __func__},                           \
                       ENVOY_SPDLOG_LEVEL(LEVEL), __VA_ARGS__);                                    \
  } while (0)

/**
 * Convenient macro for connection log.
 */
#define FANCY_CONN_LOG(LEVEL, FORMAT, CONNECTION, ...)                                             \
  FANCY_LOG(LEVEL, "[C{}] " FORMAT, (CONNECTION).id(), ##__VA_ARGS__)

/**
 * Convenient macro for stream log.
 */
#define FANCY_STREAM_LOG(LEVEL, FORMAT, STREAM, ...)                                               \
  FANCY_LOG(LEVEL, "[C{}][S{}] " FORMAT, (STREAM).connection() ? (STREAM).connection()->id() : 0,  \
            (STREAM).streamId(), ##__VA_ARGS__)

/**
 * Convenient macro for log flush.
 */
#define FANCY_FLUSH_LOG()                                                                          \
  { FancyContext::getFancyLogEntry(FANCY_KEY)->flush(); }

} // namespace Envoy
