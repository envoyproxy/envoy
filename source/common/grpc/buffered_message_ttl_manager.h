#pragma once

#include <queue>

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Grpc {

using BufferedMessageExpirationCallback = std::function<void(uint64_t)>;

// This class is used to manage the lifetime of messages stored in BufferedAsyncClient. Messages
// whose survival period has expired will be deleted from the Buffer.
// We can set the ID you want to monitor TTL in the TTL Manager,
// which will set up a callback to check for expiration.
// You can set the ID even after the callback has been invoked.
// When the callback is invoked, the callback given to the constructor will be
// executed with the TTL-elapsed ID as an argument.
// After that, if the ID to be monitored is not empty, the callback for expiration check will be set
// again. The TTL Manager can be given a set of IDs that are expected to expire at the same time.
// When checking for ID expiration, an expiration callback will be called for each ID
// belonging to this set of IDs.
class BufferedMessageTtlManager {
public:
  BufferedMessageTtlManager(Event::Dispatcher& dispatcher,
                            BufferedMessageExpirationCallback&& expiry_callback,
                            std::chrono::milliseconds message_ack_timeout)
      : dispatcher_(dispatcher), message_ack_timeout_(message_ack_timeout),
        expiry_callback_(expiry_callback),
        timer_(dispatcher_.createTimer([this] { checkExpiredMessages(); })) {}

  ~BufferedMessageTtlManager() { timer_->disableTimer(); }

  void addDeadlineEntry(const absl::flat_hash_set<uint64_t>& ids) {
    const auto expires_at = dispatcher_.timeSource().monotonicTime() + message_ack_timeout_;
    deadline_.emplace(expires_at, std::move(ids));

    if (!timer_->enabled()) {
      timer_->enableTimer(message_ack_timeout_);
    }
  }

  const std::queue<std::pair<MonotonicTime, absl::flat_hash_set<uint64_t>>>& deadlineForTest() {
    return deadline_;
  }

private:
  void checkExpiredMessages() {
    const auto now = dispatcher_.timeSource().monotonicTime();

    while (!deadline_.empty()) {
      auto& it = deadline_.front();
      if (it.first > now) {
        break;
      }
      for (auto&& id : it.second) {
        expiry_callback_(id);
      }
      deadline_.pop();
    }

    if (!deadline_.empty()) {
      const auto earliest_timepoint = deadline_.front().first;
      timer_->enableTimer(
          std::chrono::duration_cast<std::chrono::milliseconds>(earliest_timepoint - now));
    }
  }

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds message_ack_timeout_;
  BufferedMessageExpirationCallback expiry_callback_;
  Event::TimerPtr timer_;
  std::queue<std::pair<MonotonicTime, absl::flat_hash_set<uint64_t>>> deadline_;
};
} // namespace Grpc
} // namespace Envoy
