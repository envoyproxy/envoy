#include "common/stats/recent_lookups.h"

#include <functional>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
constexpr size_t Capacity = 10;
// constexpr uint64_t LogIntervalSec = 300;
} // namespace

void RecentLookups::lookup(absl::string_view str) {
  ++total_;
  if (queue_.size() >= Capacity) {
    queue_.pop_back();
  }
  queue_.push_front(std::string(str));
  /*
  SystemTime now = time_source_.systemTime();
  queue_.push_front(ItemTime(std::string(str), now));
  if (now <= last_log_time_) { // handle black-swan event for non-monotonic time.
    return;
  }
  std::chrono::seconds duration =
      std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time_);

  if (duration >= std::chrono::seconds(LogIntervalSec)) {
    std::vector<absl::string_view> message;
    forEach([&message, this](absl::string_view item, SystemTime time) {
      if (time > last_log_time_) {
        message.push_back(item);
      }
    });
    ENVOY_LOG_MISC(warn, "Recent lookups for {}", absl::StrJoin(message, ", "));
    last_log_time_ = now;
  }
  */
}

/**
 * Calls fn(item, timestamp) for each of the remembered lookups.
 *
 * @param fn The function to call for every recently looked up item.
 */
void RecentLookups::forEach(IterFn fn) const {
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
