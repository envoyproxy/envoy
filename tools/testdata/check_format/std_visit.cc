#include <variant>

namespace Envoy {
  struct SomeVisitorFunctor {
    template<typename T>
    void operator()(const T& i) const {}
  };

  void foo() {
    absl::variant<int, double> x{12};
    SomeVisitorFunctor visitor;
    std::visit(visitor, x);
  }
} // namespace Envoy
