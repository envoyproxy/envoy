#pragma once

#include <string>

#include "source/common/common/macros.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "spdlog/spdlog.h"

namespace Envoy {

using SpdLoggerSharedPtr = std::shared_ptr<spdlog::logger>;
using FineGrainLogMap = absl::flat_hash_map<std::string, SpdLoggerSharedPtr>;
using FineGrainLogMapSharedPtr = std::shared_ptr<FineGrainLogMap>;
using FineGrainLogLevelMap = absl::flat_hash_map<std::string, spdlog::level::level_enum>;

constexpr int kLogLevelMax = 6; // spdlog level is in [0, 6].
constexpr int kLogLevelMin = 0;
inline const char* kDefaultFineGrainLogFormat = "[%Y-%m-%d %T.%e][%t][%l] [%g:%#] %v";

/**
 * Data struct that stores the necessary verbosity log update info.
 */
struct VerbosityLogUpdateInfo final {
  const std::string update_pattern;
  const bool update_is_path; // i.e. it contains a path separator.
  const spdlog::level::level_enum log_level;

  VerbosityLogUpdateInfo(absl::string_view update_pattern, bool update_is_path,
                         spdlog::level::level_enum log_level)
      : update_pattern(std::string(update_pattern)), update_is_path(update_is_path),
        log_level(log_level) {}
};

/**
 * Stores the lock and functions used by Fine-Grain Logger's macro so that we don't need to declare
 * them globally. Functions are provided to initialize a logger, set log level, flush a logger.
 */
class FineGrainLogContext {
public:
  /**
   * Gets a logger from map given a key (e.g. file name).
   */
  SpdLoggerSharedPtr getFineGrainLogEntry(absl::string_view key)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Gets the default verbosity log level.
   */
  spdlog::level::level_enum getVerbosityDefaultLevel() const
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Initializes Fine-Grain Logger, gets log level from setting vector, and registers it in global
   * map if not done.
   */
  void initFineGrainLogger(const std::string& key, std::atomic<spdlog::logger*>& logger)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Sets log level. If not found, return false.
   */
  bool setFineGrainLogger(absl::string_view key, spdlog::level::level_enum log_level)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Sets the default verbosity log level and format when updating context. It will update all the
   * loggers based on new level and verbosity info vector. It is used in Context during
   * initialization or the enable process.
   */
  void setDefaultFineGrainLogLevelFormat(spdlog::level::level_enum level, const std::string& format)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Lists keys and levels of all loggers in a string for admin page usage.
   */
  std::string listFineGrainLoggers() ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Sets the levels of all loggers.
   */
  void setAllFineGrainLoggers(spdlog::level::level_enum level)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Obtain a map from logger key to log level. Useful for testing, e.g. in macros such as
   * EXPECT_LOG_CONTAINS_ALL_OF_HELPER.
   */
  FineGrainLogLevelMap getAllFineGrainLogLevelsForTest() ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Updates all loggers based on the verbosity updates <(file, level) ...>.
   * It supports file basename and glob "*" and "?" pattern, eg. ("foo", 2), ("foo/b*", 3)
   * Patterns including a slash character are matched against full path names, while those
   * without are matched against base names (by removing one suffix) only.
   *
   * It will store the current verbosity updates and clear all previous modifications for
   * future check when initializing a new logger.
   *
   * Files are matched against globs in updates in order, and the first match determines
   * the verbosity level.
   *
   * Files which do not match any pattern use the default verbosity log level.
   */
  void updateVerbositySetting(const std::vector<std::pair<absl::string_view, int>>& updates)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Updates all loggers based on the new default verbosity log level.
   * The default verbosity log level is overridable if the verbosity update vector is not empty.
   * All the loggers which are not matched with verbosity_update_info_ are set to new default
   * log level.
   *
   * verbosity_default_level_ could be different from fine_grain_default_level_ in Context after
   * calling this method.
   */
  void updateVerbosityDefaultLevel(spdlog::level::level_enum level)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_);

  /**
   * Check if a string matches a glob pattern. It only supports "*" and "?" wildcards,
   * and wildcards may match /. No support for bracket expressions [...].
   */
  static bool safeFileNameMatch(absl::string_view pattern, absl::string_view str);

  /**
   * Remove a fine grain log entry for testing only.
   */
  void removeFineGrainLogEntryForTest(absl::string_view key)
      ABSL_LOCKS_EXCLUDED(fine_grain_log_lock_) {
    absl::WriterMutexLock wl(&fine_grain_log_lock_);
    fine_grain_log_map_->erase(key);
  }

private:
  /**
   * Initializes sink for the initialization of loggers, needed only in benchmark test.
   */
  void initSink();

