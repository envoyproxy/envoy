#include "source/common/common/token_bucket_impl.h"

#include <chrono>

namespace Envoy {

namespace {
// The minimal fill rate will be one second every year.
constexpr double kMinFillRate = 1.0 / (365 * 24 * 60 * 60);
} // namespace

TokenBucketImpl::TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate)
    : max_tokens_(max_tokens), fill_rate_(std::max(std::abs(fill_rate), kMinFillRate)),
      tokens_(max_tokens), last_fill_(time_source.monotonicTime()), time_source_(time_source) {}

uint64_t TokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  if (tokens_ < max_tokens_) {
    const auto time_now = time_source_.monotonicTime();
    tokens_ = std::min((std::chrono::duration<double>(time_now - last_fill_).count() * fill_rate_) +
                           tokens_,
                       max_tokens_);
    last_fill_ = time_now;
  }

  if (allow_partial) {
    tokens = std::min(tokens, static_cast<uint64_t>(std::floor(tokens_)));
  }

  if (tokens_ < tokens) {
    return 0;
  }

  tokens_ -= tokens;
  return tokens;
}

uint64_t TokenBucketImpl::consume(uint64_t tokens, bool allow_partial,
                                  std::chrono::milliseconds& time_to_next_token) {
  auto tokens_consumed = consume(tokens, allow_partial);
  time_to_next_token = nextTokenAvailable();
  return tokens_consumed;
}

std::chrono::milliseconds TokenBucketImpl::nextTokenAvailable() {
  // If there are tokens available, return immediately.
  if (tokens_ >= 1) {
    return std::chrono::milliseconds(0);
  }
  // TODO(ramaraochavali): implement a more precise way that works for very low rate limits.
  return std::chrono::milliseconds(static_cast<uint64_t>(std::ceil((1 / fill_rate_) * 1000)));
}

void TokenBucketImpl::maybeReset(uint64_t num_tokens) {
  ASSERT(num_tokens <= max_tokens_);
  tokens_ = num_tokens;
  last_fill_ = time_source_.monotonicTime();
}

AtomicTokenBucketImpl::AtomicTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                             double fill_rate, bool init_fill)
    : max_tokens_(max_tokens), fill_rate_(std::max(std::abs(fill_rate), kMinFillRate)),
      time_source_(time_source) {
  double last_time_in_second =
      std::chrono::duration<double>(time_source.monotonicTime().time_since_epoch()).count();
  if (init_fill) {
    last_time_in_second -= max_tokens_ / fill_rate_;
  }
  last_time_in_second_.store(last_time_in_second);
}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial, double factor) {
  ASSERT(factor >= 0.0);
  ASSERT(factor <= 1.0);

  const double time_now =
      std::chrono::duration<double>(time_source_.monotonicTime().time_since_epoch()).count();
  double last_time_expected = last_time_in_second_.load(std::memory_order_relaxed);
  double last_time_new{};
  const double used_fill_rate = fill_rate_ * factor;

  do {
    if (time_now <= last_time_expected) {
      return 0;
    }

    const double total_tokens =
        std::min(max_tokens_, (time_now - last_time_expected) * used_fill_rate);

    const uint64_t available_tokens = static_cast<uint64_t>(std::floor(total_tokens));
    if (available_tokens < tokens) {
      if (allow_partial) {
        tokens = available_tokens;
      } else {
        return 0;
      }
    }

    // Move the last_time_in_second_ forward by the number of tokens consumed.
    const double total_tokens_new = total_tokens - static_cast<double>(tokens);
    last_time_new = time_now - (total_tokens_new / used_fill_rate);
    ASSERT(last_time_new >= last_time_expected);
  } while (!last_time_in_second_.compare_exchange_weak(last_time_expected, last_time_new));

  return tokens;
}

} // namespace Envoy
