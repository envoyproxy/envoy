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
    std::cerr << "==> AAB DebugRecursionChecker::enter" << std::endl;
    ASSERT(!entered_, "A resource should only be entered once");
#if !defined(NDEBUG)
    entered_ = true;
    std::cerr << "==> AAB DebugRecursionChecker::enter set entered_=true" << std::endl;
#endif // !defined(NDEBUG)
  }

  void exit() {
    std::cerr << "==> AAB DebugRecursionChecker::exit" << std::endl;
#if !defined(NDEBUG)
    entered_ = false;
    std::cerr << "==> AAB DebugRecursionChecker::exit set entered_=false" << std::endl;
#endif // !defined(NDEBUG)
  }

private:
  bool entered_ = false;
};

class AutoDebugRecursionChecker {
public:
  explicit AutoDebugRecursionChecker(DebugRecursionChecker& checker) : checker_(checker) {
    std::cerr << "==> AAB AutoDebugRecursionChecker::CTOR" << std::endl;
    checker.enter();
  }

  ~AutoDebugRecursionChecker() {
    std::cerr << "==> AAB AutoDebugRecursionChecker::DTOR" << std::endl;
    checker_.exit();
  }

private:
  DebugRecursionChecker& checker_;
};

} // namespace Common
} // namespace Envoy
