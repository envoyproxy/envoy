#include "common/memory/utils.h"

#include "gperftools/malloc_extension.h"

namespace Envoy {
namespace Memory {

void Utils::ReleaseFreeMemory() {
#ifdef TCMALLOC
  MallocExtension::instance()->ReleaseFreeMemory();
#endif
}

} // namespace Memory
} // namespace Envoy
