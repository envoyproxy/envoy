#pragma once

#include "common/common/thread_annotations.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Locking {

// Substitute for absl::MutexLock for use in Envoy, with non-const ref
// style, and more importantly, making it easier to release locks in
// the middle of scopes. absl::Mutex on its own is risky to use in
// the context of code that throws exceptions, as you don't want a
// thrown exception to leave something locked. However
// absl::MutexLock is hard to use when you need to interleave
// variable-scopes and lock-scopes, because it has no Unlock().
//
// Functionally, you could accomplish this with
// std::unique_ptr<absl::MutexLock>, but the clang lock analysis
// does not understand that composition, and would give false locking
// errors; we need a MutexLock with the functionality needed in the
// Envoy codebase (slightly beyond absl's) *and* the lock annotations.
class SCOPED_LOCKABLE MutexLock {
 public:
  explicit MutexLock(absl::Mutex& mutex) EXCLUSIVE_LOCK_FUNCTION(mutex)
      : mutex_(&mutex) {
    mutex_->Lock();
  }
  ~MutexLock() UNLOCK_FUNCTION() { unlock(); }

  void unlock() UNLOCK_FUNCTION() {
    if (mutex_ != nullptr) {
      mutex_->Unlock();
      mutex_ = nullptr;
    }
  }

 private:
  MutexLock(const MutexLock&) = delete;
  void operator=(const MutexLock&) = delete;

  absl::Mutex* mutex_;
};

}  // namespace Locking
}  // namespace Envoy
