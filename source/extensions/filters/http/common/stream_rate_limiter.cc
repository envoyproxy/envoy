#include "extensions/filters/http/common/stream_rate_limiter.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/token_bucket_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

StreamRateLimiter::StreamRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                                     std::function<void()> pause_data_cb,
                                     std::function<void()> resume_data_cb,
                                     std::function<void(Buffer::Instance&, bool)> write_data_cb,
                                     std::function<void()> continue_cb, TimeSource& time_source,
                                     Event::Dispatcher& dispatcher, const ScopeTrackedObject& scope,
                                     std::shared_ptr<TokenBucket> token_bucket,
                                     uint64_t fill_rate)
    : // bytes_per_time_slice is KiB converted to bytes divided by the number of ticks per second.
      bytes_per_time_slice_((max_kbps * 1024) / fill_rate), write_data_cb_(write_data_cb),
      continue_cb_(continue_cb), scope_(scope), token_bucket_(std::move(token_bucket)),
      token_timer_(dispatcher.createTimer([this] { onTokenTimer(); })),
      buffer_(resume_data_cb, pause_data_cb,
              []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }) {
  ASSERT(bytes_per_time_slice_ > 0);
  ASSERT(max_buffered_data > 0);
  if (!token_bucket_) {
    // Initialize a  new token bucket if caller didn't provide one.
    // The token bucket is configured with a max token count of the number of ticks per second,
    // and refills at the same rate, so that we have a per second limit which refills gradually in
    // 1/fill_rate intervals.
    token_bucket_ = std::make_shared<TokenBucketImpl>(fill_rate, time_source, fill_rate);
  }
  buffer_.setWatermarks(max_buffered_data);
}

void StreamRateLimiter::onTokenTimer() {
  // TODO(nitgoy): remove all debug trace logs before final merge
  ENVOY_LOG(trace, "stream limiter: timer wakeup: buffered={}", buffer_.length());
  Buffer::OwnedImpl data_to_write;

  if (!saw_data_) {
    // The first time we see any data on this stream (via writeData()), reset the number of tokens
    // to 1. This will ensure that we start pacing the data at the desired rate (and don't send a
    // full 1 sec of data right away which might not introduce enough delay for a stream that
    // doesn't have enough data to span more than 1s of rate allowance). Once we reset, we will
    // subsequently allow for bursting within the second to account for our data provider being
    // bursty. The shared token bucket will reset only first time even when called reset from
    // multiple streams.
    token_bucket_->reset(1);
    saw_data_ = true;
  }

  // Compute the number of tokens needed (rounded up), try to obtain that many tickets, and then
  // figure out how many bytes to write given the number of tokens we actually got.
  const uint64_t tokens_needed =
      (buffer_.length() + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = token_bucket_->consume(tokens_needed, true);
  const uint64_t bytes_to_write =
      std::min(tokens_obtained * bytes_per_time_slice_, buffer_.length());
  ENVOY_LOG(trace, "stream limiter: tokens_needed={} tokens_obtained={} to_write={}", tokens_needed,
            tokens_obtained, bytes_to_write);

  // Move the data to write into the output buffer with as little copying as possible.
  // NOTE: This might be moving zero bytes, but that should work fine.
  data_to_write.move(buffer_, bytes_to_write);

  // If the buffer still contains data in it, we couldn't get enough tokens, so schedule the next
  // token available time.
  // In case of a shared token bucket, this algorithm will prioritize one stream at a time.
  // TODO(nitgoy): add round-robin and other policies for rationing bandwidth.
  if (buffer_.length() > 0) {
    const std::chrono::milliseconds ms = token_bucket_->nextTokenAvailable();
    if (ms.count() > 0) {
      ENVOY_LOG(trace, "stream limiter: scheduling wakeup for {}ms", ms.count());
      token_timer_->enableTimer(ms, &scope_);
    }
  }

  // Write the data out, indicating end stream if we saw end stream, there is no further data to
  // send, and there are no trailers.
  write_data_cb_(data_to_write, saw_end_stream_ && buffer_.length() == 0 && !saw_trailers_);

  // If there is no more data to send and we saw trailers, we need to continue iteration to release
  // the trailers to further filters.
  if (buffer_.length() == 0 && saw_trailers_) {
    continue_cb_();
  }
}

void StreamRateLimiter::writeData(Buffer::Instance& incoming_buffer, bool end_stream) {
  ENVOY_LOG(trace, "stream limiter: incoming data length={} buffered={}", incoming_buffer.length(),
            buffer_.length());
  buffer_.move(incoming_buffer);
  saw_end_stream_ = end_stream;
  if (!token_timer_->enabled()) {
    // TODO(mattklein123): In an optimal world we would be able to continue iteration with the data
    // we want in the buffer, but have a way to clear end_stream in case we can't send it all.
    // The filter API does not currently support that and it will not be a trivial change to add.
    // Instead we cheat here by scheduling the token timer to run immediately after the stack is
    // unwound, at which point we can directly called encode/decodeData.
    token_timer_->enableTimer(std::chrono::milliseconds(0), &scope_);
    ENVOY_LOG(trace, "stream limiter: token timer is{}enabled for first time.",
              token_timer_->enabled() ? " " : " not ");
  }
}

bool StreamRateLimiter::onTrailers() {
  saw_end_stream_ = true;
  saw_trailers_ = true;
  return buffer_.length() > 0;
}
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
