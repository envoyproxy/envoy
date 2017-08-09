#pragma once

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

} // namespace Envoy
