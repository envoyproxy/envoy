#include "common/common/token_bucket_impl.h"

#include <chrono>

namespace Envoy {

TokenBucketImpl::TokenBucketImpl(uint64_t max_tokens, MonotonicTimeSource& monotonic_time_source,
                                 double fill_rate)
    : max_tokens_(max_tokens), fill_rate_(std::abs(fill_rate)), tokens_(max_tokens),
      last_fill_(monotonic_time_source.currentTime()),
      monotonic_time_source_(monotonic_time_source) {}

bool TokenBucketImpl::consume(uint64_t tokens) {
  if (tokens_ < max_tokens_) {
    const auto time_now = monotonic_time_source_.currentTime();
    tokens_ = std::min((std::chrono::duration<double>(time_now - last_fill_).count() * fill_rate_) +
                           tokens_,
                       max_tokens_);
    last_fill_ = time_now;
  }

  if (tokens_ < tokens) {
    return false;
  }

  tokens_ -= tokens;
  return true;
}

} // namespace Envoy
