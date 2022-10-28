#include "envoy/extensions/transport_sockets/tls/v3/common.pb.validate.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

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
    if (san_type == envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::
                        SAN_TYPE_UNSPECIFIED) {
      EXPECT_DEATH(createStringSanMatcher(san_matcher), "unhandled value");
    } else {
      const SanMatcherPtr matcher = createStringSanMatcher(san_matcher);
      EXPECT_NE(matcher.get(), nullptr);
      // Verify that the message is valid.
      TestUtility::validate(san_matcher);
    }
  }
}

TEST(SanMatcherConfigTest, UnspecifiedSanType) {
  envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher san_matcher;
  san_matcher.mutable_matcher()->set_exact("foo.example");
  // Do not set san_type
  EXPECT_THROW_WITH_REGEX(TestUtility::validate(san_matcher), EnvoyException,
                          "Proto constraint validation failed");
  san_matcher.set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SAN_TYPE_UNSPECIFIED);
  EXPECT_THROW_WITH_REGEX(TestUtility::validate(san_matcher), EnvoyException,
                          "Proto constraint validation failed");

  auto san_type =
      static_cast<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::SanType>(
          static_cast<int>(123));
  san_matcher.set_san_type(san_type);
  EXPECT_EQ(createStringSanMatcher(san_matcher), nullptr);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
