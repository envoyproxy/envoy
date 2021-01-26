#include "common/common/shared_token_bucket_impl.h"

#include <chrono>

#include "common/common/lock_guard.h"

namespace Envoy {

const char SharedTokenBucketImpl::GetImplSyncPoint[] = "pre_get_impl";
const char SharedTokenBucketImpl::ResetCheckSyncPoint[] = "post_reset_check";

SharedTokenBucketImpl::SharedTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                             double fill_rate)
    : impl_(max_tokens, time_source, fill_rate), reset_once_(false) {}

uint64_t SharedTokenBucketImpl::consume(uint64_t tokens, bool allow_partial)
/*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ {
  Thread::LockGuard lock(mutex_);
  synchronizer_.syncPoint(GetImplSyncPoint);
  return getImpl().consume(tokens, allow_partial);
};

std::chrono::milliseconds SharedTokenBucketImpl::nextTokenAvailable()
/*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ {
  Thread::LockGuard lock(mutex_);
  synchronizer_.syncPoint(GetImplSyncPoint);
  return getImpl().nextTokenAvailable();
};

void SharedTokenBucketImpl::reset(uint64_t num_tokens) /*ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)*/ {
  Thread::LockGuard lock(mutex_);
  // Don't reset if reset once before.
  if (reset_once_) {
    return;
  }
  reset_once_ = true;
  synchronizer_.syncPoint(ResetCheckSyncPoint);
  getImpl().reset(num_tokens);
};

bool SharedTokenBucketImpl::isMutexLocked() {
  auto locked = mutex_.tryLock();
  if (locked) {
    mutex_.unlock();
  }
  return !locked;
}

} // namespace Envoy
