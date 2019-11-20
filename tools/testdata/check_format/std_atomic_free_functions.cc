#include <atomic>

namespace Envoy {

void do_atomic_stuff() {
  std::atomic<bool> atomic_flag(false);
  std::atomic_store(&atomic_flag, true);
}

} // namespace Envoy

