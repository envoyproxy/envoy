#include "envoy/common/exception.h"
#include "envoy/extensions/bootstrap/certificate_providers/local/v3/local_certificate_provider.pb.h"
#include "envoy/registry/registry.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace CertificateProviders {
namespace Local {
namespace {

using ::testing::NiceMock;

class LocalCertificateProviderConfigTest : public testing::Test {
protected:
  envoy::extensions::bootstrap::certificate_providers::local::v3::LocalCertificateProvider
  makeConfig() const {
    envoy::extensions::bootstrap::certificate_providers::local::v3::LocalCertificateProvider config;
    TestUtility::loadFromYaml(R"EOF(
      provider_name: local_cert_minter
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
    )EOF",
                             config);
    return config;
  }

  Server::BootstrapExtensionPtr makeExtension() {
    auto* factory =
        Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(
            "envoy.bootstrap.certificate_providers.local");
    EXPECT_NE(factory, nullptr);
    return factory->createBootstrapExtension(makeConfig(), context_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Server::MockInstance> server_;
};

TEST_F(LocalCertificateProviderConfigTest, CreateAndRegisterProvider) {
  auto extension = makeExtension();
  ASSERT_NE(extension, nullptr);
  EXPECT_NO_THROW(extension->onServerInitialized(server_));
}

TEST_F(LocalCertificateProviderConfigTest, DuplicateProviderNameThrows) {
  auto first = makeExtension();
  auto second = makeExtension();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  EXPECT_NO_THROW(first->onServerInitialized(server_));
  EXPECT_THROW_WITH_REGEX(second->onServerInitialized(server_), EnvoyException,
                          "duplicate TLS certificate provider name local_cert_minter");
}

} // namespace
} // namespace Local
} // namespace CertificateProviders
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
