#include <memory>

#include "source/common/tls/cert_validator/factory.h"

#include "test/common/tls/cert_validator/test_common.h"

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

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
