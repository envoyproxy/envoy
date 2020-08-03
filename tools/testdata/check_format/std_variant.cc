#include <variant>

namespace Envoy {
    void bar() {
        std::variant<int, float> foo;
    }
} // namespace Envoy
