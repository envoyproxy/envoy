#pragma once

namespace Lyft {
/**
 * Immutable singleton pattern.
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

// TODO(htuch): Add a mutable Singleton if there is a need for this.
} // Lyft