#include "source/server/hot_restart_impl.h"

#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/un.h>

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/server/instance.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/api/os_sys_calls_impl_hot_restart.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

SharedMemory* attachSharedMemory(uint32_t base_id, uint32_t restart_epoch) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::HotRestartOsSysCalls& hot_restart_os_sys_calls = Api::HotRestartOsSysCallsSingleton::get();

  int flags = O_RDWR;
  const std::string shmem_name = fmt::format("/envoy_shared_memory_{}", base_id);
  if (restart_epoch == 0) {
    flags |= O_CREAT | O_EXCL;

    // If we are meant to be first, attempt to unlink a previous shared memory instance. If this
    // is a clean restart this should then allow the shm_open() call below to succeed.
    hot_restart_os_sys_calls.shmUnlink(shmem_name.c_str());
  }

  const Api::SysCallIntResult result =
      hot_restart_os_sys_calls.shmOpen(shmem_name.c_str(), flags, S_IRUSR | S_IWUSR);
  if (result.return_value_ == -1) {
    PANIC(fmt::format("cannot open shared memory region {} check user permissions. Error: {}",
                      shmem_name, errorDetails(result.errno_)));
  }

  if (restart_epoch == 0) {
    const Api::SysCallIntResult truncateRes =
        os_sys_calls.ftruncate(result.return_value_, sizeof(SharedMemory));
    RELEASE_ASSERT(truncateRes.return_value_ != -1, "");
  }

  const Api::SysCallPtrResult mmapRes = os_sys_calls.mmap(
      nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, result.return_value_, 0);
  SharedMemory* shmem = reinterpret_cast<SharedMemory*>(mmapRes.return_value_);
  RELEASE_ASSERT(shmem != MAP_FAILED, "");
  RELEASE_ASSERT((reinterpret_cast<uintptr_t>(shmem) % alignof(decltype(shmem))) == 0, "");

  if (restart_epoch == 0) {
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

// The base id is automatically scaled by 10 to prevent overlap of domain socket names when
// multiple Envoys with different base-ids run on a single host. Note that older versions of Envoy
// performed the multiplication in OptionsImpl which produced incorrect server info output.
// TODO(zuercher): ideally, the base_id would be separated from the restart_epoch in
// the socket names to entirely prevent collisions between consecutive base ids.
HotRestartImpl::HotRestartImpl(uint32_t base_id, uint32_t restart_epoch,
                               const std::string& socket_path, mode_t socket_mode,
                               bool skip_hot_restart_on_no_parent, bool skip_parent_stats)
    : base_id_(base_id), scaled_base_id_(base_id * 10),
      as_child_(HotRestartingChild(scaled_base_id_, restart_epoch, socket_path, socket_mode,
                                   skip_hot_restart_on_no_parent, skip_parent_stats)),
      as_parent_(HotRestartingParent(scaled_base_id_, restart_epoch, socket_path, socket_mode)),
      shmem_(attachSharedMemory(scaled_base_id_, restart_epoch)), log_lock_(shmem_->log_lock_),
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

int HotRestartImpl::duplicateParentListenSocket(const std::string& address, uint32_t worker_index) {
  return as_child_.duplicateParentListenSocket(address, worker_index);
}

void HotRestartImpl::registerUdpForwardingListener(
    Network::Address::InstanceConstSharedPtr address,
    std::shared_ptr<Network::UdpListenerConfig> listener_config) {
  as_child_.registerUdpForwardingListener(address, listener_config);
}

OptRef<Network::ParentDrainedCallbackRegistrar> HotRestartImpl::parentDrainedCallbackRegistrar() {
  return as_child_;
}

void HotRestartImpl::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  as_parent_.initialize(dispatcher, server);
  as_child_.initialize(dispatcher);
}

absl::optional<HotRestart::AdminShutdownResponse> HotRestartImpl::sendParentAdminShutdownRequest() {
  return as_child_.sendParentAdminShutdownRequest();
}

void HotRestartImpl::sendParentTerminateRequest() { as_child_.sendParentTerminateRequest(); }

HotRestart::ServerStatsFromParent
HotRestartImpl::mergeParentStatsIfAny(Stats::StoreRoot& stats_store) {
  std::unique_ptr<envoy::HotRestartMessage> wrapper_msg = as_child_.getParentStats();
  ServerStatsFromParent response;
  // getParentStats() will happily and cleanly return nullptr if we have no parent.
  if (wrapper_msg) {
    as_child_.mergeParentStats(stats_store, wrapper_msg->reply().stats());
    response.parent_memory_allocated_ = wrapper_msg->reply().stats().memory_allocated();
    response.parent_connections_ = wrapper_msg->reply().stats().num_connections();
  }
  return response;
}

void HotRestartImpl::shutdown() {
  as_parent_.shutdown();
  as_child_.shutdown();
}

uint32_t HotRestartImpl::baseId() { return base_id_; }
std::string HotRestartImpl::version() { return hotRestartVersion(); }

std::string HotRestartImpl::hotRestartVersion() {
  return fmt::format("{}.{}", HOT_RESTART_VERSION, sizeof(SharedMemory));
}

} // namespace Server
} // namespace Envoy
