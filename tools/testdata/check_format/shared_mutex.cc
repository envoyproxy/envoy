#include <shared_mutex>

namespace Envoy {

void make_a_mutex() {
  std::shared_timed_mutex mutex;
}

} // namespace Envoy

