#include "source/common/common/statusor.h"
#include "source/common/http/status.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using ::Envoy::StatusHelpers::HasStatusMessage;

TEST(StatusOr, Initialization) {
  StatusOr<int> statusor(
      Http::prematureResponseError("foobar", Http::Code::ProxyAuthenticationRequired));
  EXPECT_THAT(statusor, HasStatusMessage("foobar"));
  EXPECT_TRUE(Http::isPrematureResponseError(statusor.status()));
  EXPECT_EQ(Http::Code::ProxyAuthenticationRequired,
            Http::getPrematureResponseHttpCode(statusor.status()));
}

} // namespace Envoy
