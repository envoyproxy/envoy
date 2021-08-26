#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include "source/server/options_impl_platform_linux.h"

#include <sched.h>

#include <thread>

#include "source/common/api/os_sys_calls_impl_linux.h"
#include "source/server/options_impl_platform.h"

namespace Envoy {

uint32_t OptionsImplPlatformLinux::getCpuAffinityCount(unsigned int hw_threads) {
  unsigned int threads = 0;
  pid_t pid = getpid();
  cpu_set_t mask;
  auto& linux_os_syscalls = Api::LinuxOsSysCallsSingleton::get();

  CPU_ZERO(&mask);
  const Api::SysCallIntResult result =
      linux_os_syscalls.sched_getaffinity(pid, sizeof(cpu_set_t), &mask);
  if (result.return_value_ == -1) {
    // Fall back to number of hardware threads.
    return hw_threads;
  }

  threads = CPU_COUNT(&mask);

  // Sanity check.
  if (threads > 0 && threads <= hw_threads) {
    return threads;
  }

  return hw_threads;
}

uint32_t OptionsImplPlatform::getCpuCount() {
  unsigned int hw_threads = std::max(1U, std::thread::hardware_concurrency());
  return OptionsImplPlatformLinux::getCpuAffinityCount(hw_threads);
}

} // namespace Envoy
