#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include "source/server/options_impl_platform_linux.h"

#include <sched.h>

#include <thread>

#include "source/common/api/os_sys_calls_impl_linux.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/server/cgroup_cpu_util.h"
#include "source/server/options_impl_platform.h"

namespace Envoy {

uint32_t OptionsImplPlatformLinux::getCpuAffinityCount(unsigned int hardware_threads) {
  unsigned int threads = 0;
  pid_t pid = getpid();
  cpu_set_t mask;
  auto& linux_os_syscalls = Api::LinuxOsSysCallsSingleton::get();

  CPU_ZERO(&mask);
  const Api::SysCallIntResult result =
      linux_os_syscalls.sched_getaffinity(pid, sizeof(cpu_set_t), &mask);
  if (result.return_value_ == -1) {
    // Fall back to number of hardware threads.
    return hardware_threads;
  }

  threads = CPU_COUNT(&mask);

  // Sanity check.
  if (threads > 0 && threads <= hardware_threads) {
    return threads;
  }

  return hardware_threads;
}

uint32_t OptionsImplPlatform::getCpuCount() {
  // Step 1: Get hardware thread count
  unsigned int hardware_threads = std::max(1U, std::thread::hardware_concurrency());

  // Step 2: Get CPU affinity count (respects taskset, cpuset, etc.)
  uint32_t affinity_count = OptionsImplPlatformLinux::getCpuAffinityCount(hardware_threads);

  // Step 3: Get cgroup CPU limit with hierarchy scanning
  Filesystem::InstanceImpl fs;
  uint32_t cgroup_limit = CgroupCpuUtil::getCpuLimit(fs, hardware_threads);

  // Step 4: Take minimum of all constraints for container-aware CPU detection
  uint32_t effective_count = std::min({hardware_threads, affinity_count, cgroup_limit});

  // Step 5: Ensure minimum of 1 CPU
  return std::max(1U, effective_count);
}

} // namespace Envoy
