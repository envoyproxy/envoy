#include "test/test_common/global.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Test {

Globals& Globals::instance() {
  static Globals* globals = new Globals;
  return *globals;
}

std::string Globals::describeActiveSingletonsHelper() {
  std::string ret;
  Thread::ReleasableLockGuard map_lock(map_mutex_);
  for (auto& p : singleton_map_) {
    SingletonSharedPtr singleton = p.second.lock();
    if (singleton != nullptr) {
      absl::StrAppend(&ret, "Unexpected active singleton: ", p.first, "\n");
    }
  }
  return ret;
}

} // namespace Test
} // namespace Envoy
