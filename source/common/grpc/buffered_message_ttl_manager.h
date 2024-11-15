#pragma once

#include <queue>

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Grpc {

using BufferedMessageExpirationCallback = std::function<void(uint64_t)>;

// This class is used to manage the TTL for pending uploads within a BufferedAsyncClient. Multiple
// IDs can be inserted into the TTL manager at once, all with the same TTL (specified in the
// constructor). Upon expiry, the TTL manager will invoke the provided expiry callback for each ID.
// Note that there is no way to disable the expiration, and so it's up to the recipient of the
// callback to handle this. BufferedAsyncClient will do the right thing here: if the expired ID is
// still in flight it will be returned to the buffer, otherwise it does nothing. The TTL manager is
// designed to handle multiple sets of IDs inserted at various times, backing this with a single
// Timer. This allows us to track a large amount of IDs inserted at different times without using a
// lot of different timers, which could put undue pressure on the event loop.
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
