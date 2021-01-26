#pragma once

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"

#include "common/common/thread.h"
#include "common/common/thread_synchronizer.h"
#include "common/common/utility.h"

namespace Envoy {

/**
 * A class that implements token bucket interface.
 */
class TokenBucketImpl : public TokenBucket {
public:
  static const char MutexLockedSyncPoint[];
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   * @param mutex supplies the mutex object to be used to ensure thread-safety when the token bucket
   * is shared. By default the class will be thread-unsafe.
   */
  explicit TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate = 1,
                           Thread::MutexBasicLockable* mutex = nullptr);

  TokenBucketImpl(const TokenBucketImpl&) = delete;
  TokenBucketImpl(TokenBucketImpl&&) = delete;

  // TokenBucket
  uint64_t consume(uint64_t tokens, bool allow_partial) override;
  std::chrono::milliseconds nextTokenAvailable() override;

  /**
   * Resets the bucket to contain tokens equal to @param num_tokens
   * When the token bucket is shared, only the first reset call will work. Subsequent calls to reset
   * method will be ignored.
   */
  void reset(uint64_t num_tokens) override;

  // Used only for testing.
  Thread::ThreadSynchronizer& synchronizer() { return synchronizer_; };

private:
  const double max_tokens_;
  const double fill_rate_;
  double tokens_ ABSL_GUARDED_BY(mutex_);
  MonotonicTime last_fill_ ABSL_GUARDED_BY(mutex_);
  TimeSource& time_source_;
  Thread::MutexBasicLockable* mutex_;
  bool reset_once_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used only for testing.
};

} // namespace Envoy
