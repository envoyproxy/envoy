#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/token_bucket.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "source/common/buffer/watermark_buffer.h"

namespace Envoy {

class ScopeTrackedObject;

namespace Event {
class Timer;
} // namespace Event

namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * A generic HTTP stream rate limiter. It limits the rate of transfer for a stream to the specified
 * max rate. It calls appropriate callbacks when the buffered data crosses certain high and low
 * watermarks based on the max buffer size. It's used by the fault filter and bandwidth filter as
 * the core logic for their stream limit functionality.
 */
class StreamRateLimiter : Logger::Loggable<Logger::Id::filter> {
public:
  static constexpr std::chrono::milliseconds DefaultFillInterval = std::chrono::milliseconds(50);

  static constexpr uint64_t kiloBytesToBytes(const uint64_t val) { return val * 1024; }

  /**
   * @param max_kbps maximum rate in KiB/s.
   * @param max_buffered_data maximum data to buffer before invoking the pause callback.
   * @param pause_data_cb callback invoked when the limiter has buffered too much data.
   * @param resume_data_cb callback invoked when the limiter has gone under the buffer limit.
   * @param write_data_cb callback invoked to write data to the stream.
   * @param continue_cb callback invoked to continue the stream. This is only used to continue
   *                    trailers that have been paused during body flush.
   * @param time_source the time source to run the token bucket with.
   * @param dispatcher the stream's dispatcher to use for creating timers.
   * @param scope the stream's scope
   */
  StreamRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                    std::function<void()> pause_data_cb, std::function<void()> resume_data_cb,
                    std::function<void(Buffer::Instance&, bool)> write_data_cb,
                    std::function<void()> continue_cb,
                    std::function<void(uint64_t, bool, std::chrono::milliseconds)> write_stats_cb,
                    TimeSource& time_source, Event::Dispatcher& dispatcher,
                    const ScopeTrackedObject& scope,
                    std::shared_ptr<TokenBucket> token_bucket = nullptr,
                    std::chrono::milliseconds fill_interval = DefaultFillInterval);

  /**
   * Called by the stream to write data. All data writes happen asynchronously, the stream should
   * be stopped after this call (all data will be drained from incoming_buffer).
   */
  void writeData(Buffer::Instance& incoming_buffer, bool end_stream, bool trailer_added = false);

  /**
   * Called if the stream receives trailers.
   * Returns true if the read buffer is not completely drained yet.
   */
  bool onTrailers();

  /**
   * Like the owning filter, we must handle inline destruction, so we have a destroy() method which
   * kills any callbacks.
   */
  void destroy() { token_timer_.reset(); }
  bool destroyed() { return token_timer_ == nullptr; }

private:
  friend class StreamRateLimiterTest;
  using TimerPtr = std::unique_ptr<Event::Timer>;

  void onTokenTimer();

  const std::chrono::milliseconds fill_interval_;
  const std::function<void(Buffer::Instance&, bool)> write_data_cb_;
  const std::function<void()> continue_cb_;
  const std::function<void(uint64_t, bool, std::chrono::milliseconds)> write_stats_cb_;
  const ScopeTrackedObject& scope_;
  std::shared_ptr<TokenBucket> token_bucket_;
  Event::TimerPtr token_timer_;
  bool saw_end_stream_{};
  bool saw_trailers_{};
  Buffer::WatermarkBuffer buffer_;
};
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
