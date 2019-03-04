#pragma once

#include <memory>
#include <string>

namespace Envoy {
namespace Api {
/**
 * SysCallResult holds the rc and errno values resulting from a system call.
 */
template <typename T> struct SysCallResult {

  /**
   * The return code from the system call.
   */
  T rc_;

  /**
   * The errno value as captured after the system call.
   */
  int errno_;
};

typedef SysCallResult<int> SysCallIntResult;
typedef SysCallResult<ssize_t> SysCallSizeResult;
typedef SysCallResult<void*> SysCallPtrResult;
typedef SysCallResult<std::string> SysCallStringResult;
typedef SysCallResult<bool> SysCallBoolResult;

} // namespace Api
} // namespace Envoy
