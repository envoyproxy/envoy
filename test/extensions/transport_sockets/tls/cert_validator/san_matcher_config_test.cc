#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher_config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Verify that we handle an invalid san type enum.
TEST(SanMatcherConfigTest, TestInvalidSanType) {
  envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher san_matcher;
  san_matcher.mutable_matcher()->set_exact("foo.example");
  san_matcher.set_san_type(
      envoy::extensions::transport_sockets::tls::v3::
          SubjectAltNameMatcher_SanType_SubjectAltNameMatcher_SanType_INT_MIN_SENTINEL_DO_NOT_USE_);
  const Envoy::Ssl::SanMatcherPtr matcher = createStringSanMatcher(san_matcher);
  EXPECT_EQ(matcher.get(), nullptr);
}

// Verify that we get a valid string san matcher for all valid san types.
TEST(SanMatcherConfigTest, TestValidSanType) {
  // Iterate over all san type enums.
  for (envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType san_type =
           envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType_MIN;
       san_type <=
       envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType_MAX;
       san_type = static_cast<
           envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType>(
           static_cast<int>(san_type + 1))) {
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher san_matcher;
    san_matcher.mutable_matcher()->set_exact("foo.example");
    san_matcher.set_san_type(san_type);
    const Envoy::Ssl::SanMatcherPtr matcher = createStringSanMatcher(san_matcher);
    EXPECT_NE(matcher.get(), nullptr);
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
