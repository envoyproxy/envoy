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

SharedMemory* attachSharedMemory(const Options& options) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();

  int flags = O_RDWR;
  const std::string shmem_name = fmt::format("/envoy_shared_memory_{}", options.baseId());
  if (options.restartEpoch() == 0) {
    flags |= O_CREAT | O_EXCL;

    // If we are meant to be first, attempt to unlink a previous shared memory instance. If this
    // is a clean restart this should then allow the shm_open() call below to succeed.
    os_sys_calls.shmUnlink(shmem_name.c_str());
  }

  const Api::SysCallIntResult result =
      os_sys_calls.shmOpen(shmem_name.c_str(), flags, S_IRUSR | S_IWUSR);
  if (result.rc_ == -1) {
    PANIC(fmt::format("cannot open shared memory region {} check user permissions. Error: {}",
                      shmem_name, strerror(result.errno_)));
  }

  if (options.restartEpoch() == 0) {
    const Api::SysCallIntResult truncateRes =
        os_sys_calls.ftruncate(result.rc_, sizeof(SharedMemory));
    RELEASE_ASSERT(truncateRes.rc_ != -1, "");
  }

  const Api::SysCallPtrResult mmapRes = os_sys_calls.mmap(
      nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, result.rc_, 0);
  SharedMemory* shmem = reinterpret_cast<SharedMemory*>(mmapRes.rc_);
  RELEASE_ASSERT(shmem != MAP_FAILED, "");
  RELEASE_ASSERT((reinterpret_cast<uintptr_t>(shmem) % alignof(decltype(shmem))) == 0, "");

  if (options.restartEpoch() == 0) {
    shmem->size_ = sizeof(SharedMemory);
    shmem->version_ = HOT_RESTART_VERSION;
    initializeMutex(shmem->log_lock_);
    initializeMutex(shmem->access_log_lock_);
  } else {
    RELEASE_ASSERT(shmem->size_ == sizeof(SharedMemory),
                   "Hot restart SharedMemory size mismatch! You must have hot restarted into a "
                   "not-hot-restart-compatible new version of Envoy.");
    RELEASE_ASSERT(shmem->version_ == HOT_RESTART_VERSION,
                   "Hot restart version mismatch! You must have hot restarted into a "
                   "not-hot-restart-compatible new version of Envoy.");
  }

  // Here we catch the case where a new Envoy starts up when the current Envoy has not yet fully
  // initialized. The startup logic is quite complicated, and it's not worth trying to handle this
  // in a finer way. This will cause the startup to fail with an error code early, without
  // affecting any currently running processes. The process runner should try again later with some
  // back off and with the same hot restart epoch number.
  uint64_t old_flags = shmem->flags_.fetch_or(SHMEM_FLAGS_INITIALIZING);
  if (old_flags & SHMEM_FLAGS_INITIALIZING) {
    throw EnvoyException("previous envoy process is still initializing");
  }
  return shmem;
}

void initializeMutex(pthread_mutex_t& mutex) {
  pthread_mutexattr_t attribute;
  pthread_mutexattr_init(&attribute);
  pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
  pthread_mutex_init(&mutex, &attribute);
}

HotRestartImpl::HotRestartImpl(const Options& options)
    : as_child_(HotRestartingChild(options.baseId(), options.restartEpoch())),
      as_parent_(HotRestartingParent(options.baseId(), options.restartEpoch())),
      shmem_(attachSharedMemory(options)), options_(options), log_lock_(shmem_->log_lock_),
      access_log_lock_(shmem_->access_log_lock_) {
  // If our parent ever goes away just terminate us so that we don't have to rely on ops/launching
  // logic killing the entire process tree. We should never exist without our parent.
  int rc = prctl(PR_SET_PDEATHSIG, SIGTERM);
  RELEASE_ASSERT(rc != -1, "");
}

void HotRestartImpl::drainParentListeners() {
  as_child_.drainParentListeners();
  // At this point we are initialized and a new Envoy can startup if needed.
  shmem_->flags_ &= ~SHMEM_FLAGS_INITIALIZING;
}

int HotRestartImpl::duplicateParentListenSocket(const std::string& address) {
  return as_child_.duplicateParentListenSocket(address);
}

std::unique_ptr<envoy::HotRestartMessage> HotRestartImpl::getParentStats() {
  return as_child_.getParentStats();
}

void HotRestartImpl::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  as_parent_.initialize(dispatcher, server);
}

void HotRestartImpl::shutdownParentAdmin(ShutdownParentAdminInfo& info) {
  as_child_.shutdownParentAdmin(info);
}

void HotRestartImpl::terminateParent() { as_child_.terminateParent(); }

void HotRestartImpl::mergeParentStats(Stats::StoreRoot& stats_store,
                                      const envoy::HotRestartMessage::Reply::Stats& stats_proto) {
  as_child_.mergeParentStats(stats_store, stats_proto);
}

void HotRestartImpl::shutdown() { as_parent_.shutdown(); }

std::string HotRestartImpl::version() {
  return hotRestartVersion(options_.statsOptions().maxNameLength());
}

std::string HotRestartImpl::hotRestartVersion(uint64_t max_stat_name_len) {
  return fmt::format("{}.{}.{}", HOT_RESTART_VERSION, sizeof(SharedMemory), max_stat_name_len);
}

} // namespace Server
} // namespace Envoy
