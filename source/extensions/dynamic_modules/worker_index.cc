#include "source/extensions/dynamic_modules/worker_index.h"

#include "source/common/common/assert.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

uint32_t parseWorkerIndexFromDispatcherName(absl::string_view dispatcher_name) {
  // `SimpleAtoi` leaves its output unspecified on failure, so fall back to zero on a malformed
  // name.
  const auto separator = dispatcher_name.find_first_of('_');
  if (separator == absl::string_view::npos) {
    IS_ENVOY_BUG("worker name is not in expected format worker_{index}");
    return 0;
  }
  uint32_t worker_index = 0;
  if (!absl::SimpleAtoi(dispatcher_name.substr(separator + 1), &worker_index)) {
    IS_ENVOY_BUG("failed to parse worker index from name");
    return 0;
  }
  return worker_index;
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
