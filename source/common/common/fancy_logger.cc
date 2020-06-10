#include "common/common/logger.h"
#include <memory>

using spdlog::level::level_enum;

namespace Envoy {

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

const char* LOG_PATTERN = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

/**
 * Update logger when outdated.
 */
void updateFancyLogger(const char* file, spdlog::logger* logger, int* local_epoch) {
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

    *local_epoch = 0;
    logger->set_level(new_entry->level);
  } else {
    *local_epoch = global_epoch__->load();
    logger->set_level(entry->level);
  }
  logger->set_pattern(LOG_PATTERN);
  logger->flush_on(level_enum::critical);
  printf(" Slow path: global epoch = %d, level = %d\n", global_epoch__->load(), logger->level());
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
    new_epoch = global_epoch__->load() + 1;
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