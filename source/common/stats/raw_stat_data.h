#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats_options.h"

#include "common/common/assert.h"
#include "common/common/block_memory_hash_set.h"
#include "common/common/hash.h"
#include "common/common/thread.h"
#include "common/stats/stat_data_allocator_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 *
 * @note Due to name_ being variable size, sizeof(RawStatData) probably isn't useful. Use
 * RawStatData::structSize() or RawStatData::structSizeWithOptions() instead.
 */
struct RawStatData {

  /**
   * Due to the flexible-array-length of name_, c-style allocation
   * and initialization are necessary.
   */
  RawStatData() = delete;
  ~RawStatData() = delete;

  /**
   * @return uint64_t the size of this struct, accounting for the length of
   *     name_ and padding for alignment.
   */
  static uint64_t structSize(uint64_t name_size);

  /**
   * Wrapper for structSize, taking a StatsOptions struct. Required by
   * BlockMemoryHashSet, which has the context to supply StatsOptions.
   */
  static uint64_t structSizeWithOptions(const StatsOptions& stats_options);

  /**
   * Initializes this object to have the specified key, a refcount of 1, and all
   * other values zero. Required for the HeapRawStatDataAllocator, which does
   * not expect stat name truncation. We pass in the number of bytes allocated
   * in order to assert the copy is safe inline.
   *
   * @param key the key
   * @param stats_options the stats options
   */
  void initialize(absl::string_view key, const StatsOptions& stats_options);

  /**
   * @return uint64_t a hash of the key. This is required by BlockMemoryHashSet.
   */
  static uint64_t hash(absl::string_view key) { return HashUtil::xxHash64(key); }

  /**
   * @return true if object is in use.
   */
  bool initialized() { return name_[0] != '\0'; }

  /**
   * @return absl::string_view the name as a string_view.
   */
  absl::string_view key() const { return absl::string_view(name_); }

  /**
   * @return const char* the name.
   */
  const char* name() const { return name_; }

  std::atomic<uint64_t> value_;
  std::atomic<uint64_t> pending_increment_;
  std::atomic<uint16_t> flags_;
  std::atomic<uint16_t> ref_count_;
  std::atomic<uint32_t> unused_;
  char name_[];
};

using RawStatDataSet = BlockMemoryHashSet<Stats::RawStatData>;

template <class Stat> class RawStat : public Stat {
public:
  RawStat(StatName stat_name, RawStatData& data, StatDataAllocatorImpl<RawStatData>& alloc,
          absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : Stat(data, alloc, tag_extracted_name, tags),
        stat_name_storage_(stat_name, alloc.symbolTable()) {}
  ~RawStat() { stat_name_storage_.free(this->symbolTable()); }

  StatName statName() const override { return stat_name_storage_.statName(); }

private:
  StatNameStorage stat_name_storage_;
};

class RawStatDataAllocator : public StatDataAllocatorImpl<RawStatData> {
public:
  RawStatDataAllocator(Thread::BasicLockable& mutex, RawStatDataSet& stats_set,
                       const StatsOptions& options, SymbolTable& symbol_table)
      : StatDataAllocatorImpl(symbol_table), mutex_(mutex), stats_set_(stats_set),
        options_(options) {}
  ~RawStatDataAllocator();

  virtual RawStatData* alloc(absl::string_view name); // Virtual only for mocking.
  void free(Stats::RawStatData& data) override;
  RawStatData* allocStatName(StatName stat_name) {
    return alloc(symbolTable().toString(stat_name));
  }

  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return true; }

  template <class Stat>
  std::shared_ptr<Stat> makeStat(StatName name, absl::string_view tag_extracted_name,
                                 const std::vector<Tag>& tags) {
    RawStatData* raw_stat_data = allocStatName(name);
    if (raw_stat_data == nullptr) {
      return nullptr;
    }
    return std::make_shared<RawStat<Stat>>(name, *raw_stat_data, *this, tag_extracted_name, tags);
  }

  CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                               const std::vector<Tag>& tags) override {
    return makeStat<CounterImpl<RawStatData>>(name, tag_extracted_name, tags);
  }

  GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                           const std::vector<Tag>& tags) override {
    return makeStat<GaugeImpl<RawStatData>>(name, tag_extracted_name, tags);
  }

  BoolIndicatorSharedPtr makeBoolIndicator(StatName name, absl::string_view tag_extracted_name,
                                           const std::vector<Tag>& tags) override {
    return makeStat<BoolIndicatorImpl<RawStatData>>(name, tag_extracted_name, tags);
  }

private:
  Thread::BasicLockable& mutex_;
  RawStatDataSet& stats_set_ GUARDED_BY(mutex_);
  const StatsOptions& options_;
};

} // namespace Stats
} // namespace Envoy
