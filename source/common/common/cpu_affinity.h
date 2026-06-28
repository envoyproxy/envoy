#pragma once

#include <cstdint>
#include <vector>

namespace Envoy {
namespace Thread {

// Returns the CPUs in the process affinity mask in ascending order. Empty when the platform has no
// affinity support or the query fails.
std::vector<uint32_t> cpuAffinitySet();

// Returns the first `worker_count` CPUs of the process affinity mask, used to pin worker i to entry
// i. Empty when fewer CPUs are available than workers, which disables worker CPU pinning.
std::vector<uint32_t> workerCpuAssignment(uint32_t worker_count);

} // namespace Thread
} // namespace Envoy
