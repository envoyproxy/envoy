#pragma once

#include "envoy/common/time.h"

#include "common/common/utility.h"

namespace Envoy {

/**
 * This class implements the token bucket algorithm (not thread-safe).
 *
 * https://en.wikipedia.org/wiki/Token_bucket
 */
class TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximun number of tokens in the bucket.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second;
   * Default is 1 token/second.
   * @param time_source supplies the the time source and used to facilitate testing; Default is
   * ProdMonotonicTimeSource.
   */
  TokenBucket(uint64_t max_tokens, double fill_rate = 1,
              MonotonicTimeSource& time_source = ProdMonotonicTimeSource::instance_);

  /**
   * @param tokens supplies the number of tokens to be consumed. Default is 1.
   * @return true if bucket is not empty, otherwise it returns false.
   */
  bool consume(uint64_t tokens = 1);

private:
  static constexpr double zero_time_ = 0.0;
  const double max_tokens_;
  const double fill_rate_;
  double tokens_;
  MonotonicTime last_fill_;
  MonotonicTimeSource& time_source_;
};

} // namespace Envoy
