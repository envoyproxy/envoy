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

// Note this class is not thread-safe, and should be called exceedingly carefully.
template <class T> class TestScopedInjectableLoader {
public:
  TestScopedInjectableLoader(std::unique_ptr<T>&& instance)
      : instance_(std::move(instance)), previous_instance_(InjectableSingleton<T>::getExisting()) {
    InjectableSingleton<T>::clear();
    InjectableSingleton<T>::initialize(instance_.get());
  }
  ~TestScopedInjectableLoader() {
    InjectableSingleton<T>::clear();
    if (previous_instance_) {
      InjectableSingleton<T>::initialize(previous_instance_);
    }
  }

private:
  std::unique_ptr<T> instance_;
  T* const previous_instance_;
};

} // namespace Envoy
