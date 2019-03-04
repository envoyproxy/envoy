#include "server/hot_restart_impl.h"

#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/stats/raw_stat_data.h"
#include "common/stats/stats_options_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

// Increment this whenever there is a shared memory / RPC change that will prevent a hot restart
// from working. Operations code can then cope with this and do a full restart.
const uint64_t SharedMemory::VERSION = 11;

static BlockMemoryHashSetOptions blockMemHashOptions(uint64_t max_stats) {
  BlockMemoryHashSetOptions hash_set_options;
  hash_set_options.capacity = max_stats;

  // https://stackoverflow.com/questions/3980117/hash-table-why-size-should-be-prime
  hash_set_options.num_slots = Primes::findPrimeLargerThan(hash_set_options.capacity / 2);
  return hash_set_options;
}

SharedMemory& SharedMemory::initialize(uint64_t stats_set_size, const Options& options) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  

  const uint64_t entry_size = Stats::RawStatData::structSizeWithOptions(options.statsOptions());
  const uint64_t total_size = sizeof(SharedMemory) + stats_set_size;

  int flags = O_RDWR | O_CREAT | O_EXCL;
  // TODO TODO can leave out this transitional stuff, and skip straight to ripping out shmem
  // entirely, if it's ok to have all of that in the same PR.
  // TODO(fredlas) temporary setup: "shared" memory that only you actually use. All of this shared
  // memory stuff is getting removed in a followup PR.
  const std::string transitional_shmem_name = fmt::format("/envoy_temp_shared_memory_{}", options.restartEpoch() % 3);
  // If we are meant to be first, attempt to unlink a previous shared memory instance. If this
  // is a clean restart this should then allow the shm_open() call below to succeed.
  os_sys_calls.shmUnlink(transitional_shmem_name.c_str());
  
  
  // We are removing shared memory. Get rid of the shared memory that an Envoy of the previous
  // hot restart compatibility version would have been using.
  const std::string original_shmem_name = fmt::format("/envoy_shared_memory_{}", options.baseId());
  os_sys_calls.shmUnlink(original_shmem_name.c_str());
  

  const Api::SysCallIntResult result =
      os_sys_calls.shmOpen(transitional_shmem_name.c_str(), flags, S_IRUSR | S_IWUSR);
  if (result.rc_ == -1) {
    PANIC(fmt::format("cannot open shared memory region {} check user permissions. Error: {}",
                      transitional_shmem_name, strerror(result.errno_)));
  }

  if (options.restartEpoch() == 0) {
    const Api::SysCallIntResult truncateRes = os_sys_calls.ftruncate(result.rc_, total_size);
    RELEASE_ASSERT(truncateRes.rc_ != -1, "");
  }

  const Api::SysCallPtrResult mmapRes =
      os_sys_calls.mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, result.rc_, 0);
  SharedMemory* shmem = reinterpret_cast<SharedMemory*>(mmapRes.rc_);
  RELEASE_ASSERT(shmem != MAP_FAILED, "");
  RELEASE_ASSERT((reinterpret_cast<uintptr_t>(shmem) % alignof(decltype(shmem))) == 0, "");

  if (options.restartEpoch() == 0) {
    shmem->size_ = total_size;
    shmem->version_ = VERSION;
    shmem->max_stats_ = options.maxStats();
    shmem->entry_size_ = entry_size;
    shmem->initializeMutex(shmem->log_lock_);
    shmem->initializeMutex(shmem->access_log_lock_);
    shmem->initializeMutex(shmem->stat_lock_);
  } else {
    RELEASE_ASSERT(shmem->size_ == total_size, "");
    RELEASE_ASSERT(shmem->version_ == VERSION, "");
    RELEASE_ASSERT(shmem->max_stats_ == options.maxStats(), "");
    RELEASE_ASSERT(shmem->entry_size_ == entry_size, "");
  }
  // Stats::RawStatData must be naturally aligned for atomics to work properly.
  RELEASE_ASSERT(
      (reinterpret_cast<uintptr_t>(shmem->stats_set_data_) % alignof(Stats::RawStatDataSet)) == 0,
      "");

  // Here we catch the case where a new Envoy starts up when the current Envoy has not yet fully
  // initialized. The startup logic is quite complicated, and it's not worth trying to handle this
  // in a finer way. This will cause the startup to fail with an error code early, without
  // affecting any currently running processes. The process runner should try again later with some
  // back off and with the same hot restart epoch number.
  uint64_t old_flags = shmem->flags_.fetch_or(Flags::INITIALIZING);
  if (old_flags & Flags::INITIALIZING) {
    throw EnvoyException("previous envoy process is still initializing");
  }
  return *shmem;
}

