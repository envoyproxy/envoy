#include "exe/shared_memory.h"

#include <signal.h>
#include <sys/mman.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"

#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

// Increment this whenever there is a shared memory / RPC change that will prevent a hot restart
// from working. Operations code can then cope with this and do a full restart.
const uint64_t SharedMemory::VERSION = 8;

SharedMemory& SharedMemory::initialize(Options& options) {
  int flags = O_RDWR;
  std::string shmem_name = fmt::format("/envoy_shared_memory_{}", options.baseId());
  if (options.restartEpoch() == 0) {
    flags |= O_CREAT | O_EXCL;

    // If we are meant to be first, attempt to unlink a previous shared memory instance. If this
    // is a clean restart this should then allow the shm_open() call below to succeed.
    shm_unlink(shmem_name.c_str());
  }

  int shmem_fd = shm_open(shmem_name.c_str(), flags, S_IRUSR | S_IWUSR);
  if (shmem_fd == -1) {
    PANIC(fmt::format("cannot open shared memory region {} check user permissions", shmem_name));
  }

  if (options.restartEpoch() == 0) {
    int rc = ftruncate(shmem_fd, sizeof(SharedMemory));
    RELEASE_ASSERT(rc != -1);
    UNREFERENCED_PARAMETER(rc);
  }

  SharedMemory* shmem = reinterpret_cast<SharedMemory*>(
      mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0));
  RELEASE_ASSERT(shmem != MAP_FAILED);

  if (options.restartEpoch() == 0) {
    shmem->size_ = sizeof(SharedMemory);
    shmem->version_ = VERSION;
    shmem->initializeMutex(shmem->log_lock_);
    shmem->initializeMutex(shmem->access_log_lock_);
    shmem->initializeMutex(shmem->stat_lock_);
    shmem->initializeMutex(shmem->init_lock_);
  } else {
    RELEASE_ASSERT(shmem->size_ == sizeof(SharedMemory));
    RELEASE_ASSERT(shmem->version_ == VERSION);
  }

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
#ifdef PTHREAD_MUTEX_ROBUST
  pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
#endif
  pthread_mutex_init(&mutex, &attribute);
}

std::string SharedMemory::version() { return fmt::format("{}.{}", VERSION, sizeof(SharedMemory)); }

} // namespace Server
} // namespace Envoy
