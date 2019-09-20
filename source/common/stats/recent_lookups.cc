#include "common/stats/recent_lookups.h"

#include <functional>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
constexpr size_t Capacity = 10;
} // namespace

void RecentLookups::lookup(absl::string_view str) {
  ++total_;
  if (queue_.size() >= Capacity) {
    queue_.pop_back();
  }
  queue_.push_front(std::string(str));
}

/**
 * Calls fn(item, timestamp) for each of the remembered lookups.
 *
 * @param fn The function to call for every recently looked up item.
 */
void RecentLookups::forEach(const IterFn& fn) const {
  absl::flat_hash_map<absl::string_view, uint64_t> counts;
  for (const std::string& item : queue_) {
    ++counts[item];
  }
  for (auto iter : counts) {
    fn(iter.first, iter.second);
  }
}

} // namespace Stats
} // namespace Envoy
