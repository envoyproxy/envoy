#include <variant>

namespace Envoy {
  void foo() {
    absl::variant<int, double> x{12};
    auto y = std::holds_alternative<double>(x);
  }
} // namespace Envoy
