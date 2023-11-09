#include "source/server/instance_impl.h"

namespace Envoy {
namespace Server {
void InstanceImpl::maybeCreateHeapShrinker() {
  heap_shrinker_ =
      std::make_unique<Memory::HeapShrinker>(dispatcher(), overloadManager(), *stats().rootScope());
}

} // namespace Server
} // namespace Envoy
