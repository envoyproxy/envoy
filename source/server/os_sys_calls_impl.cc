#include "server/os_sys_calls_impl.h"

#include <sys/types.h>
#include <unistd.h>

namespace Envoy {
namespace Server {

// These are in a separate compilation unit so that the tests don't need to link against -lrt for
// the definitions of these functions.

int OsSysCallsImpl::shmOpen(const char* name, int oflag, mode_t mode) {
  return ::shm_open(name, oflag, mode);
}

int OsSysCallsImpl::shmUnlink(const char* name) { return ::shm_unlink(name); }

int OsSysCallsImpl::ftruncate(int fd, off_t length) { return ::ftruncate(fd, length); }

void* OsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
  return ::mmap(addr, length, prot, flags, fd, offset);
}

} // namespace Server
} // namespace Envoy
