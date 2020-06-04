#include "common/common/logger.h"

using spdlog::level::level_enum;


namespace Envoy{

/**
 * FancyLogger implementation by Jinhui Song.
 */

/**
 * Log info struct for the linked list.
 */
struct FancyLogInfo {
  std::string file;
  mutable spdlog::level::level_enum level;
  const FancyLogInfo* next;
};

/**
 * Global epoch of the latest update of <file, log_level> map.
 */
static std::atomic<int>* global_epoch__ = nullptr;

/** 
 * Lock for the following linked list.
 */
static absl::Mutex fancy_log_lock__;

/**
 * Global linked list for file and log_level.
 */
static FancyLogInfo* fancy_log_list__ = nullptr;



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
 */
const FancyLogInfo* findFancyInfo(FancyLogInfo* list, std::string file) {
    const FancyLogInfo* cur = list;
    absl::MutexLock l(&fancy_log_lock__);
    for( ; cur != nullptr; cur = cur->next) {
        if (cur->file == file) {
            break;
        }
    }
    return cur;
}

/**
 * Initialize the global epoch and linked list, update the local fancy logger.
 */
void updateFancyLogger(const char* file, spdlog::logger* logger, int** local_epoch) {
    if (!global_epoch__) {
        initFancyGlobalEpoch();
    }
    
    const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
    if (!entry)             // initialization if no entry exists (empty list case included)
    {
        FancyLogInfo* new_entry = new FancyLogInfo;
        new_entry->file = std::string(file);
        new_entry->level = level_enum::info;        // default level, so no global epoch update needed
        new_entry->next = fancy_log_list__;         // add the entry in the front
        fancy_log_list__ = new_entry;
    }
    else {                  // update local epoch and log level
        **local_epoch = *global_epoch__;
        logger->set_level(entry->level);
    }

}

/**
 * Hook for setting log level.
 */
int setFancyLogLevel(const char* file, level_enum log_level) {
    int new_epoch = 0;
    if (!global_epoch__) {
        initFancyGlobalEpoch();
    }
    else {
        new_epoch = global_epoch__->load() + 1;
        global_epoch__->store(new_epoch);
    }

    const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
    if (!entry) {
        FancyLogInfo* new_entry = new FancyLogInfo;
        new_entry->file = std::string(file);
        new_entry->level = log_level;
        new_entry->next = fancy_log_list__;
        fancy_log_list__ = new_entry;
    }
    else {
        fancy_log_lock__.Lock();
        entry->level = log_level;
        fancy_log_lock__.Unlock();
    }

    return new_epoch;
}

} // namespace Envoy