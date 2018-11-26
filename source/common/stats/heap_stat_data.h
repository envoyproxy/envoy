#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "common/common/hash.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/stats/stat_data_allocator_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
 */
struct HeapStatData {
  /**
   * @returns absl::string_view the name as a string_view.
   */
  absl::string_view key() const { return name_; }

  /**
   * @returns std::string the name as a const char*.
   */
  const char* name() const { return name_; }

  static HeapStatData* alloc(absl::string_view name);
  void free();

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  char name_[];

private:
  /**
   * You cannot construct/destruct a HeapStatData directly with new/delete as
   * it's variable-size. Use alloc()/free() methods above.
   */
  explicit HeapStatData(absl::string_view name);
  ~HeapStatData() {}
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
  struct HeapStatHash {
    size_t operator()(const HeapStatData* a) const { return HashUtil::xxHash64(a->key()); }
  };
  struct HeapStatCompare {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const {
      return (a->key() == b->key());
    }
  };

  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/3927 and
  //  https://github.com/envoyproxy/envoy/issues/3585, which can help reorganize
  // the heap stats using a ref-counted symbol table to compress the stat strings.
  using StatSet = absl::flat_hash_set<HeapStatData*, HeapStatHash, HeapStatCompare>;

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
