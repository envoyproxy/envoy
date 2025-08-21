#pragma once

#include "envoy/api/os_sys_calls_hot_restart.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Api {

class HotRestartOsSysCallsImpl : public HotRestartOsSysCalls {
public:
  // Api::HotRestartOsSysCalls
  SysCallIntResult shmOpen(const char* name, int oflag, mode_t mode) override;
  SysCallIntResult shmUnlink(const char* name) override;
};

using HotRestartOsSysCallsSingleton = ThreadSafeSingleton<HotRestartOsSysCallsImpl>;

} // namespace Api
} // namespace Envoy
