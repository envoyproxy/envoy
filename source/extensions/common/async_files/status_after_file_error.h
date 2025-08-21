#pragma once

#include "envoy/api/os_sys_calls_common.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

absl::Status statusAfterFileError(int error_code);

template <typename T> absl::Status statusAfterFileError(Api::SysCallResult<T> result) {
  return statusAfterFileError(result.errno_);
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
