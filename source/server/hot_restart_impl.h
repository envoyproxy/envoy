#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats_options.h"

#include "common/common/assert.h"
#include "common/stats/raw_stat_data.h"

#include "server/hot_restarting_child.h"
#include "server/hot_restarting_parent.h"

namespace Envoy {
namespace Server {

/**
 * Shared memory segment. This structure is laid directly into shared memory and is used amongst
 * all running envoy processes.
 * TODO(fredlas) delete this class in followup PR.
 */
class SharedMemory {
public:
  static void configure(uint64_t max_num_stats, uint64_t max_stat_name_len);
  static std::string version(uint64_t max_num_stats, const Stats::StatsOptions& stats_options);

  // TODO(fredlas) move to HotRestartImpl
  // Made public for testing.
  static const uint64_t VERSION;

  int64_t maxStats() const { return max_stats_; }

private:
  struct Flags {
    static const uint64_t INITIALIZING = 0x1;
  };

  // Due to the flexible-array-length of stats_set_data_, c-style allocation
  // and initialization are necessary.
  SharedMemory() = delete;
  ~SharedMemory() = delete;

  /**
   * Initialize the shared memory segment, depending on whether we should be the first running
   * envoy, or a host restarted envoy process.
   */
  static SharedMemory& initialize(uint64_t stats_set_size, const Options& options);

  /**
   * Initialize a pthread mutex for process shared locking.
   */
  void initializeMutex(pthread_mutex_t& mutex);

  uint64_t size_;
  uint64_t version_;
  uint64_t max_stats_;
  uint64_t entry_size_;
  std::atomic<uint64_t> flags_;
  pthread_mutex_t log_lock_;
  pthread_mutex_t access_log_lock_;
  pthread_mutex_t stat_lock_;
  pthread_mutex_t init_lock_;
  alignas(BlockMemoryHashSet<Stats::RawStatData>) uint8_t stats_set_data_[];

  friend class HotRestartImpl;
};

/**
 * Implementation of Thread::BasicLockable that operates on a process shared pthread mutex.
 */
class ProcessSharedMutex : public Thread::BasicLockable {
public:
  ProcessSharedMutex(pthread_mutex_t& mutex) : mutex_(mutex) {}

  void lock() EXCLUSIVE_LOCK_FUNCTION() override {
    // Deal with robust handling here. If the other process dies without unlocking, we are going
    // to die shortly but try to make sure that we can handle any signals, etc. that happen without
    // getting into a further messed up state.
    int rc = pthread_mutex_lock(&mutex_);
    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
  }

  bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) override {
    int rc = pthread_mutex_trylock(&mutex_);
    if (rc == EBUSY) {
      return false;
    }

    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }

    return true;
  }

  void unlock() UNLOCK_FUNCTION() override {
    int rc = pthread_mutex_unlock(&mutex_);
    ASSERT(rc == 0);
  }

private:
  pthread_mutex_t& mutex_;
};

/**
 * Implementation of HotRestart built for Linux. Most of the "protocol" type logic is split out into
 * HotRestarting{Base,Parent,Child}. This class ties all that to shared memory and version logic.
 */
class HotRestartImpl : public HotRestart {
public:
  HotRestartImpl(const Options& options);

  // Server::HotRestart
  void drainParentListeners() override;
  int duplicateParentListenSocket(const std::string& address) override;
  std::unique_ptr<envoy::api::v2::core::HotRestartMessage> getParentStats() override;
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) override;
  void shutdownParentAdmin(ShutdownParentAdminInfo& info) override;
  void terminateParent() override;
  void shutdown() override;
  std::string version() override;
  Thread::BasicLockable& logLock() override { return log_lock_; }
  Thread::BasicLockable& accessLogLock() override { return access_log_lock_; }
  Stats::RawStatDataAllocator& statsAllocator() override { return *stats_allocator_; }

  /**
   * envoy --hot_restart_version doesn't initialize Envoy, but computes the version string
   * based on the configured options.
   */
  static std::string hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len);

private:
  static std::string versionHelper(uint64_t max_num_stats, const Stats::StatsOptions& stats_options,
                                   Stats::RawStatDataSet& stats_set);

  HotRestartingChild as_child_;
  HotRestartingParent as_parent_;
  const Options& options_;
  BlockMemoryHashSetOptions stats_set_options_;
  SharedMemory& shmem_;
  std::unique_ptr<Stats::RawStatDataSet> stats_set_ GUARDED_BY(stat_lock_);
  std::unique_ptr<Stats::RawStatDataAllocator> stats_allocator_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
};

} // namespace Server
} // namespace Envoy
