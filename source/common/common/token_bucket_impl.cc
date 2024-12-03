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

bool AtomicTokenBucketImpl::consume() {
  constexpr auto consumed_cb = [](double total_tokens) -> double {
    return total_tokens >= 1 ? 1 : 0;
  };
  return consume(consumed_cb) == 1;
}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  const auto consumed_cb = [tokens, allow_partial](double total_tokens) {
    const auto consumed = static_cast<double>(tokens);
    if (total_tokens >= consumed) {
      return consumed; // There are enough tokens to consume.
    }
    // If allow_partial is true, consume all available tokens.
    return allow_partial ? std::max<double>(0, std::floor(total_tokens)) : 0;
  };
  return static_cast<uint64_t>(consume(consumed_cb));
}

double AtomicTokenBucketImpl::remainingTokens() const {
  const double time_now = timeNowInSeconds();
  const double time_old = time_in_seconds_.load(std::memory_order_relaxed);
  return std::min(max_tokens_, (time_now - time_old) * fill_rate_);
}

double AtomicTokenBucketImpl::timeNowInSeconds() const {
  return std::chrono::duration<double>(time_source_.monotonicTime().time_since_epoch()).count();
}

} // namespace Envoy
