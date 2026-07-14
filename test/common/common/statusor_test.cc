#include "source/common/common/statusor.h"
#include "source/common/http/status.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

TEST(StatusOr, Initialization) {
  StatusOr<int> statusor(
      Http::prematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired));
  EXPECT_THAT(statusor, Not(IsOk()));
  EXPECT_TRUE(Http::isPrematureResponseError(statusor.status()));
  EXPECT_EQ("foobar", statusor.status().message());
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired,
            Http::getPrematureResponseHttpCode(statusor.status()));
}

} // namespace Envoy
