#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Thread {

/**
 * Like the C++11 "basic lockable concept" but a concrete interface vs. a template.
 */
class BasicLockable {
public:
  virtual ~BasicLockable() {}

  virtual void lock() PURE;
  virtual bool try_lock() PURE;
  virtual void unlock() PURE;
};

/**
 * A lock guard that deals with an optional lock.
 */
template <class T> class OptionalLockGuard {
public:
  OptionalLockGuard(T* lock) : lock_(lock) {
    if (lock) {
      lock->lock();
    }
  }

  ~OptionalLockGuard() {
    if (lock_) {
      lock_->unlock();
    }
  }

private:
  T* lock_;
};

} // namespace Thread
} // namespace Envoy
