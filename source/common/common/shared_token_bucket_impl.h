#pragma once

#include "common/common/thread.h"
#include "common/common/thread_synchronizer.h"
#include "common/common/token_bucket_impl.h"

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
  uint64_t consume(uint64_t tokens, bool allow_partial)
      /*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ override;

  std::chrono::milliseconds nextTokenAvailable() /*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ override;

  /**
   * Resets the bucket to contain tokens equal to @param num_tokens
   * Since the token bucket is shared, only the first reset call will work. Subsequent calls to
   * reset method will be ignored.
   */
  void reset(uint64_t num_tokens) /*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ override;

  // Used only for testing.
  Thread::ThreadSynchronizer& synchronizer() { return synchronizer_; };

  // Returns a flag to indicate whether mutex was in lock state.
  bool isMutexLocked() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(false, mutex_);

private:
  TokenBucketImpl& getImpl() { return impl_; }

  Thread::MutexBasicLockable mutex_;
  TokenBucketImpl impl_ ABSL_GUARDED_BY(mutex_);
  bool reset_once_ ABSL_GUARDED_BY(mutex_);
  mutable Thread::ThreadSynchronizer synchronizer_; // Used only for testing.
};

} // namespace Envoy
