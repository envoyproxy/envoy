#include <chrono>

namespace Envoy {

std::chrono::duration<long int, std::nano> foo() {
  return std::chrono::steady_clock::duration(12345);
}

} // namespace Envoy
