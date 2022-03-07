#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/certificate_providers/static_cert_provider/v3/config.pb.h"

#include "source/extensions/certificate_providers/factory.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

class StaticCertificateProvider : public CertificateProvider::CertificateProvider {
public:
  StaticCertificateProvider(const envoy::config::core::v3::TypedExtensionConfig& config,
                            Api::Api& api);

  Capabilites capabilities() const override { return capabilities_; };

  const std::string& caCert(absl::string_view /*cert_name*/) const override { return ca_cert_; };

  std::list<Envoy::CertificateProvider::Certpair>
  certPairs(absl::string_view /*cert_name*/) override {
    return cert_pairs_;
  };

  Common::CallbackHandlePtr addUpdateCallback(absl::string_view /*cert_name*/,
                                              std::function<void()> /*callback*/) override {
    return nullptr;
  }

private:
  Capabilites capabilities_;
  const std::string ca_cert_;
  std::list<Envoy::CertificateProvider::Certpair> cert_pairs_;
};

class StaticCertificateProviderFactory : public CertificateProviderFactory {
public:
  StaticCertificateProviderFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::certificate_providers::static_cert_provider::v3::
            StaticCertificateProviderConfig()};
  }

  Envoy::CertificateProvider::CertificateProviderSharedPtr
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
