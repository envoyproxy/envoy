#include "source/common/common/shared_token_bucket_impl.h"

#include <chrono>

#include "source/common/common/lock_guard.h"

namespace Envoy {

const char SharedTokenBucketImpl::GetImplSyncPoint[] = "pre_get_impl";
const char SharedTokenBucketImpl::ResetCheckSyncPoint[] = "post_reset_check";

SharedTokenBucketImpl::SharedTokenBucketImpl(uint64_t max_tokens, TimeSource& time_source,
                                             double fill_rate)
    : impl_(max_tokens, time_source, fill_rate) {}

uint64_t SharedTokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  Thread::LockGuard lock(mutex_);
  synchronizer_.syncPoint(GetImplSyncPoint);
  return impl_.consume(tokens, allow_partial);
};

uint64_t SharedTokenBucketImpl::consume(uint64_t tokens, bool allow_partial,
                                        std::chrono::milliseconds& time_to_next_token) {
  Thread::LockGuard lock(mutex_);
  synchronizer_.syncPoint(GetImplSyncPoint);
  return impl_.consume(tokens, allow_partial, time_to_next_token);
};

std::chrono::milliseconds SharedTokenBucketImpl::nextTokenAvailable() {
  Thread::LockGuard lock(mutex_);
  synchronizer_.syncPoint(GetImplSyncPoint);
  return impl_.nextTokenAvailable();
};

void SharedTokenBucketImpl::maybeReset(uint64_t num_tokens) {
  Thread::LockGuard lock(mutex_);
  // Don't reset if reset once before.
  if (reset_once_) {
    return;
  }
  reset_once_ = true;
  synchronizer_.syncPoint(ResetCheckSyncPoint);
  impl_.maybeReset(num_tokens);
};

} // namespace Envoy
