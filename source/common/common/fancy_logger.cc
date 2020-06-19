#include "common/common/logger.h"
#include "absl/container/flat_hash_map.h"
#include <atomic>
#include <memory>

using spdlog::level::level_enum;

namespace Envoy {

/**
 * FancyLogger implementation by Jinhui Song.
 */

/**
 * If the sink is initialized, should be protected by WriterLock.
 */
int kSinkInit = 0;

/**
 * Lock for the following global map (not for the corresponding loggers).
 */
static absl::Mutex fancy_log_lock__;

/**
 * Global hash map <unit, log info>, where unit can be file, function or line.
 */
absl::flat_hash_map<std::string, spdlog::logger*> fancy_log_map__;

const char* LOG_PATTERN = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

spdlog::sink_ptr getSink() {
  static spdlog::sink_ptr sink = Logger::DelegatingLogSink::init();
  return sink;
}

/**
 * Implementation of BasicLockable by Jinhui Song, to avoid dependency
 * problem of thread.h.
 */
class FancyBasicLockable : public Thread::BasicLockable {
public:
  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  absl::Mutex mutex_;
};

/**
 * Initialize sink for the initialization of loggers, once and for all.
 */
void initSink() {
  spdlog::sink_ptr sink = getSink();
  Logger::DelegatingLogSinkSharedPtr sp = std::static_pointer_cast<Logger::DelegatingLogSink>(sink);
  if (!sp->hasLock()) {
    static FancyBasicLockable tlock;
    sp->setLock(tlock);
    sp->set_should_escape(false); // unsure
    printf("sink lock: %s\n", sp->hasLock() ? "true" : "false");
  }
}

/**
 * Create a logger and add it to map.
 */
spdlog::logger* createLogger(std::string key, level_enum level = level_enum::info) {
  spdlog::logger* new_logger = new spdlog::logger(key, getSink()); // TODO: change to unique_ptr
  new_logger->set_level(level);
  new_logger->set_pattern(LOG_PATTERN);
  new_logger->flush_on(level_enum::critical);
  fancy_log_map__.insert(std::make_pair(key, new_logger));
  return new_logger;
}

/**
 * Initialize Fancy Logger and register it in global map if not done.
 */
void initFancyLogger(std::string key, std::atomic<spdlog::logger*>& logger) {
  absl::WriterMutexLock l(&fancy_log_lock__);
  if (!kSinkInit) {
    initSink();
  }
  auto it = fancy_log_map__.find(key);
  spdlog::logger* target = it == fancy_log_map__.end()? createLogger(key) : it->second;
  logger.store(target);
//   printf("  Init: key: %s, level: %d\n", key.c_str(), logger.load(std::memory_order_relaxed)->level());
}

/**
 * Set log level.
 */
void setFancyLogger(std::string key, level_enum log_level) {
  absl::WriterMutexLock l(&fancy_log_lock__);
  auto it = fancy_log_map__.find(key);
  if (it != fancy_log_map__.end()) {
    it->second->set_level(log_level);
  } else {
    createLogger(key, log_level);
  }
}

} // namespace Envoy
