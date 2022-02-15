#include "source/extensions/certificate_providers/local_certificate/config.h"

#include "envoy/certificate_provider/certificate_provider.h"

#include "source/extensions/certificate_providers/local_certificate/local_certificate.h"

#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

CertificateProvider::CertificateProviderSharedPtr LocalCertificateFactory::createCertificateProviderInstance(
    const envoy::config::core::v3::TypedExtensionConfig& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api) {
  return std::make_shared<Provider>(config, factory_context, api);
}

ProtobufTypes::MessagePtr LocalCertificateFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
    new envoy::extensions::certificate_providers::local_certificate::v3::
    LocalCertificate()};
}

REGISTER_FACTORY(LocalCertificateFactory, CertificateProvider::CertificateProviderFactory);
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
