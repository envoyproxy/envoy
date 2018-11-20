#include "test/test_common/global.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Test {

Globals& Globals::instance() {
  static Globals* h = new Globals;
  return *h;
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

Globals::SingletonSharedPtr Globals::get(const std::string& type_name,
                                         const MakeObjectFn& make_object,
                                         const DeleteObjectFn& delete_object) {
  Thread::LockGuard map_lock(map_mutex_);
  std::weak_ptr<Singleton>& weak_singleton_ref = singleton_map_[type_name];
  SingletonSharedPtr singleton = weak_singleton_ref.lock();

  if (singleton == nullptr) {
    singleton = std::make_shared<Singleton>(make_object(), delete_object);
    weak_singleton_ref = singleton;
  }
  return singleton;
}

} // namespace Test
} // namespace Envoy
