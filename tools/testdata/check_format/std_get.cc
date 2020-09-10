#include <variant>

namespace Envoy {
  void foo() {
    absl::variant<int> x(5);
    auto y = std::get<int>(x);
  }
} // namespace Envoy
