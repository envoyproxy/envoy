#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/certificate_provider/certificate_provider_factory.h"

#include "source/common/certificate_provider/certificate_provider_manager_impl.h"

#include "test/common//certificate_provider/test_certificate_provider.pb.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace CertificateProvider {

class TestCertificateProviderImpl : public Envoy::CertificateProvider::CertificateProvider {
public:
  TestCertificateProviderImpl(
      const test::common::certificate_provider::TestCertificateProviderConfig&,
      Server::Configuration::TransportSocketFactoryContext&, Api::Api&){};

  Envoy::CertificateProvider::CertificateProvider::Capabilities capabilities() const override {
    return cap_;
  };
  const std::string trustedCA(const std::string&) const override { return ""; };
  std::vector<
      std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
  tlsCertificates(const std::string&) const override {
    return tls_certificates_;
  };
  Envoy::CertificateProvider::OnDemandUpdateHandlePtr addOnDemandUpdateCallback(
      const std::string, absl::optional<Envoy::CertificateProvider::OnDemandUpdateMetadataPtr>,
      Event::Dispatcher&, ::Envoy::CertificateProvider::OnDemandUpdateCallbacks&) override {
    return nullptr;
  };
  Common::CallbackHandlePtr addUpdateCallback(const std::string&, std::function<void()>) override {
    return nullptr;
  };

private:
  Envoy::CertificateProvider::CertificateProvider::Capabilities cap_;
  std::vector<
      std::reference_wrapper<const envoy::extensions::transport_sockets::tls::v3::TlsCertificate>>
      tls_certificates_;
};

class CertificateProviderFactoryForTest
    : public Envoy::CertificateProvider::CertificateProviderFactory {
public:
  std::string name() const override {
    return "envoy.certificate_providers.test_certificate_provider";
  };
  Envoy::CertificateProvider::CertificateProviderSharedPtr createCertificateProviderInstance(
      const envoy::config::core::v3::TypedExtensionConfig& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context,
      Api::Api& api) override {
    auto message =
        std::make_unique<test::common::certificate_provider::TestCertificateProviderConfig>();
    Config::Utility::translateOpaqueConfig(config.typed_config(),
                                           ProtobufMessage::getStrictValidationVisitor(), *message);
    return std::make_shared<TestCertificateProviderImpl>(*message, factory_context, api);
  };

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new test::common::certificate_provider::TestCertificateProviderConfig()};
  };
};

class CertificateProviderManagerImplTest : public testing::Test,
                                           public Logger::Loggable<Logger::Id::cert_provider> {
protected:
  CertificateProviderManagerImplTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

TEST_F(CertificateProviderManagerImplTest, AddAndGetCertificateProvider) {
  CertificateProviderFactoryForTest test_certificate_provider_factory;
  Registry::InjectFactory<Envoy::CertificateProvider::CertificateProviderFactory>
      registered_factory(test_certificate_provider_factory);

  const std::string yaml_string = R"EOF(
    name: envoy.certificate_providers.test_certificate_provider
    typed_config:
        "@type": type.googleapis.com/test.common.certificate_provider.TestCertificateProviderConfig
  )EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  CertificateProviderManagerPtr cert_provider_manager(new CertificateProviderManagerImpl(*api_));
  EXPECT_NO_THROW(
      cert_provider_manager->addCertificateProvider("test_provider", config, factory_context_));
  auto cert_provider = cert_provider_manager->getCertificateProvider("test_provider");
  EXPECT_NE(cert_provider, nullptr);
}

TEST_F(CertificateProviderManagerImplTest, GetCertificateProviderNotPresent) {
  CertificateProviderManagerPtr cert_provider_manager(new CertificateProviderManagerImpl(*api_));
  auto cert_provider = cert_provider_manager->getCertificateProvider("test_provider");
  EXPECT_EQ(cert_provider, nullptr);
}

} // namespace CertificateProvider
} // namespace Envoy
