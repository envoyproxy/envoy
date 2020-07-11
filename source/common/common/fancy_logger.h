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
 * Stores the lock and functions used by Fancy Logger's macro so that we don't need to declaring
 * them globally. Functions are provided to initialize a logger, set log level, flush a logger.
 */
class FancyContext {
public:
  static FancyMapPtr getFancyLogMap();
  static absl::Mutex* getFancyLogLock();
  /**
   * Initializes Fancy Logger and register it in global map if not done.
   */
  static void initFancyLogger(std::string key, std::atomic<spdlog::logger*>& logger);

  /**
   * Sets log level. If not found, return false.
   */
  static bool setFancyLogger(std::string key, spdlog::level::level_enum log_level);

protected:
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

public:
  /**
   * Lock for the following global map (not for the corresponding loggers).
   */
  ABSL_CONST_INIT static absl::Mutex fancy_log_lock_;
};

#define FANCY_KEY std::string(__FILE__)

// #define FANCY_KEY std::string(__FILE__ ":" __func__)

// #define FANCY_KEY std::string(__FILE__ ":" __func__) + std::to_string(__LINE__)

/**
 * Macro for fancy logger.
 * Use a global map to store logger and take use of thread-safe spdlog::logger.
 * The local pointer is used to avoid another load() when logging. Here we use
 * spdlog::logger* as atomic<shared_ptr> is a C++20 feature.
 */
#define FANCY_LOG(LEVEL, ...)                                                                      \
  do {                                                                                             \
    static std::atomic<spdlog::logger*> flogger{0};                                                \
    spdlog::logger* local_flogger = flogger.load(std::memory_order_relaxed);                       \
    if (!local_flogger) {                                                                          \
      FancyContext::initFancyLogger(FANCY_KEY, flogger);                                           \
      flogger.load(std::memory_order_relaxed)                                                      \
          ->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, ENVOY_SPDLOG_LEVEL(LEVEL),       \
                __VA_ARGS__);                                                                      \
    } else {                                                                                       \
      local_flogger->log(spdlog::source_loc{__FILE__, __LINE__, __func__},                         \
                         ENVOY_SPDLOG_LEVEL(LEVEL), __VA_ARGS__);                                  \
    }                                                                                              \
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
  {                                                                                                \
    FancyContext::getFancyLogLock()->ReaderLock();                                                 \
    FancyContext::getFancyLogMap()->find(FANCY_KEY)->second->flush();                              \
    FancyContext::getFancyLogLock()->ReaderUnlock();                                               \
  }

} // namespace Envoy
