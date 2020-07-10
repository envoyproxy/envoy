#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "spdlog/spdlog.h"

namespace Envoy {

using FancyMap = absl::flat_hash_map<std::string, std::shared_ptr<spdlog::logger>>;
using FancyMapPtr = std::shared_ptr<FancyMap>;
using SpdLoggerPtr = std::shared_ptr<spdlog::logger>;

class FancyContext {
public:
  static FancyMapPtr getFancyLogMap();
  static absl::Mutex* getFancyLogLock();
  static void initFancyLogger(std::string key, std::atomic<spdlog::logger*>& logger);
  static bool setFancyLogger(std::string key, spdlog::level::level_enum log_level);
  static void flushFancyLogger(); // not work now, so not used...

protected:
  static void initSink();
  static spdlog::logger* createLogger(std::string key, int level = -1)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(fancy_log_lock_);

public:
  /**
   * Lock for the following global map (not for the corresponding loggers).
   */
  ABSL_CONST_INIT static absl::Mutex fancy_log_lock_;
};

/**
 * ---------------------------------------------------------------
 * Fancy logging macros
 */

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

/**
 * End
 * ----------------------------------------------------------------
 */

} // namespace Envoy
