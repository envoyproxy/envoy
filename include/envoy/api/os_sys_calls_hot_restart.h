#pragma once

#ifndef WIN32
#include <sys/mman.h> // for mode_t

#endif

#include "envoy/api/os_sys_calls_common.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

class HotRestartOsSysCalls {
public:
  virtual ~HotRestartOsSysCalls() {}

  /**
   * @see shm_open (man 3 shm_open)
   */
  virtual SysCallIntResult shmOpen(const char* name, int oflag, mode_t mode) PURE;

  /**
   * @see shm_unlink (man 3 shm_unlink)
   */
  virtual SysCallIntResult shmUnlink(const char* name) PURE;
};

typedef std::unique_ptr<HotRestartOsSysCalls> HotRestartOsSysCallsPtr;

} // namespace Api
} // namespace Envoy
