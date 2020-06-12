#include "common/common/logger.h"
#include <atomic>
#include <memory>

using spdlog::level::level_enum;

namespace Envoy {

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
 * FancyLogger implementation by Jinhui Song.
 */

spdlog::level::level_enum kDefaultFancyLevel = spdlog::level::info;

/**
 * Global epoch of the latest update of <file, log_level> map.
 */
std::atomic<int>* global_epoch__ = nullptr;

/**
 * Lock for the following linked list.
 */
static absl::Mutex fancy_log_lock__;

/**
 * Global linked list for file and log_level, static if not tests.
 */
FancyLogInfo* fancy_log_list__ = nullptr;

/**
 * Global epoch initialization. Used to store the static variable as it's not sure whether
 * the epoch is initialized in updateFancyLogger or setFancyLogger.
 */
void initFancyGlobalEpoch() {
  static std::atomic<int> global_epoch_val;
  global_epoch_val.store(0);
  global_epoch__ = &global_epoch_val;
<<<<<<< HEAD

  spdlog::sink_ptr sink = getSink();
  Logger::DelegatingLogSinkSharedPtr sp = std::static_pointer_cast<Logger::DelegatingLogSink>(sink);
  printf("sink has lock or not: %s\n", sp->hasLock()? "true" : "false");
  if (!sp->hasLock()) {
      static FancyBasicLockable tlock;
      sp->setLock(tlock);
      sp->set_should_escape(false);         // unsure
      printf("sink lock: %s\n", sp->hasLock()? "true" : "false");
  }
=======
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f
}

/**
 * Find the file info in the linked list, return null if not found.
 * Should run in a locked session.
 */
const FancyLogInfo* findFancyInfo(FancyLogInfo* list, std::string file) {
  const FancyLogInfo* cur = list;
  for (; cur != nullptr; cur = cur->next) {
    if (cur->file == file) {
      break;
    }
  }
  return cur;
}
<<<<<<< HEAD

spdlog::sink_ptr getSink() {
    static spdlog::sink_ptr sink = Logger::DelegatingLogSink::init();
    return sink;
}
=======
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f

const char* LOG_PATTERN = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

/**
 * Update logger when outdated.
 */
<<<<<<< HEAD
void updateFancyLogger(const char* file, spdlog::logger* logger, std::atomic<int>* local_epoch) {
=======
void updateFancyLogger(const char* file, spdlog::logger* logger, int* local_epoch) {
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f
  absl::ReaderMutexLock l(&fancy_log_lock__);
  if (!global_epoch__) {
    initFancyGlobalEpoch();
  }
  const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
  if (!entry) {
    FancyLogInfo* new_entry = new FancyLogInfo;
    new_entry->file = std::string(file);
    new_entry->level = level_enum::info;
    new_entry->next = fancy_log_list__;
    fancy_log_list__ = new_entry;

<<<<<<< HEAD
    local_epoch->store(0);
    logger->set_level(new_entry->level);
  } else {
    local_epoch->store(global_epoch__->load(std::memory_order_relaxed));
=======
    *local_epoch = 0;
    logger->set_level(new_entry->level);
  } else {
    *local_epoch = global_epoch__->load();
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f
    logger->set_level(entry->level);
  }
  logger->set_pattern(LOG_PATTERN);
  logger->flush_on(level_enum::critical);
<<<<<<< HEAD
  printf(" Slow path: global epoch = %d, level = %d\n", global_epoch__->load(std::memory_order_relaxed), logger->level());
=======
  printf(" Slow path: global epoch = %d, level = %d\n", global_epoch__->load(), logger->level());
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f
}

/**
 * Hook for setting log level.
 */
int setFancyLogLevel(const char* file, level_enum log_level) {
  absl::WriterMutexLock l(&fancy_log_lock__);
  int new_epoch = 0;
  if (!global_epoch__) {
    initFancyGlobalEpoch();
  } else {
<<<<<<< HEAD
    new_epoch = global_epoch__->load(std::memory_order_relaxed) + 1;
=======
    new_epoch = global_epoch__->load() + 1;
>>>>>>> 49258b4159776e84c96ae447e3e9f10b435dfa6f
    global_epoch__->store(new_epoch);
  }

  printf("Setting Fancy Log Level... \n");
  const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
  if (!entry) {
    FancyLogInfo* new_entry = new FancyLogInfo;
    new_entry->file = std::string(file);
    new_entry->level = log_level;
    new_entry->next = fancy_log_list__;
    fancy_log_list__ = new_entry;
  } else {
    entry->level = log_level;
  }

  return new_epoch;
}

} // namespace Envoy