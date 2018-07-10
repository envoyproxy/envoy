#include "common/common/assert.h"

#include "gtest/gtest.h"

TEST(Assert, VariousLogs) {
  EXPECT_DEATH({ RELEASE_ASSERT(0); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ VERBOSE_RELEASE_ASSERT(0, "With some logs"); },
               ".*assert failure: 0. Details: With some logs.*");
  EXPECT_DEATH(
      {
        VERBOSE_RELEASE_ASSERT(0, "With multiple "
                                      << "logs");
      },
      ".*assert failure: 0. Details: With multiple logs.*");
}
