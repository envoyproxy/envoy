#pragma once

#include <deque>
#include <functional>
#include <utility>

#include "envoy/common/time.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// Remembers the last 'Capacity' items passed to lookup().
class RecentLookups {
public:
  explicit RecentLookups(TimeSource& time_source) : time_source_(time_source) {}

  /**
   * Records a lookup of a string. Only the last 'Capacity' lookups are remembered.
   *
   * @param str the item being looked up.
   */
  void lookup(absl::string_view str);

  using IterFn = std::function<void(absl::string_view, SystemTime)>;

  /**
   * Calls fn(item, timestamp) for each of the remembered lookups.
   *
   * @param fn The function to call for every recently looked up item.
   */
  void forEach(IterFn fn) const;

  /**
   * @return the time-source associated with the object.
   */
  TimeSource& timeSource() { return time_source_; }

  /**
   * @return the total number of lookups since tracking began.
   */
  uint64_t total() const { return total_; }

private:
  using ItemTime = std::pair<std::string, SystemTime>;
  std::deque<ItemTime> queue_;
  TimeSource& time_source_;
  uint64_t total_{0};
  SystemTime last_log_time_;
};

} // namespace Stats
} // namespace Envoy
