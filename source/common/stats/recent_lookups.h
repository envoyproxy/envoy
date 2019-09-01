#pragma once

#include <deque>
#include <functional>
#include <utility>

#include "envoy/common/time.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

// Remembers the last 'Capacity' items passed to lookup().
template <class T, size_t Capacity = 10> class RecentLookups {
public:
  explicit RecentLookups(TimeSource& time_source) : time_source_(time_source) {}

  // Records a lookup of an object of type T. Only the last 'Capacity' lookups
  // are remembered.
  void lookup(T t) {
    if (queue_.size() >= Capacity) {
      if (free_fn_) {
        free_fn_(queue_.back().first);
      }
      queue_.pop_back();
    }
    queue_.push_front(ItemTime(t, time_source_.systemTime()));
  }

  using FreeFn = std::function<void(T)>;
  using IterFn = std::function<void(T, SystemTime, size_t)>;

  // Calls fn(item, timestmp, count) for each of the remembered
  // lookups. Duplicates are collated and provided with their timestamp and
  // count.
  void forEach(IterFn fn) {
    using TimeCount = std::pair<SystemTime, size_t>;
    absl::flat_hash_map<T, TimeCount> entry_map;
    for (ItemTime& item_time : queue_) {
      TimeCount& time_count = entry_map[item_time.first];
      if ((time_count.second == 0) || (item_time.second > time_count.first)) {
        time_count.first = item_time.second;
      }
      ++time_count.second;
    }
    for (auto& item_time_count : entry_map) {
      TimeCount& time_count = item_time_count.second;
      fn(item_time_count.first, time_count.first, time_count.second);
    }
  }

  void clear() { queue_.clear(); }
  void setFreeFn(const FreeFn& free_fn) { free_fn_ = free_fn; }
  TimeSource& timeSource() { return time_source_; }

private:
  using ItemTime = std::pair<T, SystemTime>;
  std::deque<ItemTime> queue_;
  TimeSource& time_source_;
  FreeFn free_fn_;
};

} // namespace Stats
} // namespace Envoy
