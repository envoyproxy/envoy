#pragma once

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"

#include "common/common/utility.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

/**
 * A class that implements token bucket interface.
 */
class TokenBucketImpl : public TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   */
  explicit TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate = 1,
                           bool allow_multiple_resets = false);

  // TokenBucket
  uint64_t consume(uint64_t tokens, bool allow_partial) override;
  std::chrono::milliseconds nextTokenAvailable() override;
  void reset(uint64_t num_tokens) override;

private:
  const double max_tokens_;
  const double fill_rate_;
  double tokens_;
  absl::optional<bool> reset_once_;
  MonotonicTime last_fill_;
  TimeSource& time_source_;
  absl::Mutex mutex_;
};

} // namespace Envoy
