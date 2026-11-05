#include "source/common/event/worker_id.h"

#include <cstdint>
#include <optional>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Event {

std::optional<uint32_t> workerIdFromThreadName(absl::string_view thread_name) {
  constexpr absl::string_view prefix = "worker_";
  if (!absl::StartsWith(thread_name, prefix)) {
    return std::nullopt;
  }
  const absl::string_view id = thread_name.substr(prefix.size());
  if (id.empty()) {
    return std::nullopt;
  }
  for (const char c : id) {
    if (!absl::ascii_isdigit(c)) {
      return std::nullopt;
    }
  }

  uint32_t worker_id = 0;
  if (!absl::SimpleAtoi(id, &worker_id)) {
    // Out of range.
    return std::nullopt;
  }
  return worker_id;
}

} // namespace Event
} // namespace Envoy
