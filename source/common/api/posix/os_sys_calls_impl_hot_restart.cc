#include <cerrno>

#include "common/api/os_sys_calls_impl_hot_restart.h"

namespace Envoy {
namespace Api {

SysCallIntResult HotRestartOsSysCallsImpl::shmOpen(const char* name, int oflag, mode_t mode) {
  const int rc = ::shm_open(name, oflag, mode);
  return {rc, errno};
}

SysCallIntResult HotRestartOsSysCallsImpl::shmUnlink(const char* name) {
  const int rc = ::shm_unlink(name);
  return {rc, errno};
}

} // namespace Api
} // namespace Envoy
