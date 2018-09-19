#include "common/stats/null_stat_data.h"

namespace Envoy {
namespace Stats {

NullStatData* NullStatDataAllocator::alloc(absl::string_view name) {
  UNREFERENCED_PARAMETER(name);
  return null_stat_ptr_.get();
}

void NullStatDataAllocator::free(NullStatData& data) { UNREFERENCED_PARAMETER(data); }

template class StatDataAllocatorImpl<NullStatData>;

} // namespace Stats
} // namespace Envoy
