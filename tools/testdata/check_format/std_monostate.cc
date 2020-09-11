#include <variant>

namespace Envoy {
  struct S {
    S(int i) : i(i) {}
    int i;
  };

  void foo() {
    absl::variant<std::monostate, S> x;
  }
} // namespace Envoy
