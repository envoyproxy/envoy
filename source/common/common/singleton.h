#pragma once

#include "absl/base/call_once.h"

namespace Envoy {

/**
 * Immutable singleton pattern. See singleton/manager.h for mutable/destroyable singletons.
 */
template <class T> class ConstSingleton {
public:
  /**
   * Obtain an instance of the singleton for class T.
   * @return const T& a reference to the singleton for class T.
   */
  static const T& get() {
    static T* instance = new T();
    return *instance;
  }
};

/* Mutable singleton.  All functions in the singleton class *must* be threadsafe.*/
template <class T> class ThreadSafeSingleton {
public:
  static T& get() {
    absl::call_once(ThreadSafeSingleton<T>::create_once_, &ThreadSafeSingleton<T>::Create);
    return *ThreadSafeSingleton<T>::instance_;
  }

protected:
  template <typename A> friend class TestThreadsafeSingletonInjector;

  static void Create() { instance_ = new T(); }

  static absl::once_flag create_once_;
  static T* instance_;
};

template <class T> absl::once_flag ThreadSafeSingleton<T>::create_once_;

template <class T> T* ThreadSafeSingleton<T>::instance_ = nullptr;

} // namespace Envoy
