#include "common/memory/utils.h"

#ifdef TCMALLOC

#include "gperftools/malloc_extension.h"

namespace Envoy {
namespace Memory {

void Utils::ReleaseFreeMemory() { MallocExtension::instance()->ReleaseFreeMemory(); }

} // namespace Memory
} // namespace Envoy

#else

namespace Envoy {
namespace Memory {

void Utils::ReleaseFreeMemory() {}

} // namespace Memory
} // namespace Envoy

#endif // #ifdef TCMALLOC
