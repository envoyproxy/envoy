#include <memory>

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/tls/cert_validator/factory.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher_config.h"

#include "test/extensions/transport_sockets/tls/cert_validator/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

TEST(FactoryTest, TestGetCertValidatorName) {
  EXPECT_EQ("envoy.tls.cert_validator.default", getCertValidatorName(nullptr));
  auto config = std::make_unique<TestCertificateValidationContextConfig>();
  EXPECT_EQ("envoy.tls.cert_validator.default", getCertValidatorName(config.get()));

  envoy::config::core::v3::TypedExtensionConfig custom_config = {};
  custom_config.set_name("envoy.tls.cert_validator.spiffe");
  config = std::make_unique<TestCertificateValidationContextConfig>(custom_config);
  EXPECT_EQ(custom_config.name(), getCertValidatorName(config.get()));
}

TEST(FactoryTest, BackwardsCompatibleSanMatcherFactoryTest) {
  // Create proto for BackwardsCompatibleSanMatcher.
  envoy::config::core::v3::TypedExtensionConfig typed_config = {};
  typed_config.set_name("envoy.san_matchers.backward_compatible_san_matcher");
  envoy::type::matcher::v3::StringMatcher matcher;
  typed_config.set_allocated_typed_config(new ProtobufWkt::Any());
  typed_config.mutable_typed_config()->PackFrom(matcher);

  // Create san matcher.
  Envoy::Ssl::SanMatcherFactory* factory =
      Envoy::Config::Utility::getAndCheckFactory<Envoy::Ssl::SanMatcherFactory>(typed_config, true);
  ASSERT_NE(factory, nullptr);
  Envoy::Ssl::SanMatcherPtr san_matcher = factory->createSanMatcher(typed_config);
  EXPECT_NE(dynamic_cast<BackwardsCompatibleSanMatcher*>(san_matcher.get()), nullptr);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
