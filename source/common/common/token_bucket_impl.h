#pragma once

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"

#include "common/common/utility.h"

namespace Envoy {

/**
 * A class that implements token bucket interface (not thread-safe).
 */
class TokenBucketImpl : public TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximun number of tokens in the bucket.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   * @param time_source supplies the time source. The default is ProdMonotonicTimeSource.
   */
  explicit TokenBucketImpl(uint64_t max_tokens, double fill_rate = 1,
                           MonotonicTimeSource& time_source = ProdMonotonicTimeSource::instance_);

  bool consume(uint64_t tokens = 1) override;

private:
  const double max_tokens_;
  const double fill_rate_;
  double tokens_;
  MonotonicTime last_fill_;
  MonotonicTimeSource& time_source_;
};

} // namespace Envoy
