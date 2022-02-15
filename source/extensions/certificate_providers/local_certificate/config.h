#pragma once

//#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"
#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/certificate_provider/certificate_provider_factory.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

/**
 * Config registration for the LocalCertificate provider. @see
 * CertificateProvider::CertificateProviderFactory
 */
class LocalCertificateFactory : public CertificateProvider::CertificateProviderFactory {
public:
  std::string name() const override { return "envoy.certificate_providers.local_certificate"; };
  CertificateProvider::CertificateProviderSharedPtr createCertificateProviderInstance(
      const envoy::config::core::v3::TypedExtensionConfig& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context,
      Api::Api& api) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
