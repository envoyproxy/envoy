#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/certificate_providers/static_cert_provider/v3/config.pb.h"

#include "source/extensions/certificate_providers/certificate_provider.h"
#include "source/extensions/certificate_providers/factory.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

class StaticCertificateProvider : public CertificateProvider {
public:
  StaticCertificateProvider(const envoy::config::core::v3::TypedExtensionConfig& config,
                            Api::Api& api);

  Capabilites capabilities() const override { return capabilities_; };

  const std::string& getCACertificate(absl::string_view /*cert_name*/) const override {
    return ca_cert_;
  };

  std::list<Certpair> getCertpair(absl::string_view /*cert_name*/) override { return certpairs_; };

  Certpair* generateIdentityCertificate(const SSL_CLIENT_HELLO* /* ssl_client_hello */) override {
    return nullptr;
  };

  // This is static certificate provider, so callbacks and subsription are not used.
  void onCertpairUpdated(absl::string_view, Certpair) override{};
  void onCACertUpdated(absl::string_view, const std::string) override{};
  void onUpatedFailed() override{};
  void addSubsription(CertificateSubscriptionPtr, std::string) override{};

private:
  Capabilites capabilities_;
  const std::string ca_cert_;
  std::list<Certpair> certpairs_;
};

class StaticCertificateProviderFactory : public CertificateProviderFactory {
public:
  StaticCertificateProviderFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::certificate_providers::static_cert_provider::v3::
            StaticCertificateProviderConfig()};
  }

  Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr
  createCertificateProviderInstance(const envoy::config::core::v3::TypedExtensionConfig& config,
                                    Api::Api& api) override {
    return std::make_shared<StaticCertificateProvider>(config, api);
  }

  std::string name() const override { return "envoy.certificate_providers.static_cert_provider"; }
};

REGISTER_FACTORY(StaticCertificateProviderFactory, CertificateProviderFactory);

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
