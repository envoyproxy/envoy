#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include <sched.h>

#include <cerrno>

#include "common/api/os_sys_calls_impl_linux.h"

namespace Envoy {
namespace Api {

SysCallIntResult LinuxOsSysCallsImpl::sched_getaffinity(pid_t pid, size_t cpusetsize,
                                                        cpu_set_t* mask) {
  const int rc = ::sched_getaffinity(pid, cpusetsize, mask);
  return {rc, errno};
}

} // namespace Api
} // namespace Envoy
