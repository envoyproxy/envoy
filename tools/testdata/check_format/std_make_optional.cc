#include <optional>

namespace Envoy {
    void foo() {
      uint64_t value = 1;
      uint64_t optional_value = std::make_optional<uint64_t>(value);
    }
} // namespace Envoy
