#include "common/common/status.h"
#include "common/common/statusor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(StatusOr, Initialization) {
  StatusOr<int> statusor(PrematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired));
  EXPECT_FALSE(statusor.ok());
  EXPECT_TRUE(IsPrematureResponseError(statusor.status()));
  EXPECT_EQ("foobar", statusor.status().message());
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired,
            GetPrematureResponseHttpCode(statusor.status()));
}

TEST(StatusOr, DefaultInitialization) {
  StatusOr<int> statusor;
  EXPECT_DEATH(GetStatusCode(statusor.status()), "");
}

} // namespace Envoy
