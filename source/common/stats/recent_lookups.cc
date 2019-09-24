#include "common/stats/recent_lookups.h"

#include <functional>
#include <utility>

#include "common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

void RecentLookups::lookup(absl::string_view str) {
  ++total_;
  if (capacity_ == 0) {
    return;
  }
  Map::iterator map_iter = map_.find(str);
  if (map_iter != map_.end()) {
    // The item is already in the list, but we need to bump its count and move
    // it to the front, so we must re-order the list, which will invalidate the
    // iterators to i.
    List::iterator list_iter = map_iter->second;
    ItemCount item_count = std::move(*list_iter);
    list_.erase(list_iter);
    ++item_count.count_;
    list_.push_front(std::move(item_count));
    map_iter->second = list_.begin();
  } else {
    ASSERT(list_.size() <= capacity_);
    // Evict oldest item if needed.
    if (list_.size() >= capacity_) {
      evictOne();
    }

    // The string storage is in the list entry.
    list_.push_front(ItemCount{std::string(str), 1});
    List::iterator list_iter = list_.begin();
    map_[list_iter->item_] = list_iter;
  }
  ASSERT(list_.size() == map_.size());
}

/**
 * Calls fn(item, timestamp) for each of the remembered lookups.
 *
 * @param fn The function to call for every recently looked up item.
 */
void RecentLookups::forEach(const IterFn& fn) const {
  for (const ItemCount& item_count : list_) {
    fn(item_count.item_, item_count.count_);
  }
}

void RecentLookups::setCapacity(uint64_t capacity) {
  capacity_ = capacity;
  while (capacity_ < list_.size()) {
    evictOne();
  }
}

void RecentLookups::evictOne() {
  const ItemCount& item_count = list_.back();
  int erased = map_.erase(item_count.item_);
  ASSERT(erased == 1);
  list_.pop_back();
}

} // namespace Stats
} // namespace Envoy
