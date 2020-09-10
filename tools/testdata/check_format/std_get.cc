#include <string>
#include <variant>

namespace Envoy {
  void foo() {
    absl::variant<std::string> x("abc");
    auto y = std::get<std::string>(x);
  }
} // namespace Envoy
