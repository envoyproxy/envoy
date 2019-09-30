#include "common/stats/recent_lookups.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

void RecentLookups::lookup(absl::string_view str) {
  ++total_;
  if (capacity_ == 0) {
    return;
  }
  auto map_iter = map_.find(str);
  if (map_iter != map_.end()) {
    // The item is already in the list. Bump its reference-count and move it to
    // the front of the list.
    auto list_iter = map_iter->second;
    ++list_iter->count_;
    if (list_iter != list_.begin()) {
      list_.splice(list_.begin(), list_, list_iter);
    }
  } else {
    ASSERT(list_.size() <= capacity_);
    // Evict oldest item if needed.
    if (list_.size() >= capacity_) {
      evictOne();
    }

    // The string storage is in the list entry.
    list_.push_front(ItemCount{std::string(str), 1});
    auto list_iter = list_.begin();
    map_[list_iter->item_] = list_iter;
  }
  ASSERT(list_.size() == map_.size());
}

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
  ASSERT(!list_.empty());
  ASSERT(!map_.empty());
  const ItemCount& item_count = list_.back();
  int erased = map_.erase(item_count.item_);
  ASSERT(erased == 1);
  list_.pop_back();
}

} // namespace Stats
} // namespace Envoy
