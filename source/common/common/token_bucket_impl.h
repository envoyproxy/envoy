#pragma once

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"

#include "source/common/common/utility.h"

namespace Envoy {

/**
 * A class that implements token bucket interface (not thread-safe).
 */
class TokenBucketImpl : public TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   */
  explicit TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate = 1);

  // TokenBucket
  uint64_t consume(uint64_t tokens, bool allow_partial) override;
  uint64_t consume(uint64_t tokens, bool allow_partial,
                   std::chrono::milliseconds& time_to_next_token) override;
  std::chrono::milliseconds nextTokenAvailable() override;
  void maybeReset(uint64_t num_tokens) override;

private:
  const double max_tokens_;
  const double fill_rate_;
  double tokens_;
  MonotonicTime last_fill_;
  TimeSource& time_source_;
};

/**
 * Atomic token bucket. This class is thread-safe.
 * TODO(wbpcode): this could be used to replace the SharedTokenBucketImpl once this
  * implementation is proven to be robust.
 */
class AtomicTokenBucketImpl : public TokenBucket {
public:
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   */
  explicit AtomicTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                 double fill_rate = 1);

  uint64_t consume(uint64_t tokens, bool allow_partial) override;
  uint64_t consume(uint64_t tokens, bool allow_partial,
                   std::chrono::milliseconds& time_to_next_token) override;
  // The next token available time is calculated based on the fill rate and the number of tokens
  // available in the bucket. But in the multi-threaded environment, the number of tokens available
  // can change between the calculation and the actual consumption. So, the next token available
  // time may not be accurate.
  std::chrono::milliseconds nextTokenAvailable() override;
  // Reset the number of tokens in the bucket to the specified number. But in the multi-threaded
  // environment, the number of tokens available in the bucket can change by other mayReset calls.
  // So, the final number of tokens in the bucket may not be the same as the specified number.
  void maybeReset(uint64_t num_tokens) override;

private:
  const double max_tokens_{};
  const double fill_rate_{};
  std::atomic<double> total_tokens_{};
  std::atomic<bool> could_fill_{true};
  MonotonicTime last_fill_;
  TimeSource& time_source_;
};

} // namespace Envoy
