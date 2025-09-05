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
  // Step 1: Get hardware thread count
  unsigned int hw_threads = std::max(1U, std::thread::hardware_concurrency());
  
  // Step 2: Get CPU affinity count (respects taskset, cpuset, etc.)
  uint32_t affinity_count = OptionsImplPlatformLinux::getCpuAffinityCount(hw_threads);
  
  // Step 3: Get cgroup CPU limit (Go-style algorithm with hierarchy scanning)
  Filesystem::InstanceImpl fs;
  uint32_t cgroup_limit = CgroupCpuUtil::getCpuLimit(fs, hw_threads);
  
  // Step 4: Apply Go's GOMAXPROCS algorithm - minimum of all constraints
  uint32_t effective_count = std::min({hw_threads, affinity_count, cgroup_limit});
  
  // Step 5: Ensure minimum of 1 (modified from Go's max(2, ceil(effective_cpu_limit)))
  return std::max(1U, effective_count);
}

} // namespace Envoy
