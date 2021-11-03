#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher_config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Verify we handle an invalid san type enum.
TEST(SanMatcherConfigTest, TestInvalidSanType) {
  envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher san_matcher;
  san_matcher.mutable_matcher()->set_exact("foo.example");
  san_matcher.set_san_type(
      envoy::extensions::transport_sockets::tls::v3::
          SubjectAltNameMatcher_SanType_SubjectAltNameMatcher_SanType_INT_MIN_SENTINEL_DO_NOT_USE_);
  auto matcher = createStringSanMatcher(san_matcher);
  EXPECT_EQ(matcher.get(), nullptr);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
