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
  using StatAndName = std::pair<StatType*, std::string>;

  struct CompareNames {
#if 0
    CompareNames(const SymbolTable& /*symbol_table*/, uint32_t& num_compares)
        : //symbol_table_(symbol_table),
          num_compares_(num_compares) {}
#endif
    //bool operator()(const StatType* a, const StatType* b) const {
    bool operator()(const StatAndName& a, const StatAndName& b) const {
      //++num_compares_;
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
      //return symbol_table_.lessThan(a->statName(), b->statName());
      return a.second < b.second;
#endif
    }

#if USE_PRIO_QUEUE
    //void flushStatFromCache(StatType* stat) { name_cache_.erase(stat); }
#endif

    //const SymbolTable& symbol_table_;
#if USE_PRIO_QUEUE
    //mutable absl::flat_hash_map<const StatType*, std::string> name_cache_;
#endif
    //uint32_t& num_compares_;
  };

#if USE_PRIO_QUEUE
  using OrderedSet = std::priority_queue<StatAndName, std::vector<StatAndName>, CompareNames>;
#else
  //using OrderedSet = absl::btree_set<StatType*, CompareNames>;
  using OrderedSet = absl::btree_set<StatAndName, CompareNames>;
  using OrderedSetIter = typename OrderedSet::iterator;
#endif

  void setUsedOnly(bool used_only) { used_only_ = used_only; }

  SharedStatVector getFilteredStatsAfter(uint32_t page_size, StatName after) const {
    //uint32_t num_compares = 0;
    CompareNames compare_names; //(symbol_table_, num_compares);
    OrderedSet top(compare_names);
    size_t stats_remaining;
    SharedStatVector ret;
#if !USE_PRIO_QUEUE
    OrderedSetIter last;
#endif

    forEach([&stats_remaining](size_t stats) { stats_remaining = stats; },
            [&top, this, after, page_size, &stats_remaining, &ret
#if !USE_PRIO_QUEUE
             , &last
#endif
             ](
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
#if 1
          if (top.size() < page_size) {
            top.push(StatAndName(&stat, stat.name()));
          } else {
            std::string name = stat.name();
            if (name < top.top().second) {
              top.pop();
              top.emplace(StatAndName(&stat, std::move(name)));
            }
          }
#else
          top.push(&stat);
          if (top.size() > page_size) {
            //StatType* removed = top.top();
            top.pop();
            //compare_names.flushStatFromCache(removed);
          }
#endif
#else
#if 0
          top.emplace(StatAndName(&stat, stat.name()));
          if (top.size() > page_size) {
            OrderedSetIter last = top.end();
            --last;
          }
#else
          if (top.size() < page_size) {
            top.emplace(StatAndName(&stat, stat.name()));
            if (top.size() == page_size) {
              last = top.end();
              --last;
            }
          } else {
            std::string name = stat.name();
            if (name < last->second) {
              top.erase(last);
              top.emplace(StatAndName(&stat, std::move(name)));
              last = top.end();
              --last;
            }
          }
#endif
#endif
        }

        if (--stats_remaining == 0) {
#if USE_PRIO_QUEUE
          ret.resize(top.size());
          for (int32_t index = top.size() - 1; index >= 0; --index) {
            ASSERT(!top.empty());
            ret[index] = top.top().first;
            top.pop();
          }
#else
          ret.reserve(top.size());
          for (StatAndName& stat_and_name : top) {
            ret.push_back(SharedStat(stat_and_name.first));
          }
#endif
        }
    });
    //ENVOY_LOG_MISC(error, "Num Compares={}", num_compares);
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
