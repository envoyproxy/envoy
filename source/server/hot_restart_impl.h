#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/server/hot_restart.h"

#include "common/common/assert.h"
#include "common/stats/allocator_impl.h"

#include "server/hot_restarting_child.h"
#include "server/hot_restarting_parent.h"

namespace Envoy {
namespace Server {

// Increment this whenever there is a shared memory / RPC change that will prevent a hot restart
// from working. Operations code can then cope with this and do a full restart.
const uint64_t HOT_RESTART_VERSION = 11;

/**
 * Shared memory segment. This structure is laid directly into shared memory and is used amongst
 * all running envoy processes.
 */
struct SharedMemory {
  uint64_t size_;
  uint64_t version_;
  pthread_mutex_t log_lock_;
  pthread_mutex_t access_log_lock_;
  std::atomic<uint64_t> flags_;
};
static const uint64_t SHMEM_FLAGS_INITIALIZING = 0x1;

/**
 * Initialize the shared memory segment, depending on whether we are the first running
 * envoy, or a host restarted envoy process.
 *
 * @param base_id uint32_t that is the base id flag used to start this Envoy.
 * @param restart_epoch uint32_t the restart epoch flag used to start this Envoy.
 */
SharedMemory* attachSharedMemory(uint32_t base_id, uint32_t restart_epoch);

/**
 * Initialize a pthread mutex for process shared locking.
 */
void initializeMutex(pthread_mutex_t& mutex);

/**
 * Implementation of Thread::BasicLockable that operates on a process shared pthread mutex.
 */
class ProcessSharedMutex : public Thread::BasicLockable {
public:
  ProcessSharedMutex(pthread_mutex_t& mutex) : mutex_(mutex) {}

  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override {
    // Deal with robust handling here. If the other process dies without unlocking, we are going
    // to die shortly but try to make sure that we can handle any signals, etc. that happen without
    // getting into a further messed up state.
    int rc = pthread_mutex_lock(&mutex_);
    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
  }

  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override {
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

  void unlock() ABSL_UNLOCK_FUNCTION() override {
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
  HotRestartImpl(uint32_t base_id, uint32_t restart_epoch);

  // Server::HotRestart
  void drainParentListeners() override;
  int duplicateParentListenSocket(const std::string& address) override;
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) override;
  void sendParentAdminShutdownRequest(time_t& original_start_time) override;
  void sendParentTerminateRequest() override;
  ServerStatsFromParent mergeParentStatsIfAny(Stats::StoreRoot& stats_store) override;
  void shutdown() override;
  uint32_t baseId() override;
  std::string version() override;
  Thread::BasicLockable& logLock() override { return log_lock_; }
  Thread::BasicLockable& accessLogLock() override { return access_log_lock_; }

  /**
   * envoy --hot_restart_version doesn't initialize Envoy, but computes the version string
   * based on the configured options.
   */
  static std::string hotRestartVersion();

private:
  uint32_t base_id_;
  uint32_t scaled_base_id_;
  HotRestartingChild as_child_;
  HotRestartingParent as_parent_;
  // This pointer is shared memory, and is expected to exist until process end.
  // It will automatically be unmapped when the process terminates.
  SharedMemory* shmem_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
};

} // namespace Server
} // namespace Envoy
