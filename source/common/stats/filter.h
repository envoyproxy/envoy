#pragma once

#include "envoy/stats/store.h"

#include "source/common/stats/symbol_table_impl.h"

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
    CompareNames(const SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
    bool operator()(const StatType* a, const StatType* b) const {
      return symbol_table_.lessThan(a->statName(), b->statName());
    }

    const SymbolTable& symbol_table_;
  };

  using OrderedSet = std::set<StatType*, CompareNames>;
  using OrderedSetIter = typename OrderedSet::iterator;

  void setUsedOnly(bool used_only) { used_only_ = used_only; }

  SharedStatVector getFilteredStatsAfter(uint32_t page_size, StatName after) const {
    CompareNames compare_names(symbol_table_);
    OrderedSet top(compare_names);
    size_t stats_remaining;
    SharedStatVector ret;

    forEach([&stats_remaining](size_t stats) { stats_remaining = stats; },
            [&top, this, after, page_size, &stats_remaining, &ret](StatType& stat) {
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
        }

        if (--stats_remaining == 0) {
          ret.reserve(top.size());
          for (StatType* stat : top) {
            ret.push_back(SharedStat(stat));
          }
        }
    });
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
