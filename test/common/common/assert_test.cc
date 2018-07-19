#include "common/common/assert.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(Assert, VariousLogs) {
  Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink()); // For coverage build.
  EXPECT_DEATH({ RELEASE_ASSERT(0, ""); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ RELEASE_ASSERT(0, "With some logs"); },
               ".*assert failure: 0. Details: With some logs.*");
  EXPECT_DEATH({ RELEASE_ASSERT(0 == EAGAIN, fmt::format("using {}", "fmt")); },
               ".*assert failure: 0 == EAGAIN. Details: using fmt.*");
}

} // namespace Envoy
