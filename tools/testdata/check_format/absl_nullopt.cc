#include <optional>

namespace Envoy {
std::optional<int> foo() { return absl::nullopt; }
} // namespace Envoy
