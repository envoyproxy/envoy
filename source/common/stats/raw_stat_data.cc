#include "common/stats/raw_stat_data.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace Envoy {
namespace Stats {

namespace {

// Round val up to the next multiple of the natural alignment.
// Note: this implementation only works because 8 is a power of 2.
uint64_t roundUpMultipleNaturalAlignment(uint64_t val) {
  const uint64_t multiple = alignof(RawStatData);
  static_assert(multiple == 1 || multiple == 2 || multiple == 4 || multiple == 8 || multiple == 16,
                "multiple must be a power of 2 for this algorithm to work");
  return (val + multiple - 1) & ~(multiple - 1);
}

} // namespace

// Normally the compiler would do this, but because name_ is a flexible-array-length
// element, the compiler can't. RawStatData is put into an array in HotRestartImpl, so
// it's important that each element starts on the required alignment for the type.
uint64_t RawStatData::structSize(uint64_t name_size) {
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + name_size + 1);
}

uint64_t RawStatData::structSizeWithOptions(const StatsOptions& stats_options) {
  return structSize(stats_options.maxNameLength());
}

void RawStatData::initialize(absl::string_view key, const StatsOptions& stats_options) {
  ASSERT(!initialized());
  ASSERT(key.size() <= stats_options.maxNameLength());
  ref_count_ = 1;
  memcpy(name_, key.data(), key.size());
  name_[key.size()] = '\0';
}

template class StatDataAllocatorImpl<RawStatData>;

} // namespace Stats
} // namespace Envoy
