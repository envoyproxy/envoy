#pragma once

#include <functional>
#include <list>
#include <utility>

#include "absl/container/flat_hash_map.h"
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

  /**
   * Clears out all contents.
   */
  void clear() {
    total_ = 0;
    map_.clear();
    list_.clear();
  }

  /**
   * Controls the maximum number of recent lookups to remember. If set to 0,
   * then only lookup counts is tracked.
   * @param capacity The number of lookups to remember.
   */
  void setCapacity(uint64_t capacity);

  /**
   * @return The configured capacity.
   */
  uint64_t capacity() const { return capacity_; }

private:
  void evictOne();

  struct ItemCount {
    std::string item_;
    uint64_t count_;
  };
  using List = std::list<ItemCount>;
  List list_;

  // TODO(jmarantz): we could make this more compact by making this a set of
  // list-iterators with heterogeneous hash/compare functors.
  using Map = absl::flat_hash_map<absl::string_view, List::iterator>;
  Map map_;
  uint64_t total_{0};
  uint64_t capacity_{0};
};

} // namespace Stats
} // namespace Envoy
