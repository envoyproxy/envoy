#include "source/exe/platform_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(PlatformImpl, Basic) {
  PlatformImpl platform;

#ifdef __linux__
  EXPECT_EQ(true, platform.enableCoreDump());
#else
  EXPECT_EQ(false, platform.enableCoreDump());
#endif
}

} // namespace Envoy
