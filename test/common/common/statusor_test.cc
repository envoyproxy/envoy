#include "source/common/common/statusor.h"
#include "source/common/http/status.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(StatusOr, Initialization) {
  StatusOr<int> statusor(
      Http::prematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired));
  EXPECT_FALSE(statusor.ok());
  EXPECT_TRUE(Http::isPrematureResponseError(statusor.status()));
  EXPECT_EQ("foobar", statusor.status().message());
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired,
            Http::getPrematureResponseHttpCode(statusor.status()));
}

} // namespace Envoy
