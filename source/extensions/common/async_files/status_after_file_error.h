#pragma once

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

absl::Status statusAfterFileError();

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
