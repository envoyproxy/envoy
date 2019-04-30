#include "common/stats/heap_stat_data.h"

#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

HeapStatDataAllocator::~HeapStatDataAllocator() { ASSERT(stats_.empty()); }

HeapStatData* HeapStatData::alloc(StatName stat_name, SymbolTable& symbol_table) {
  void* memory = ::malloc(sizeof(HeapStatData) + stat_name.size());
  ASSERT(memory);
  symbol_table.incRefCount(stat_name);
  return new (memory) HeapStatData(stat_name);
}

void HeapStatData::free(SymbolTable& symbol_table) {
  symbol_table.free(statName());
  this->~HeapStatData();
  ::free(this); // matches malloc() call above.
}

HeapStatData& HeapStatDataAllocator::alloc(StatName name) {
  using HeapStatDataFreeFn = std::function<void(HeapStatData * d)>;
  std::unique_ptr<HeapStatData, HeapStatDataFreeFn> data_ptr(
      HeapStatData::alloc(name, symbolTable()),
      [this](HeapStatData* d) { d->free(symbolTable()); });
  Thread::ReleasableLockGuard lock(mutex_);
  auto ret = stats_.insert(data_ptr.get());
  HeapStatData* existing_data = *ret.first;
  lock.release();

  if (ret.second) {
    return *data_ptr.release();
  }
  ++existing_data->ref_count_;
  return *existing_data;
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

  data.free(symbolTable());
}

#ifndef ENVOY_CONFIG_COVERAGE
void HeapStatDataAllocator::debugPrint() {
  Thread::LockGuard lock(mutex_);
  for (HeapStatData* heap_stat_data : stats_) {
    ENVOY_LOG_MISC(info, "{}", symbolTable().toString(heap_stat_data->statName()));
  }
}
#endif

template class StatDataAllocatorImpl<HeapStatData>;

} // namespace Stats
} // namespace Envoy
