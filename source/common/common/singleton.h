#pragma once

namespace Envoy {

// FIXME(alyssar) ThreadSafeSingleton?
/**
 * Immutable singleton pattern. See singleton/manager.h for mutable/destroyable singletons.
 */
template <class T> class ConstSingleton {
public:
  /**
   * Obtain an instance of the singleton for class T.
   *
   * All functions in T must be thread-safe.
   *
   * @return T& a reference to the singleton for class T.
   */
  static T& get() {
    static T* instance = new T();
    return *instance;
  }
};

} // namespace Envoy
