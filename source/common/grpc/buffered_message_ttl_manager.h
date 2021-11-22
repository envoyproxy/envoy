#pragma once

#include <queue>

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

  const std::queue<std::pair<MonotonicTime, absl::flat_hash_set<uint64_t>>>& deadline() {
    return deadline_;
  }

private:
  void checkMessages() {
    const auto now = dispatcher_.timeSource().monotonicTime();

    while (!deadline_.empty()) {
      auto& it = deadline_.front();
      if (it.first > now) {
        break;
      }
      for (auto&& id : it.second) {
        // If the retrieved message is a PendingFlush, it means that the message
        // has timed out. A timeout is treated as an error, and the callback will
        // re-buffer the message.
        callbacks_.onError(id);
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
  BufferedAsyncClientCallbacks& callbacks_;
  Event::TimerPtr timer_;
  std::queue<std::pair<MonotonicTime, absl::flat_hash_set<uint64_t>>> deadline_;
};
} // namespace Grpc
} // namespace Envoy
