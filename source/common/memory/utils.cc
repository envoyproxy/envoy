#include "common/memory/utils.h"

#ifdef TCMALLOC
#include "gperftools/malloc_extension.h"
#endif

namespace Envoy {
namespace Memory {

void Utils::releaseFreeMemory() {
#ifdef TCMALLOC
  MallocExtension::instance()->ReleaseFreeMemory();
#endif
}

} // namespace Memory
} // namespace Envoy
