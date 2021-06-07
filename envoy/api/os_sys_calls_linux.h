#pragma once

#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include <sched.h>

#include "envoy/api/os_sys_calls_common.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

class LinuxOsSysCalls {
public:
  virtual ~LinuxOsSysCalls() = default;

  /**
   * @see sched_getaffinity (man 2 sched_getaffinity)
   */
  virtual SysCallIntResult sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t* mask) PURE;
};

using LinuxOsSysCallsPtr = std::unique_ptr<LinuxOsSysCalls>;

} // namespace Api
} // namespace Envoy
