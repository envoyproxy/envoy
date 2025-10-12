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

#include "absl/strings/ascii.h"

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
  uint32_t affinity_count = OptionsImplPlatformLinux::getCpuAffinityCount(hw_threads);

  uint32_t cgroup_limit = hw_threads; // Fallback to hardware threads if `cgroup` detection fails

  // Check environment variable for cgroup detection (safe during early startup)
  const char* env_value = std::getenv("ENVOY_CGROUP_CPU_DETECTION");
  bool enable_cgroup_detection = true; // Default: enabled

  if (env_value != nullptr) {
    std::string value = absl::AsciiStrToLower(env_value);
    enable_cgroup_detection = (value != "false");
  }

  if (enable_cgroup_detection) {
    Filesystem::InstanceImpl fs;
    auto& detector = CgroupDetectorSingleton::get();
    absl::optional<uint32_t> detected_limit = detector.getCpuLimit(fs);
    if (detected_limit.has_value()) {
      cgroup_limit = detected_limit.value();
    }
  }

  uint32_t effective_count = std::min({hw_threads, affinity_count, cgroup_limit});
  return std::max(1U, effective_count);
}

} // namespace Envoy
