#include "common/stats/recent_lookups.h"

#include <functional>
#include <utility>

#include "envoy/common/time.h"

#include "common/common/logger.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
constexpr size_t Capacity = 10;
constexpr uint64_t LogIntervalSec = 300;
} // namespace

void RecentLookups::lookup(absl::string_view str) {
  ++total_;
  if (queue_.size() >= Capacity) {
    queue_.pop_back();
  }
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
}

/**
 * Calls fn(item, timestamp) for each of the remembered lookups.
 *
 * @param fn The function to call for every recently looked up item.
 */
void RecentLookups::forEach(IterFn fn) const {
  for (const ItemTime& item_time : queue_) {
    fn(item_time.first, item_time.second);
  }
}

} // namespace Stats
} // namespace Envoy
