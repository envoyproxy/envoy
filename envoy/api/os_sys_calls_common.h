#pragma once

#include <memory>
#include <string>

#include "envoy/common/platform.h"

namespace Envoy {
namespace Api {
/**
 * SysCallResult holds the rc and errno values resulting from a system call.
 */
template <typename T> struct SysCallResult {

  /**
   * The return code from the system call.
   */
  T return_value_;

  /**
   * The errno value as captured after the system call.
   */
  int errno_;
};

using SysCallIntResult = SysCallResult<int>;
using SysCallSizeResult = SysCallResult<ssize_t>;
using SysCallPtrResult = SysCallResult<void*>;
using SysCallStringResult = SysCallResult<std::string>;
using SysCallBoolResult = SysCallResult<bool>;
using SysCallSocketResult = SysCallResult<os_fd_t>;

} // namespace Api
} // namespace Envoy
