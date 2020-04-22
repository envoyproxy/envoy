#pragma once

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {

// Note this class is not thread-safe, and should be called exceedingly carefully.
template <class T> class TestThreadsafeSingletonInjector {
public:
  TestThreadsafeSingletonInjector(T* instance) {
    latched_instance_ = &ThreadSafeSingleton<T>::get();
    ThreadSafeSingleton<T>::instance_ = instance;
  }
  ~TestThreadsafeSingletonInjector() { ThreadSafeSingleton<T>::instance_ = latched_instance_; }

private:
  T* latched_instance_;
};

} // namespace Envoy
