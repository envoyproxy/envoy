#include "external_mock.h"
#include "external_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(ExternalMacrosTest, WorksWithoutRepositoryOverride) {
  testing::StrictMock<External::MockExternalValueProvider> provider;
  EXPECT_CALL(provider, value()).WillOnce(testing::Return(3));
  EXPECT_EQ(External::externalTestValue(), provider.value());
}

} // namespace
