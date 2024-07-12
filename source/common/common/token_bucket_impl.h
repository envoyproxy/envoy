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
 */
class AtomicTokenBucketImpl {
public:
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   * @param init_fill supplies whether the bucket should be initialized with max_tokens.
   */
  explicit AtomicTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                 double fill_rate = 1.0, bool init_fill = true);

  // This reference https://github.com/facebook/folly/blob/main/folly/TokenBucket.h.
  template <class GetConsumedTokens> double consume(const GetConsumedTokens& cb) {
    const double time_now = timeNowInSeconds();

    double time_old = time_in_seconds_.load(std::memory_order_relaxed);
    double time_new{};
    double consumed{};
    do {
      const double total_tokens = std::min(max_tokens_, (time_now - time_old) * fill_rate_);
      if (consumed = cb(total_tokens); consumed == 0) {
        return 0;
      }

      // There are two special cases that should rarely happen in practice but we will not
      // prevent them in this common template method:
      // The consumed is negative. It means the token is added back to the bucket.
      // The consumed is larger than total_tokens. It means the bucket is overflowed and future
      // tokens are consumed.

      // Move the time_in_seconds_ forward by the number of tokens consumed.
      const double total_tokens_new = total_tokens - consumed;
      time_new = time_now - (total_tokens_new / fill_rate_);
    } while (
        !time_in_seconds_.compare_exchange_weak(time_old, time_new, std::memory_order_relaxed));

    return consumed;
  }

  /**
   * Consumes one tokens from the bucket.
   * @return true if the token is consumed, false otherwise.
   */
  bool consume();

  /**
   * Consumes multiple tokens from the bucket.
   * @param tokens the number of tokens to consume.
   * @param allow_partial whether to allow partial consumption.
   * @return the number of tokens consumed.
   */
  uint64_t consume(uint64_t tokens, bool allow_partial);

  /**
   * Get the maximum number of tokens in the bucket. The actual maximum number of tokens in the
   * bucket may be changed with the factor.
   * @return the maximum number of tokens in the bucket.
   */
  double maxTokens() const { return max_tokens_; }

  /**
   * Get the fill rate of the bucket. This is a constant for the lifetime of the bucket. But note
   * the actual used fill rate will multiply the dynamic factor.
   * @return the fill rate of the bucket.
   */
  double fillRate() const { return fill_rate_; }

  /**
   * Get the remaining number of tokens in the bucket. This is a snapshot and may change after the
   * call.
   * @return the remaining number of tokens in the bucket.
   */
  double remainingTokens() const;

private:
  double timeNowInSeconds() const;

  const double max_tokens_;
  const double fill_rate_;

  std::atomic<double> time_in_seconds_{};
  TimeSource& time_source_;
};

} // namespace Envoy
