#pragma once

#include <deque>
#include <functional>
#include <utility>

#include "envoy/common/time.h"

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
  using IterFn = std::function<void(T, SystemTime)>;

  // Calls fn(item, timestamp) for each of the remembered lookups.
  void forEach(IterFn fn) {
    for (ItemTime& item_time : queue_) {
      fn(item_time.first, item_time.second);
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
