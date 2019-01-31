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
    RELEASE_ASSERT(!entered_, "A resource should only be entered once");
    entered_ = true;
  }

  void exit() {
    entered_ = false;
  }

private:
  bool entered_ = false;
};

class AutoRecursionChecker {
public:
  explicit AutoRecursionChecker(RecursionChecker& checker): checker_(checker) {
    checker.enter();
  }

  ~AutoRecursionChecker() {
    checker_.exit();
  }

private:
  RecursionChecker &checker_;
};

}
}
