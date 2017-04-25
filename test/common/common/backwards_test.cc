#include "common/common/backward.h"

#include "gtest/gtest.h"

TEST(Backward, Basic) {
  // There isn't much to test here and this feature is really just useful for
  // debugging.  This test simply verifies that we do not cause a crash when
  // logging a backtrace, and covers the added lines.
  BackwardsTrace tracer(true); // Log at a level that means we cover lines even in opt
  tracer.Capture();
  tracer.Log();
}

TEST(Backward, InvalidUsageTest) {
  // Ensure we do not crash if logging is attempted when there was no trace captured
  BackwardsTrace tracer(true);
  tracer.Log();
}
