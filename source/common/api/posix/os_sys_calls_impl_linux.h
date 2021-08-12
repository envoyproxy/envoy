#pragma once

#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include "envoy/api/os_sys_calls_linux.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Api {

class LinuxOsSysCallsImpl : public LinuxOsSysCalls {
public:
  // Api::LinuxOsSysCalls
  SysCallIntResult sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t* mask) override;
};

using LinuxOsSysCallsSingleton = ThreadSafeSingleton<LinuxOsSysCallsImpl>;

} // namespace Api
} // namespace Envoy
