#include "source/common/common/token_bucket_impl.h"

#include <atomic>
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
  auto time_in_seconds = timeNowInSeconds();
  if (init_fill) {
    time_in_seconds -= max_tokens_ / fill_rate_;
  }
  time_in_seconds_.store(time_in_seconds, std::memory_order_relaxed);
}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial, double factor) {
  ASSERT(factor >= 0.0);
  ASSERT(factor <= 1.0);

  const double time_now = timeNowInSeconds();
  const double used_fill_rate = fill_rate_ * factor;

  double time_old = time_in_seconds_.load(std::memory_order_relaxed);
  double time_new{};
  do {
    if (time_now <= time_old) {
      return 0;
    }

    const double total_tokens = std::min(max_tokens_, (time_now - time_old) * used_fill_rate);
    const uint64_t available_tokens = static_cast<uint64_t>(std::floor(tokens));

    if (available_tokens < tokens) {
      if (allow_partial) {
        tokens = available_tokens;
      } else {
        return 0;
      }
    }

    // Move the time_in_seconds_ forward by the number of tokens consumed.
    const double total_tokens_new = total_tokens - static_cast<double>(tokens);
    time_new = time_now - (total_tokens_new / used_fill_rate);
    ASSERT(time_new >= time_old);
  } while (!time_in_seconds_.compare_exchange_weak(time_old, time_new, std::memory_order_relaxed));

  return tokens;
}

double AtomicTokenBucketImpl::remainingTokens(double factor) const {
  const double time_now = timeNowInSeconds();
  const double time_old = time_in_seconds_.load(std::memory_order_relaxed);
  ASSERT(time_now >= time_old);
  return std::min(max_tokens_, (time_now - time_old) * fill_rate_) * factor;
}

double AtomicTokenBucketImpl::timeNowInSeconds() const {
  return std::chrono::duration<double>(time_source_.monotonicTime().time_since_epoch()).count();
}

} // namespace Envoy