void SharedMemory::initializeMutex(pthread_mutex_t& mutex) {
  pthread_mutexattr_t attribute;
  pthread_mutexattr_init(&attribute);
  pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
  pthread_mutex_init(&mutex, &attribute);
}

std::string SharedMemory::version(uint64_t max_num_stats,
                                  const Stats::StatsOptions& stats_options) {
  return fmt::format("{}.{}.{}.{}", VERSION, sizeof(SharedMemory), max_num_stats,
                     stats_options.maxNameLength());
}

HotRestartImpl::HotRestartImpl(const Options& options)
    : as_child_(HotRestartingChild(options.baseId(), options.restartEpoch())),
      as_parent_(HotRestartingParent(options.baseId(), options.restartEpoch())), options_(options),
      stats_set_options_(blockMemHashOptions(options.maxStats())),
      shmem_(SharedMemory::initialize(
          Stats::RawStatDataSet::numBytes(stats_set_options_, options_.statsOptions()), options_)),
      log_lock_(shmem_.log_lock_), access_log_lock_(shmem_.access_log_lock_),
      stat_lock_(shmem_.stat_lock_) {
  {
    // We must hold the stat lock when attaching to an existing memory segment
    // because it might be actively written to while we sanityCheck it.
    Thread::LockGuard lock(stat_lock_);
    stats_set_ =
        std::make_unique<Stats::RawStatDataSet>(stats_set_options_, options.restartEpoch() == 0,
                                                shmem_.stats_set_data_, options_.statsOptions());
  }
  stats_allocator_ = std::make_unique<Stats::RawStatDataAllocator>(stat_lock_, *stats_set_,
                                                                   options_.statsOptions());
  // If our parent ever goes away just terminate us so that we don't have to rely on ops/launching
  // logic killing the entire process tree. We should never exist without our parent.
  int rc = prctl(PR_SET_PDEATHSIG, SIGTERM);
  RELEASE_ASSERT(rc != -1, "");
}

void HotRestartImpl::drainParentListeners() {
  as_child_.drainParentListeners();
  // At this point we are initialized and a new Envoy can startup if needed.
  shmem_.flags_ &= ~SharedMemory::Flags::INITIALIZING;
}

int HotRestartImpl::duplicateParentListenSocket(const std::string& address) {
  return as_child_.duplicateParentListenSocket(address);
}

std::unique_ptr<envoy::api::v2::core::HotRestartMessage> HotRestartImpl::getParentStats() {
  return as_child_.getParentStats();
}

void HotRestartImpl::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  as_parent_.initialize(dispatcher, server);
}

void HotRestartImpl::shutdownParentAdmin(ShutdownParentAdminInfo& info) {
  as_child_.shutdownParentAdmin(info);
}

void HotRestartImpl::terminateParent() { as_child_.terminateParent(); }

void HotRestartImpl::shutdown() {
  as_parent_.shutdown();
}

std::string HotRestartImpl::version() {
  Thread::LockGuard lock(stat_lock_);
  return versionHelper(shmem_.maxStats(), options_.statsOptions(), *stats_set_);
}

// Called from envoy --hot-restart-version -- needs to instantiate a RawStatDataSet so it
// can generate the version string.
std::string HotRestartImpl::hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len) {
  Stats::StatsOptionsImpl stats_options;
  stats_options.max_obj_name_length_ = max_stat_name_len - stats_options.maxStatSuffixLength();

  const BlockMemoryHashSetOptions hash_set_options = blockMemHashOptions(max_num_stats);
  const uint64_t bytes = Stats::RawStatDataSet::numBytes(hash_set_options, stats_options);
  std::unique_ptr<uint8_t[]> mem_buffer_for_dry_run_(new uint8_t[bytes]);

  Stats::RawStatDataSet stats_set(hash_set_options, true /* init */, mem_buffer_for_dry_run_.get(),
                                  stats_options);

  return versionHelper(max_num_stats, stats_options, stats_set);
}

std::string HotRestartImpl::versionHelper(uint64_t max_num_stats,
                                          const Stats::StatsOptions& stats_options,
                                          Stats::RawStatDataSet& stats_set) {
  return SharedMemory::version(max_num_stats, stats_options) + "." +
         stats_set.version(stats_options);
}

} // namespace Server
} // namespace Envoy
