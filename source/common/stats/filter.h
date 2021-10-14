#pragma once

#define USE_PRIO_QUEUE 0
#if USE_PRIO_QUEUE
#include <queue>
#endif

#include "envoy/stats/store.h"

#include "source/common/stats/symbol_table_impl.h"

#if !USE_PRIO_QUEUE
#include "absl/container/btree_set.h"
#endif

namespace Envoy {
namespace Stats {

template <class StatType>
class StatsFilter {
public:
  explicit StatsFilter(const Store& store)
      : store_(store),
        symbol_table_(store.constSymbolTable()) {}

  using FilterFn = std::function<bool(StatType&)>;
  using SharedStat = RefcountPtr<StatType>;
  using SharedStatVector = std::vector<SharedStat>;

  struct CompareNames {
    CompareNames(const SymbolTable& symbol_table, uint32_t& num_compares)
        : symbol_table_(symbol_table),
          num_compares_(num_compares) {}
    bool operator()(const StatType* a, const StatType* b) const {
      ++num_compares_;
#if 0
      std::string& a_str = name_cache_[a];
      std::string& b_str = name_cache_[b];
      if (a_str.empty()) {
        a_str = a->name();
      }
      if (b_str.empty()) {
        b_str = b->name();
      }
      ASSERT((a_str < b_str) == symbol_table_.lessThan(a->statName(), b->statName()),
             absl::StrCat("a=", a_str, ", b=", b_str));
      return a_str < b_str;
#else
      return symbol_table_.lessThan(a->statName(), b->statName());
#endif
    }

#if USE_PRIO_QUEUE
    void flushStatFromCache(StatType* stat) { name_cache_.erase(stat); }
#endif

    const SymbolTable& symbol_table_;
#if USE_PRIO_QUEUE
    mutable absl::flat_hash_map<const StatType*, std::string> name_cache_;
#endif
    uint32_t& num_compares_;
  };

#if USE_PRIO_QUEUE
  using OrderedSet = std::priority_queue<StatType*, std::vector<StatType*>, CompareNames>;
#else
  using OrderedSet = absl::btree_set<StatType*, CompareNames>;
  using OrderedSetIter = typename OrderedSet::iterator;
#endif

  void setUsedOnly(bool used_only) { used_only_ = used_only; }

  SharedStatVector getFilteredStatsAfter(uint32_t page_size, StatName after) const {
    uint32_t num_compares = 0;
    CompareNames compare_names(symbol_table_, num_compares);
    OrderedSet top(compare_names);
    size_t stats_remaining;
    SharedStatVector ret;

    forEach([&stats_remaining](size_t stats) { stats_remaining = stats; },
            [&top, this, after, page_size, &stats_remaining, &ret](
                StatType& stat) {
        // If we are filtering out non-empties, and this is empty,
        // then skip this one.
        if ((!used_only_ || stat.used()) &&

            // Filter out stats that are alphabetically earlier than 'after'.
            (after.empty() || symbol_table_.lessThan(after, stat.statName()))) {

          // Check whether 'stat' should be admitted into the 'top' set. This
          // algorithm runs in NumStats*log(page_size + 1) time.
          //
          // We do the cheap used() filtering and the comparison with the last
          // element before the set insertion work, to try to reduce number of
          // log(NumStats) set mutations.
#if USE_PRIO_QUEUE
          top.push(&stat);
          if (top.size() > page_size) {
            //StatType* removed = top.top();
            top.pop();
            //compare_names.flushStatFromCache(removed);
          }
#else
          bool do_insert = top.size() < page_size;
          if (!do_insert) {
            OrderedSetIter last = top.end();
            --last;
            if (symbol_table_.lessThan(stat.statName(), (*last)->statName())) {
              top.erase(last);
              do_insert = true;
            }
          }

          if (do_insert) {
            top.insert(&stat);
          }
#endif
        }

        if (--stats_remaining == 0) {
#if USE_PRIO_QUEUE
          ret.resize(top.size());
          for (int32_t index = top.size() - 1; index >= 0; --index) {
            ASSERT(!top.empty());
            ret[index] = top.top();
            top.pop();
          }
#else
          ret.reserve(top.size());
          for (StatType* stat : top) {
            ret.push_back(SharedStat(stat));
          }
#endif
        }
    });
    ENVOY_LOG_MISC(error, "Num Compares={}", num_compares);
    return ret;
  }

  void forEach(std::function<void(size_t)> size_fn, std::function<void(Counter&)> fn) const {
    store_.forEachCounter(size_fn, fn);
  }
  void forEach(std::function<void(size_t)> size_fn, std::function<void(Gauge&)> f) const {
    store_.forEachGauge(size_fn, f);
  }
  void forEach(std::function<void(size_t)> size_fn, std::function<void(TextReadout&)> f) const {
    store_.forEachTextReadout(size_fn, f);
  }

private:
  const Store& store_;
  const SymbolTable& symbol_table_;
  bool used_only_{false};
  //RE2::RE2 regex_;
};

} // namespace Envoy
} // namespace Stats
