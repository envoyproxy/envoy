#include "source/common/common/token_bucket_impl.h"

#include <chrono>

namespace Envoy {

namespace {
// The minimal fill rate will be one second every year.
constexpr double kMinFillRate = 1.0 / (365 * 24 * 60 * 60);

// Convert seconds duration (in double representation) to nanoseconds duration.
std::chrono::nanoseconds toNanoseconds(double value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(value));
}

// Convert nanoseconds duration to seconds duration (in double representation).
double toSeconds(std::chrono::nanoseconds value) {
  return std::chrono::duration_cast<std::chrono::duration<double>>(value).count();
}

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
  auto time_in_nanoseconds = time_source.monotonicTime();
  if (init_fill) {
    time_in_nanoseconds -= toNanoseconds(max_tokens_ / fill_rate_);
  }
  time_in_nanoseconds_.store(time_in_nanoseconds);
}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial, double factor) {
  ASSERT(factor >= 0.0);
  ASSERT(factor <= 1.0);

  const MonotonicTime time_now = time_source_.monotonicTime();
  MonotonicTime time_expected = time_in_nanoseconds_.load();
  MonotonicTime time_new{};

  const double used_fill_rate = fill_rate_ * factor;

  do {
    if (time_now <= time_expected) {
      return 0;
    }

    const double total_tokens =
        std::min(max_tokens_, toSeconds(time_now - time_expected) * used_fill_rate);

    const uint64_t available_tokens = static_cast<uint64_t>(std::floor(total_tokens));
    if (available_tokens < tokens) {
      if (allow_partial) {
        tokens = available_tokens;
      } else {
        return 0;
      }
    }

    // Move the time_in_nanoseconds_ forward by the number of tokens consumed.
    const double total_tokens_new = total_tokens - static_cast<double>(tokens);
    time_new = time_now - toNanoseconds(total_tokens_new / used_fill_rate);

    ASSERT(time_new >= time_expected);
  } while (!time_in_nanoseconds_.compare_exchange_weak(time_expected, time_new));

  return tokens;
}

} // namespace Envoy
