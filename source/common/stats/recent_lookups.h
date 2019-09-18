#pragma once

#include <functional>
#include <map>
#include <utility>

//#include "envoy/common/time.h"

//#include "common/common/hash.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// Remembers the last 'Capacity' items passed to lookup().
class RecentLookups {
public:
  // explicit RecentLookups(TimeSource& time_source) : time_source_(time_source) {}

  /**
   * Records a lookup of a string. Only the last 'Capacity' lookups are remembered.
   *
   * @param str the item being looked up.
   */
  void lookup(absl::string_view str);

  using IterFn = std::function<void(absl::string_view, uint64_t)>;

  /**
   * Calls fn(item, timestamp) for each of the remembered lookups.
   *
   * @param fn The function to call for every recently looked up item.
   */
  void forEach(IterFn fn) const;

  /**
   * @return the time-source associated with the object.
   */
  // TimeSource& timeSource() { return time_source_; }

  /**
   * @return the total number of lookups since tracking began.
   */
  uint64_t total() const { return total_; }

private:
  struct ItemCount {
    std::string item_;
    int64_t count_;

    /*bool operator<(const ItemCount& that) const {
      if (count_ == that.count_) {
        return item_ < that.item_;
      }
      return count_ < that.count_;
    }

    template <typename H> friend H AbslHashValue(H h, const ItemCount& item_count) {
      return H::combine(std::move(h), item_count.item_);
      }*/
  };

  using List = std::list<ItemCount>;
  List list_;
  using Map = absl::flat_hash_map<absl::string_view, List::iterator>;
  Map map_;
  uint64_t total_{0};
};

} // namespace Stats
} // namespace Envoy
