#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/stats/stat_data_allocator_impl.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
 */
struct HeapStatData {
  explicit HeapStatData(StatNamePtr name_ptr);

  /**
   * @returns std::string the name as a std::string with no truncation.
   */
  std::string name() const { return name_ptr_->toString(); }

  bool operator==(const HeapStatData& rhs) const { return *name_ptr_ == *rhs.name_ptr_; }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  StatNamePtr name_ptr_;
};

/**
 * Implementation of StatDataAllocator using a pure heap-based strategy, so that
 * Envoy implementations that do not require hot-restart can use less memory.
 */
class HeapStatDataAllocator : public StatDataAllocatorImpl<HeapStatData> {
public:
  HeapStatDataAllocator();
  ~HeapStatDataAllocator();

  // StatDataAllocatorImpl
  HeapStatData* alloc(absl::string_view name) override;
  void free(HeapStatData& data) override;

  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return false; }

  // SymbolTableImpl
  StatNamePtr encode(absl::string_view sv) {
    Thread::LockGuard lock(mutex_);
    return table_.encode(sv);
  }

private:
  struct HeapStatHash {
    size_t operator()(const HeapStatData* a) const { return a->name_ptr_->hash(); }
  };
  struct HeapStatCompare {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const { return (*a == *b); }
  };

  typedef std::unordered_set<HeapStatData*, HeapStatHash, HeapStatCompare> StatSet;

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  StatSet stats_ GUARDED_BY(mutex_);
  // A locally held symbol table which encodes stat names as StatNamePtrs and decodes StatNamePtrs
  // back into strings.
  SymbolTableImpl table_ GUARDED_BY(mutex_);
  // A mutex is needed here to protect both the stats_ object and the table_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
