#pragma once

#include <atomic>
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

  /**
   * Records a lookup of an object of type T. Only the last 'Capacity' lookups
   * are remembered.
   *
   * @param t the item being looked up.
   */
  void lookup(T t) {
    ++total_;
    if (queue_.size() >= Capacity) {
      queue_.pop_back();
    }
    queue_.push_front(ItemTime(t, time_source_.systemTime()));
  }

  using IterFn = std::function<void(T, SystemTime)>;

  /**
   * Calls fn(item, timestamp) for each of the remembered lookups.
   *
   * @param fn The function to call for every recently looked up item.
   */
  void forEach(IterFn fn) const {
    for (const ItemTime& item_time : queue_) {
      fn(item_time.first, item_time.second);
    }
  }

  /**
   * @return the time-source associated with the object.
   */
  TimeSource& timeSource() { return time_source_; }

  /**
   * @return the total number of lookups since tracking began.
   */
  uint64_t total() const { return total_; }

private:
  using ItemTime = std::pair<T, SystemTime>;
  std::deque<ItemTime> queue_;
  TimeSource& time_source_;
  uint64_t total_{0};
};

} // namespace Stats
} // namespace Envoy
