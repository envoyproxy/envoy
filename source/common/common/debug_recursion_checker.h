#pragma once

#include "source/common/common/assert.h"

namespace Envoy {
namespace Common {
/**
 * A helper class to assert that a call is not recursive.
 */
class DebugRecursionChecker {
public:
  void enter() {
    ASSERT(!entered_, "A resource should only be entered once");
#if !defined(NDEBUG)
    entered_ = true;
#endif // !defined(NDEBUG)
  }

  void exit() {
#if !defined(NDEBUG)
    entered_ = false;
#endif // !defined(NDEBUG)
  }

private:
  bool entered_ = false;
};

class AutoDebugRecursionChecker {
public:
  explicit AutoDebugRecursionChecker(DebugRecursionChecker& checker) : checker_(checker) {
    checker.enter();
  }

  ~AutoDebugRecursionChecker() { checker_.exit(); }

private:
  DebugRecursionChecker& checker_;
};

} // namespace Common
} // namespace Envoy