  /**
   * Creates a logger given key, and add it to map. Log level is from getLogLevel.
   * Key is the log component name, e.g. file name now.
   */
  spdlog::logger* createLogger(const std::string& key)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(fine_grain_log_lock_);

  /**
   * Append verbosity level updates to the VerbosityLogUpdateInfo vector.
   */
  void appendVerbosityLogUpdate(absl::string_view update_pattern,
                                spdlog::level::level_enum log_level)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(fine_grain_log_lock_);

  /**
   * Returns the current log level of `file`. Default log level is used if there is no
   * match in verbosity_update_info_.
   */
  spdlog::level::level_enum getLogLevel(absl::string_view file) const
      ABSL_SHARED_LOCKS_REQUIRED(fine_grain_log_lock_);

  /**
   * Lock for the following global map, update vector, and default log level
   * (not for the corresponding loggers).
   */
  mutable absl::Mutex fine_grain_log_lock_;

  /**
   * Map that stores <key, logger> pairs, key can be the file name.
   */
  FineGrainLogMapSharedPtr fine_grain_log_map_ ABSL_GUARDED_BY(fine_grain_log_lock_) =
      std::make_shared<FineGrainLogMap>();

  /**
   * Vector that stores <update, level> pairs, key can be the file basename or glob expressions.
   * It will override the default verbosity log level.
   */
  std::vector<VerbosityLogUpdateInfo> verbosity_update_info_ ABSL_GUARDED_BY(fine_grain_log_lock_);

  /**
   * Default verbosity log level. It could be different from fine_grain_default_level_ in Context.
   * It is overridable if verbosity_update_info_ is not empty.
   */
  spdlog::level::level_enum
      verbosity_default_level_ ABSL_GUARDED_BY(fine_grain_log_lock_) = spdlog::level::info;
};

FineGrainLogContext& getFineGrainLogContext();

/**
 * Macro to get a fine-grain logger.
 * Uses a global map to store logger and take use of thread-safe spdlog::logger.
 * The local pointer is used to avoid another load() when logging. Here we use
 * spdlog::logger* as atomic<shared_ptr> is a C++20 feature.
 */
#define FINE_GRAIN_LOGGER()                                                                        \
  ([]() -> spdlog::logger* {                                                                       \
    static std::atomic<spdlog::logger*> flogger{nullptr};                                          \
    spdlog::logger* local_flogger = flogger.load(std::memory_order_relaxed);                       \
    if (!local_flogger) {                                                                          \
      ::Envoy::getFineGrainLogContext().initFineGrainLogger(__FILE__, flogger);                    \
      local_flogger = flogger.load(std::memory_order_relaxed);                                     \
    }                                                                                              \
    return local_flogger;                                                                          \
  }())

/**
 * Macro for fine-grain logger to log.
 */
#define FINE_GRAIN_LOG(LEVEL, ...)                                                                 \
  do {                                                                                             \
    spdlog::logger* local_flogger = FINE_GRAIN_LOGGER();                                           \
    if (ENVOY_LOG_COMP_LEVEL(*local_flogger, LEVEL)) {                                             \
      local_flogger->log(spdlog::source_loc{__FILE__, __LINE__, __func__},                         \
                         ENVOY_SPDLOG_LEVEL(LEVEL), __VA_ARGS__);                                  \
    }                                                                                              \
  } while (0)

/**
 * Convenient macro for connection log.
 */
#define FINE_GRAIN_CONN_LOG(LEVEL, FORMAT, CONNECTION, ...)                                        \
  FINE_GRAIN_LOG(LEVEL, "[C{}] " FORMAT, (CONNECTION).id(), ##__VA_ARGS__)

/**
 * Convenient macro for stream log.
 */
#define FINE_GRAIN_STREAM_LOG(LEVEL, FORMAT, STREAM, ...)                                          \
  FINE_GRAIN_LOG(LEVEL, "[C{}][S{}] " FORMAT,                                                      \
                 (STREAM).connection() ? (STREAM).connection()->id() : 0, (STREAM).streamId(),     \
                 ##__VA_ARGS__)

/**
 * Convenient macro for log flush.
 */
#define FINE_GRAIN_FLUSH_LOG()                                                                     \
  do {                                                                                             \
    SpdLoggerSharedPtr p = ::Envoy::getFineGrainLogContext().getFineGrainLogEntry(__FILE__);       \
    if (p) {                                                                                       \
      p->flush();                                                                                  \
    }                                                                                              \
  } while (0)

} // namespace Envoy
