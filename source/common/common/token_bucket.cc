#include "common/common/token_bucket.h"

#include <algorithm>

namespace Envoy {

TokenBucket::TokenBucket(uint64_t max_tokens, double tokens_per_sec)
    : max_tokens_(max_tokens), refill_rate_(tokens_per_sec * nano), tokens_(max_tokens),
      last_fill_(std::chrono::high_resolution_clock::now()) {}

bool TokenBucket::consume(uint64_t tokens) {
  const auto time_now = std::chrono::high_resolution_clock::now();

  if (tokens_ < max_tokens_) {
    tokens_ = std::min(
        (std::chrono::duration_cast<std::chrono::microseconds>(time_now - last_fill_).count() *
         refill_rate_) +
            tokens_,
        max_tokens_);
  }

  last_fill_ = time_now;
  if (tokens_ < tokens) {
    return false;
  }

  tokens_ -= tokens;
  return true;
}

} // namespace Envoy
