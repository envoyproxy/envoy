#include "source/extensions/filters/http/common/stream_rate_limiter.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/common/assert.h"
#include "source/common/common/token_bucket_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

StreamRateLimiter::StreamRateLimiter(
    uint64_t max_kbps, uint64_t max_buffered_data, std::function<void()> pause_data_cb,
    std::function<void()> resume_data_cb,
    std::function<void(Buffer::Instance&, bool)> write_data_cb, std::function<void()> continue_cb,
    std::function<void(uint64_t, bool, std::chrono::milliseconds)> write_stats_cb,
    TimeSource& time_source, Event::Dispatcher& dispatcher, const ScopeTrackedObject& scope,
    std::shared_ptr<TokenBucket> token_bucket, std::chrono::milliseconds fill_interval)
    : fill_interval_(std::move(fill_interval)), write_data_cb_(write_data_cb),
      continue_cb_(continue_cb), write_stats_cb_(std::move(write_stats_cb)), scope_(scope),
      token_bucket_(std::move(token_bucket)),
      token_timer_(dispatcher.createTimer([this] { onTokenTimer(); })),
      buffer_(resume_data_cb, pause_data_cb,
              []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }) {
  ASSERT(max_buffered_data > 0);
  ASSERT(fill_interval_.count() > 0);
  ASSERT(fill_interval_.count() <= 1000);
  auto max_tokens = kiloBytesToBytes(max_kbps);
  if (!token_bucket_) {
    // Initialize a  new token bucket if caller didn't provide one.
    // The token bucket is configured with a max token count of the number of bytes per second,
    // and refills at the same rate, so that we have a per second limit which refills gradually
    // in one fill_interval_ at a time.
    token_bucket_ = std::make_shared<TokenBucketImpl>(max_tokens, time_source, max_tokens);
  }
  // Reset the bucket to contain only one fill_interval worth of tokens.
  // If the token bucket is shared, only first reset call will work.
  auto initial_tokens = max_tokens * fill_interval_.count() / 1000;
  token_bucket_->maybeReset(initial_tokens);
  ENVOY_LOG(debug,
            "StreamRateLimiter <Ctor>: fill_interval={}ms "
            "initial_tokens={} max_tokens={}",
            fill_interval_.count(), initial_tokens, max_tokens);
  buffer_.setWatermarks(max_buffered_data);
}

void StreamRateLimiter::onTokenTimer() {
  Buffer::OwnedImpl data_to_write;

  // Try to obtain that as many tokens as bytes in the buffer, and then
  // figure out how many bytes to write given the number of tokens we actually got.
  const uint64_t tokens_obtained = token_bucket_->consume(buffer_.length(), true);
  const uint64_t bytes_to_write = std::min(tokens_obtained, buffer_.length());
  ENVOY_LOG(debug,
            "StreamRateLimiter <onTokenTimer>: tokens_needed={} "
            "tokens_obtained={} to_write={}",
            buffer_.length(), tokens_obtained, bytes_to_write);

  // Move the data to write into the output buffer with as little copying as possible.
  // NOTE: This might be moving zero bytes, but that should work fine.
  data_to_write.move(buffer_, bytes_to_write);
  write_stats_cb_(bytes_to_write, buffer_.length() > 0, fill_interval_);

  // If the buffer still contains data in it, we couldn't get enough tokens, so schedule the next
  // token available time.
  // In case of a shared token bucket, this algorithm will prioritize one stream at a time.
  // TODO(nitgoy): add round-robin and other policies for rationing bandwidth.
  if (buffer_.length() > 0) {
    ENVOY_LOG(debug,
              "StreamRateLimiter <onTokenTimer>: scheduling wakeup for {}ms, "
              "buffered={}",
              fill_interval_.count(), buffer_.length());
    token_timer_->enableTimer(fill_interval_, &scope_);
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

void StreamRateLimiter::writeData(Buffer::Instance& incoming_buffer, bool end_stream,
                                  bool trailer_added) {
  auto len = incoming_buffer.length();
  buffer_.move(incoming_buffer);
  saw_end_stream_ = end_stream;
  // If trailer_added is true, set saw_trailers_ to true to continue encode trailers, added
  // after buffer_.move to ensure buffer has data and won't invoke continue_cb_ before
  // processing the data in last data frame.
  if (trailer_added) {
    saw_trailers_ = true;
  }

  ENVOY_LOG(debug,
            "StreamRateLimiter <writeData>: got new {} bytes of data. token "
            "timer {} scheduled.",
            len, !token_timer_->enabled() ? "now" : "already");
  if (!token_timer_->enabled()) {
    // TODO(mattklein123): In an optimal world we would be able to continue iteration with the data
    // we want in the buffer, but have a way to clear end_stream in case we can't send it all.
    // The filter API does not currently support that and it will not be a trivial change to add.
    // Instead we cheat here by scheduling the token timer to run immediately after the stack is
    // unwound, at which point we can directly called encode/decodeData.
    token_timer_->enableTimer(std::chrono::milliseconds(0), &scope_);
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
