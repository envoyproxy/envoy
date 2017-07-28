#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/stats/stats_impl.h"

namespace Envoy {
namespace Server {

/**
 * Shared memory segment. This structure is laid directly into shared memory and is used amongst
 * all running envoy processes.
 */
class SharedMemory {
public:
  static std::string version();

private:
  struct Flags {
    static const uint64_t INITIALIZING = 0x1;
  };

  SharedMemory() {}

  /**
   * Initialize the shared memory segment, depending on whether we should be the first running
   * envoy, or a host restarted envoy process.
   */
  static SharedMemory& initialize(Options& options);

  /**
   * Initialize a pthread mutex for process shared locking.
   */
  void initializeMutex(pthread_mutex_t& mutex);

  static const uint64_t VERSION;

  uint64_t size_;
  uint64_t version_;
  std::atomic<uint64_t> flags_;
  pthread_mutex_t log_lock_;
  pthread_mutex_t access_log_lock_;
  pthread_mutex_t stat_lock_;
  pthread_mutex_t init_lock_;
  std::array<Stats::RawStatData, 16384> stats_slots_;

  friend class HotRestartImpl;
};

/**
 * Implementation of Thread::BasicLockable that operates on a process shared pthread mutex.
 */
class ProcessSharedMutex : public Thread::BasicLockable {
public:
  ProcessSharedMutex(pthread_mutex_t& mutex) : mutex_(mutex) {}

  void lock() override {
    // Deal with robust handling here. If the other process dies without unlocking, we are going
    // to die shortly but try to make sure that we can handle any signals, etc. that happen without
    // getting into a further messed up state.
    int rc = pthread_mutex_lock(&mutex_);
    ASSERT(rc == 0 || rc == EOWNERDEAD);
#ifdef PTHREAD_MUTEX_ROBUST
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
#endif
  }

  bool try_lock() override {
    int rc = pthread_mutex_trylock(&mutex_);
    if (rc == EBUSY) {
      return false;
    }

    ASSERT(rc == 0 || rc == EOWNERDEAD);
#ifdef PTHREAD_MUTEX_ROBUST
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
#endif
    return true;
  }

  void unlock() override {
    int rc = pthread_mutex_unlock(&mutex_);
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
  }

private:
  pthread_mutex_t& mutex_;
};

} // namespace Server
} // namespace Envoy
