#include "common/common/logger.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(RegistryTest, LoggerWithName) {
  EXPECT_EQ(nullptr, Logger::Registry::logger("blah"));
  EXPECT_EQ("upstream", Logger::Registry::logger("upstream")->name());
}

} // namespace Envoy
