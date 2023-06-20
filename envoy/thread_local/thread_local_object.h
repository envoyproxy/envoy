#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "source/common/common/assert.h"

namespace Envoy {
namespace ThreadLocal {

/**
 * All objects that are stored via the ThreadLocal interface must derive from this type.
 */
class ThreadLocalObject {
public:
  virtual ~ThreadLocalObject() = default;

  /**
   * Return the object casted to a concrete type. See getTyped() below for comments on the casts.
   */
  template <class T> T& asType() {
    ASSERT(dynamic_cast<T*>(this) != nullptr);
    return *static_cast<T*>(this);
  }
};

using ThreadLocalObjectSharedPtr = std::shared_ptr<ThreadLocalObject>;

template <class T = ThreadLocalObject> class TypedSlot;

} // namespace ThreadLocal
} // namespace Envoy
