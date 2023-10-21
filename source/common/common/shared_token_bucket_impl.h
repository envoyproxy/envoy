#pragma once

#include "source/common/common/thread.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/common/common/token_bucket_impl.h"

namespace Envoy {

/**
 * A thread-safe wrapper class for TokenBucket interface.
 */
class SharedTokenBucketImpl : public TokenBucket {
public:
  static const char GetImplSyncPoint[];
  static const char ResetCheckSyncPoint[];
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param fill_rate supplies the number of tokens that will return to the bucket on each second.
   * The default is 1.
   * @param mutex supplies the mutex object to be used to ensure thread-safety when the token bucket
   * is shared. By default the class will be thread-unsafe.
   */
  explicit SharedTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                 double fill_rate = 1);

  SharedTokenBucketImpl(const SharedTokenBucketImpl&) = delete;
  SharedTokenBucketImpl(SharedTokenBucketImpl&&) = delete;

  // TokenBucket

  uint64_t consume(uint64_t tokens, bool allow_partial) override;
  uint64_t consume(uint64_t tokens, bool allow_partial,
                   std::chrono::milliseconds& time_to_next_token) override;
  std::chrono::milliseconds nextTokenAvailable() override;

  /**
   * Since the token bucket is shared, only the first reset call will work.
   * Subsequent calls to reset method will be ignored.
   */
  void maybeReset(uint64_t num_tokens) override;

private:
  Thread::MutexBasicLockable mutex_;
  TokenBucketImpl impl_ ABSL_GUARDED_BY(mutex_);
  bool reset_once_ ABSL_GUARDED_BY(mutex_){false};
  mutable Thread::ThreadSynchronizer synchronizer_; // Used only for testing.
  friend class SharedTokenBucketImplTest;
};

} // namespace Envoy
