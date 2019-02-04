#pragma once

#include "common/common/assert.h"

namespace Envoy {
namespace Common {
/**
 * A helper class to prevent re-entry into a non-reentrant class.
 */
class RecursionChecker {
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

class AutoRecursionChecker {
public:
  explicit AutoRecursionChecker(RecursionChecker& checker) : checker_(checker) { checker.enter(); }

  ~AutoRecursionChecker() { checker_.exit(); }

private:
  RecursionChecker& checker_;
};

} // namespace Common
} // namespace Envoy
