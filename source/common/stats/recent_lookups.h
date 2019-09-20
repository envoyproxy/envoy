#pragma once

#include <deque>
#include <functional>
#include <utility>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// Remembers the last 'Capacity' items passed to lookup().
class RecentLookups {
public:
  /**
   * Records a lookup of a string. Only the last 'Capacity' lookups are remembered.
   *
   * @param str the item being looked up.
   */
  void lookup(absl::string_view str);

  using IterFn = std::function<void(absl::string_view, uint64_t)>;

  /**
   * Calls fn(item, count) for each of the remembered lookups.
   *
   * @param fn The function to call for every recently looked up item.
   */
  void forEach(const IterFn& fn) const;

  /**
   * @return the total number of lookups since tracking began.
   */
  uint64_t total() const { return total_; }

private:
  std::deque<std::string> queue_;
  uint64_t total_{0};
};

} // namespace Stats
} // namespace Envoy
