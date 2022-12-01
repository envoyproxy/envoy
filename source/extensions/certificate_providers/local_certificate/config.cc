#include "source/extensions/certificate_providers/local_certificate/config.h"

#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/extensions/certificate_providers/local_certificate/v3/local_certificate.pb.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/certificate_providers/local_certificate/local_certificate.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace LocalCertificate {

CertificateProvider::CertificateProviderSharedPtr
LocalCertificateFactory::createCertificateProviderInstance(
    const envoy::config::core::v3::TypedExtensionConfig& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api) {
  auto message = std::make_unique<
      envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate>();
  Config::Utility::translateOpaqueConfig(config.typed_config(),
                                         ProtobufMessage::getStrictValidationVisitor(), *message);
  return std::make_shared<Provider>(*message, factory_context, api);
}

ProtobufTypes::MessagePtr LocalCertificateFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::certificate_providers::local_certificate::v3::LocalCertificate()};
}

REGISTER_FACTORY(LocalCertificateFactory, CertificateProvider::CertificateProviderFactory);
} // namespace LocalCertificate
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
