#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/grpc/buffered_async_client.h"

namespace Envoy {
namespace Grpc {

class BufferedMessageTtlManager {
public:
  BufferedMessageTtlManager(Event::Dispatcher& dispatcher, BufferedAsyncClientCallbacks& callbacks,
                            std::chrono::milliseconds message_ack_timeout)
      : dispatcher_(dispatcher), message_ack_timeout_(message_ack_timeout), callbacks_(callbacks),
        timer_(dispatcher_.createTimer([this] { checkMessages(); })) {}

  ~BufferedMessageTtlManager() { timer_->disableTimer(); }

  void setDeadline(absl::flat_hash_set<uint64_t>&& ids) {
    const auto expires_at = dispatcher_.timeSource().monotonicTime() + message_ack_timeout_;
    deadline_.emplace(expires_at, std::move(ids));

    if (!timer_->enabled()) {
      timer_->enableTimer(message_ack_timeout_);
    }
  }

private:
  void checkMessages() {
    const auto now = dispatcher_.timeSource().monotonicTime();

    auto begin_it = deadline_.lower_bound(now);

    for (auto it = begin_it; it != deadline_.end(); ++it) {
      for (auto&& id : it->second) {
        // If the retrieved message is a PendingFlush, it means that the message
        // has timed out. A timeout is treated as an error, and the callback will
        // re-buffer the message.
        callbacks_.onError(id);
      }
    }

    deadline_.erase(begin_it, deadline_.end());

    if (!deadline_.empty()) {
      const auto earliest_timepoint = deadline_.rbegin()->first;
      timer_->enableTimer(
          std::chrono::duration_cast<std::chrono::milliseconds>(earliest_timepoint - now));
    }
  }

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds message_ack_timeout_;
  BufferedAsyncClientCallbacks& callbacks_;
  Event::TimerPtr timer_;
  std::map<MonotonicTime, absl::flat_hash_set<uint64_t>, std::greater<>> deadline_;
};
} // namespace Grpc
} // namespace Envoy
