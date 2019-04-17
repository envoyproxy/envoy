#include "common/stats/raw_stat_data.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "common/common/lock_guard.h"

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

const uint64_t MaxNameLength = 255;

} // namespace

// Normally the compiler would do this, but because name_ is a flexible-array-length
// element, the compiler can't. RawStatData is put into an array in HotRestartImpl, so
// it's important that each element starts on the required alignment for the type.
uint64_t RawStatData::structSize(uint64_t name_size) {
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + name_size + 1);
}

uint64_t RawStatData::structSizeWithOptions() { return structSize(MaxNameLength); }

void RawStatData::initialize(absl::string_view key) {
  ASSERT(!initialized());
  ASSERT(key.size() <= MaxNameLength);
  ref_count_ = 1;
  memcpy(name_, key.data(), key.size());
  name_[key.size()] = '\0';
}

Stats::RawStatData* RawStatDataAllocator::alloc(absl::string_view name) {
  // Try to find the existing slot in shared memory, otherwise allocate a new one.
  Thread::LockGuard lock(mutex_);
  if (name.length() > MaxNameLength) {
    ENVOY_LOG_MISC(
        warn,
        "Statistic '{}' is too long with {} characters, it will be truncated to {} characters",
        name, name.size(), MaxNameLength);
    name = name.substr(0, MaxNameLength);
  }
  auto value_created = stats_set_.insert(name);
  Stats::RawStatData* data = value_created.first;
  if (data == nullptr) {
    return nullptr;
  }
  // For new entries (value-created.second==true), BlockMemoryHashSet calls Value::initialize()
  // automatically, but on recycled entries (value-created.second==false) we need to bump the
  // ref-count.
  if (!value_created.second) {
    ++data->ref_count_;
  }
  return data;
}

void RawStatDataAllocator::free(Stats::RawStatData& data) {
  // We must hold the lock since the reference decrement can race with an initialize above.
  Thread::LockGuard lock(mutex_);
  ASSERT(data.ref_count_ > 0);
  --data.ref_count_;
  if (data.ref_count_ > 0) {
    return;
  }
  bool key_removed = stats_set_.remove(data.key());
  ASSERT(key_removed);
  memset(static_cast<void*>(&data), 0, Stats::RawStatData::structSizeWithOptions());
}

template class StatDataAllocatorImpl<RawStatData>;

} // namespace Stats
} // namespace Envoy
