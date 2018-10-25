#include "common/stats/heap_stat_data.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

HeapStatData::HeapStatData(absl::string_view key) {
  StringUtil::strlcpy(name_, key.data(), key.size() + 1);
}

HeapStatDataAllocator::HeapStatDataAllocator() {}

HeapStatDataAllocator::~HeapStatDataAllocator() { ASSERT(stats_.empty()); }

HeapStatData* HeapStatDataAllocator::alloc(absl::string_view name) noexcept {
  // Any expected truncation of name is done at the callsite. No truncation is
  // required to use this allocator. Note that data must be freed by calling
  // its free() method, and not by destructing or unique_ptr. So this method
  // cannot call anything that might throw, and thus it cannot through itself.
  HeapStatData* data = HeapStatData::alloc(name);
  Thread::ReleasableLockGuard lock(mutex_);
  auto ret = stats_.insert(data);
  HeapStatData* existing_data = *ret.first;
  lock.release();

  if (ret.second) {
    return data;
  }
  ++existing_data->ref_count_;
  data->free();
  return existing_data;
}

void HeapStatDataAllocator::free(HeapStatData& data) {
  ASSERT(data.ref_count_ > 0);
  if (--data.ref_count_ > 0) {
    return;
  }

  {
    Thread::LockGuard lock(mutex_);
    size_t key_removed = stats_.erase(&data);
    ASSERT(key_removed == 1);
  }

  data.free();
}

HeapStatData* HeapStatData::alloc(absl::string_view name) {
  void* memory = ::malloc(sizeof(HeapStatData) + name.size() + 1);
  ASSERT(memory);
  return new (memory) HeapStatData(name);
}

void HeapStatData::free() {
  this->~HeapStatData();
  ::free(this); // matches malloc() call above.
}

template class StatDataAllocatorImpl<HeapStatData>;

} // namespace Stats
} // namespace Envoy
