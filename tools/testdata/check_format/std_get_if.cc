#include <variant>

namespace Envoy {
  void foo() {
    absl::variant<int, float> x{12};
    auto y = std::get_if<int>(&x);
  }
} // namespace Envoy
