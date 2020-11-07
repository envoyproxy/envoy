#include <chrono>

namespace Envoy {

std::chrono::duration<long int, std::nano> foo_int() {
  return std::chrono::steady_clock::duration(0);
}

std::chrono::duration<long int, std::nano> foo_decimal() {
  return std::chrono::steady_clock::duration(0.0);
}

} // namespace Envoy
