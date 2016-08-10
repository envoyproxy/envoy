#include "stats.h"

#ifdef TCMALLOC

#include "gperftools/malloc_extension.h"

namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() {
  uint64_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
  return value;
}

uint64_t Stats::totalCurrentlyReserved() {
  uint64_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.heap_size", &value);
  return value;
}

} // Memory

#else

namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() { return 0; }
uint64_t Stats::totalCurrentlyReserved() { return 0; }

} // Memory

#endif // #ifdef TCMALLOC
