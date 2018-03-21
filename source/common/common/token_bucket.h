#pragma once

#include <chrono>
#include <iostream>

#include "envoy/common/time.h"

#include "common/common/utility.h"

namespace Envoy {

/**
 * This class implements the token bucket algorithm (not thread-safe).
 *
 * https://en.wikipedia.org/wiki/Token_bucket
 */
template <typename Ratio = std::ratio<1L, 1L>> class TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximun number of tokens in the bucket.
   * @param refill_rate supplies the number of tokens coming back into the bucket on each second.
   */
  TokenBucket(uint64_t max_tokens,
              MonotonicTime start_time = ProdMonotonicTimeSource::instance_.currentTime())
      : max_tokens_(max_tokens), tokens_(max_tokens), last_fill_(start_time) {}

  /**
   * @param tokens supplies the number of tokens consumed from the bucket on each call. Default
   * is 1.
   * @return true if bucket is not empty, false otherwise.
   */
  bool consume(uint64_t tokens = 1) {
    const auto time_now = ProdMonotonicTimeSource::instance_.currentTime();

    if (tokens_ < max_tokens_) {
      tokens_ =
          std::min(std::chrono::duration<double, Ratio>(time_now - last_fill_).count() + tokens_,
                   max_tokens_);
    }

    last_fill_ = time_now;
    if (tokens_ < tokens) {
      return false;
    }

    tokens_ -= tokens;
    return true;
  }

private:
  const double max_tokens_;
  double tokens_;
  MonotonicTime last_fill_;
};

} // namespace Envoy
