#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "common/common/hash.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/stats/stat_data_allocator_impl.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
 */
struct HeapStatData {
  explicit HeapStatData(absl::string_view key);

  /**
   * @returns absl::string_view the name as a string_view.
   */
  absl::string_view key() const { return name_; }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  std::string name_;
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

private:
  struct HeapStatHash_ {
    size_t operator()(const HeapStatData* a) const { return HashUtil::xxHash64(a->key()); }
  };
  struct HeapStatCompare_ {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const {
      return (a->key() == b->key());
    }
  };

  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/3927 and
  //  https://github.com/envoyproxy/envoy/issues/3585, which can help reorganize
  // the heap stats using a ref-counted symbol table to compress the stat strings.
  typedef std::unordered_set<HeapStatData*, HeapStatHash_, HeapStatCompare_> StatSet;

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher.
  StatSet stats_ GUARDED_BY(mutex_);
  // A mutex is needed here to protect the stats_ object from both alloc() and free() operations.
  // Although alloc() operations are called under existing locking, free() operations are made from
  // the destructors of the individual stat objects, which are not protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
