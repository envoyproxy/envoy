#include "source/common/common/cpu_affinity.h"

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>

#include "source/common/api/os_sys_calls_impl_linux.h"
#endif

namespace Envoy {
namespace Thread {

std::vector<uint32_t> cpuAffinitySet() {
#if defined(__linux__)
  cpu_set_t mask;
  CPU_ZERO(&mask);
  // Read the process mask via `getpid()`. The worker threads inherit it at startup.
  const Api::SysCallIntResult result =
      Api::LinuxOsSysCallsSingleton::get().sched_getaffinity(getpid(), sizeof(cpu_set_t), &mask);
  if (result.return_value_ != 0) {
    return {};
  }
  std::vector<uint32_t> cpus;
  for (uint32_t cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (CPU_ISSET(cpu, &mask)) {
      cpus.push_back(cpu);
    }
  }
  return cpus;
#else
  return {};
#endif
}

std::vector<uint32_t> workerCpuAssignment(uint32_t worker_count) {
  std::vector<uint32_t> cpus = cpuAffinitySet();
  if (worker_count == 0 || cpus.size() < worker_count) {
    return {};
  }
  cpus.resize(worker_count);
  return cpus;
}

} // namespace Thread
} // namespace Envoy
