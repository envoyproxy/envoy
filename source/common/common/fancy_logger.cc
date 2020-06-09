#include "common/common/logger.h"
#include <memory>

using spdlog::level::level_enum;


namespace Envoy{

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


static spdlog::sink_ptr getFancySink() {
    static spdlog::sink_ptr sink = Logger::DelegatingLogSink::init();
    return sink;
  }

const char* LOG_PATTERN = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

/**
 * Get Fancy Logger.
 * 1. If up to date, return a logger with local level;
 * 2. else, update the local logger with global linked list.
 */
spdlog::logger getFancyLogger(const char* file, int* local_epoch, level_enum** local_level) {
    // printf("%s: epoch = %d, level = %d\n", file, *local_epoch, static_cast<int>(**local_level));

    spdlog::logger flogger("Anonymous", getFancySink());
    if (!global_epoch__) {
        initFancyGlobalEpoch();
    }
    const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
    if (!entry) {
        // printf(" - Creating a new entry ...\n");
        FancyLogInfo* new_entry = new FancyLogInfo;
        new_entry->file = std::string(file);
        new_entry->level = level_enum::info;

        fancy_log_lock__.Lock();
        new_entry->next = fancy_log_list__;
        fancy_log_list__ = new_entry;
        fancy_log_lock__.Unlock();

        // initialize local epoch & local level of the first site of file
        *local_epoch = 0;
        *local_level = &new_entry->level;
    }
    else if (*local_epoch < global_epoch__->load()) {
        // printf(" - Updating existing entry ...\n");
        *local_epoch = global_epoch__->load();
        *local_level = &entry->level;
    }

    flogger.set_level(**local_level);
    flogger.set_pattern(LOG_PATTERN);
    flogger.flush_on(level_enum::critical);
    printf(" Return: global epoch = %d, local epoch = %d, logger level = %d\n", \
        global_epoch__->load(), *local_epoch, flogger.level());
    return static_cast<spdlog::logger>(flogger);
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

    printf("Setting Fancy Log Level: \n");
    const FancyLogInfo* entry = findFancyInfo(fancy_log_list__, file);
    if (!entry) {
        // printf(" - Creating a new entry here...\n");
        FancyLogInfo* new_entry = new FancyLogInfo;
        new_entry->file = std::string(file);
        new_entry->level = log_level;
        fancy_log_lock__.Lock();
        new_entry->next = fancy_log_list__;
        fancy_log_list__ = new_entry;
    }
    else {
        // printf(" - Modify existing entry ...\n");
        fancy_log_lock__.Lock();
        entry->level = log_level;
    }
    fancy_log_lock__.Unlock();

    return new_epoch;
}



} // namespace Envoy