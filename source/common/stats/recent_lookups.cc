#include "common/stats/recent_lookups.h"

#include <functional>
#include <utility>

//#include "envoy/common/time.h"

#include "common/common/assert.h"
//#include "common/common/logger.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
constexpr size_t Capacity = 10;
// constexpr uint64_t LogIntervalSec = 300;
} // namespace

void RecentLookups::lookup(absl::string_view str) {
  ++total_;

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
    ASSERT(list_.size() <= Capacity);
    // Evict oldest item if needed.
    if (list_.size() >= Capacity) {
      ItemCount item_count = std::move(list_.back());
      list_.pop_back();
      map_.erase(item_count.item_);
    }

    // The string storage is in the list entry.
    list_.push_front(ItemCount{std::string(str), 1});
    List::iterator list_iter = list_.begin();
    map_[list_iter->item_] = list_iter;
  }
}

/**
 * Calls fn(item, timestamp) for each of the remembered lookups.
 *
 * @param fn The function to call for every recently looked up item.
 */
void RecentLookups::forEach(IterFn fn) const {
  for (const ItemCount& item_count : list_) {
    fn(item_count.item_, item_count.count_);
  }
}

} // namespace Stats
} // namespace Envoy
