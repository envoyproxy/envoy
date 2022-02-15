#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/certificate_providers/default_cert_provider/v3/config.pb.h"

#include "source/extensions/certificate_providers/certificate_provider.h"
#include "source/extensions/certificate_providers/factory.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

class DefaultCertificateProvider : public CertificateProvider {
public:
  DefaultCertificateProvider(const envoy::config::core::v3::TypedExtensionConfig& config,
                             Api::Api& api);

  Capabilites capabilities() const override { return capabilities_; };

  const std::string& getCACertificate() const override { return ca_cert_; };

  std::list<Certpair> getTlsCertificates() override { return tls_certificates_; };

  Certpair* generateTlsCertificate(absl::string_view /* server_name */) override {
    return nullptr;
  };

private:
  const std::string ca_cert_;
  Capabilites capabilities_;
  std::list<Certpair> tls_certificates_;
  std::map<absl::string_view, Certpair> cached_tls_certificates_;
};

class DefaultCertificateProviderFactory : public CertificateProviderFactory {
public:
  DefaultCertificateProviderFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::certificate_providers::
                                         default_cert_provider::v3::DefaultCertProviderConfig()};
  }

  Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr
  createCertificateProviderInstance(const envoy::config::core::v3::TypedExtensionConfig& config,
                                    Api::Api& api) override {
    return std::make_shared<DefaultCertificateProvider>(config, api);
  }

  std::string name() const override { return "envoy.certificate_providers.default_cert_provider"; }
};

REGISTER_FACTORY(DefaultCertificateProviderFactory, CertificateProviderFactory);

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
