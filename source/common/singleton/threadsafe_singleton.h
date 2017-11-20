#pragma once

#include "absl/base/call_once.h"

namespace Envoy {

/**
 * ThreadSafeSingleton allows easy global cross-thread access to a non-const object.
 *
 * This singleton class should be used for singletons which must be globally
 * accessible but can not be marked as const. All functions in the singleton class
 * *must* be threadsafe.
 *
 * Note that there is heavy resistence in Envoy to adding this type of singleton
 * if data will persist with state changes across tests, as it becomes difficult
 * to write clean unit tests if a state change in one test will persist into
 * another test. Be wary of using it. A example of acceptable usage is OsSyscallsImpl,
 * where the functions are not strictly speaking const, but affect the OS rather than the
 * class itself. An example of unacceptable usage upstream would be for
 * globally accessible stat counters, it would have the aforementioned problem
 * where state "leaks" across tests.
 *
 * */
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
