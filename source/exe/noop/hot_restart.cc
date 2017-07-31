#include "exe/hot_restart.h"

#include <mutex>

namespace Envoy {
namespace Server {

HotRestartImpl::HotRestartImpl(Options& options)
    : shmem_(SharedMemory::initialize(options)), log_lock_(shmem_.log_lock_),
      access_log_lock_(shmem_.access_log_lock_), stat_lock_(shmem_.stat_lock_) {}

Stats::RawStatData* HotRestartImpl::alloc(const std::string& name) {
  // Try to find the existing slot in shared memory, otherwise allocate a new one.
  std::unique_lock<Thread::BasicLockable> lock(stat_lock_);
  for (Stats::RawStatData& data : shmem_.stats_slots_) {
    if (!data.initialized()) {
      data.initialize(name);
      return &data;
    } else if (data.matches(name)) {
      data.ref_count_++;
      return &data;
    }
  }

  return nullptr;
}

void HotRestartImpl::free(Stats::RawStatData& data) {
  // We must hold the lock since the reference decrement can race with an initialize above.
  std::unique_lock<Thread::BasicLockable> lock(stat_lock_);
  ASSERT(data.ref_count_ > 0);
  if (--data.ref_count_ > 0) {
    return;
  }

  memset(&data, 0, sizeof(Stats::RawStatData));
}

} // namespace Server
} // namespace Envoy
