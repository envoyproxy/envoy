#include "common/common/token_bucket_impl.h"

#include <chrono>

namespace Envoy {

TokenBucketImpl::TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate,
                                 absl::Mutex* mutex)
    : max_tokens_(max_tokens), fill_rate_(std::abs(fill_rate)), tokens_(max_tokens),
      last_fill_(time_source.monotonicTime()), time_source_(time_source), mutex_(mutex),
      reset_once_(false) {}

uint64_t TokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  absl::MutexLockMaybe lock(mutex_);
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

std::chrono::milliseconds TokenBucketImpl::nextTokenAvailable() {
  // If there are tokens available, return immediately.
  absl::MutexLockMaybe lock(mutex_);
  if (tokens_ >= 1) {
    return std::chrono::milliseconds(0);
  }
  // TODO(ramaraochavali): implement a more precise way that works for very low rate limits.
  return std::chrono::milliseconds(static_cast<uint64_t>(std::ceil((1 / fill_rate_) * 1000)));
}

void TokenBucketImpl::reset(uint64_t num_tokens) {
  ASSERT(num_tokens <= max_tokens_);
  absl::MutexLockMaybe lock(mutex_);
  // Don't reset if thread-safe i.e. shared and reset once before.
  if (mutex_ && reset_once_) {
    return;
  }
  tokens_ = num_tokens;
  last_fill_ = time_source_.monotonicTime();
  reset_once_ = true;
}

} // namespace Envoy
