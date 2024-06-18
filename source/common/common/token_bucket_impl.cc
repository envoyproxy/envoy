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
                                             double fill_rate)
    : max_tokens_(max_tokens), fill_rate_(std::max(std::abs(fill_rate), kMinFillRate)),
      total_tokens_(max_tokens), last_fill_(time_source.monotonicTime()),
      time_source_(time_source) {}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  bool expect_could_fill = true;
  // Only one thread could fill the bucket at same time. No loop is needed here because one
  // thread fills the bucket is enough.
  if (could_fill_.compare_exchange_weak(expect_could_fill, false)) {
    const MonotonicTime time_now = time_source_.monotonicTime();

    // Fill the bucket by the CAS loop. Note double type number is used to store the total tokens
    // because the fill_tokens could be a fraction.
    const double fill_tokens =
        (std::chrono::duration<double>(time_now - last_fill_).count() * fill_rate_);
    double expected_tokens = total_tokens_.load(std::memory_order_relaxed);
    do {
    } while (!total_tokens_.compare_exchange_weak(
        expected_tokens, std::min(max_tokens_, expected_tokens + fill_tokens)));

    // Update the last fill time and set could_fill_ to true to allow other threads to fill the
    // bucket if needed.
    last_fill_ = time_now;
    could_fill_ = true;
  }

  // Try to consume tokens.
  double expected_tokens = total_tokens_.load(std::memory_order_relaxed);
  uint64_t consume_tokens = tokens;
  do {
    // Convert expected total tokens to uint64_t because we can consume only integer tokens.
    const uint64_t available_tokens = static_cast<uint64_t>(std::floor(expected_tokens));

    // No available tokens to consume means the total tokens is less than 1. Return 0.
    if (available_tokens == 0) {
      return 0;
    }

    // No enough available tokens to consume. Return 0 if allow_partial is false.
    // Update consume_tokens to available_tokens if allow_partial is true.
    if (available_tokens < consume_tokens) {
      if (allow_partial) {
        consume_tokens = available_tokens;
      } else {
        return 0;
      }
    }

    // consume_tokens <= available_tokens <= expected_tokens. So the subtraction will not
    // overflow.
    // Loop while the weak CAS fails trying to subtract consume_tokens from expected.
  } while (!total_tokens_.compare_exchange_weak(
      expected_tokens, static_cast<double>(expected_tokens - consume_tokens)));

  return consume_tokens;
}

uint64_t AtomicTokenBucketImpl::consume(uint64_t tokens, bool allow_partial,
                                        std::chrono::milliseconds& time_to_next_token) {
  auto tokens_consumed = consume(tokens, allow_partial);
  time_to_next_token = nextTokenAvailable();
  return tokens_consumed;
}

std::chrono::milliseconds AtomicTokenBucketImpl::nextTokenAvailable() {
  const double expected_tokens = total_tokens_.load(std::memory_order_relaxed);
  if (expected_tokens >= 1) {
    return std::chrono::milliseconds(0);
  }
  return std::chrono::milliseconds(static_cast<uint64_t>(std::ceil((1 / fill_rate_) * 1000)));
}

void AtomicTokenBucketImpl::maybeReset(uint64_t num_tokens) {
  const double new_total_tokens = std::min(static_cast<double>(num_tokens), max_tokens_);

  bool expect_could_fill = true;
  while (true) {
    if (could_fill_.compare_exchange_weak(expect_could_fill, false)) {
      // The CAS is used in other cases because we need to make sure related operations are applied
      // on the latest value or we need to check and change the value atomically.
      // Bu we can set new value directly here because we don't care about the previous value.
      total_tokens_ = new_total_tokens;
      last_fill_ = time_source_.monotonicTime();
      could_fill_ = true;
      break;
    }
  }
}

} // namespace Envoy
