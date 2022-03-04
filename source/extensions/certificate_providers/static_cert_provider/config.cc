#include "source/extensions/certificate_providers/static_cert_provider/config.h"

#include "envoy/extensions/certificate_providers/static_cert_provider/v3/config.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

StaticCertificateProvider::StaticCertificateProvider(
    const envoy::config::core::v3::TypedExtensionConfig& config, Api::Api& api) {

  envoy::extensions::certificate_providers::static_cert_provider::v3::
      StaticCertificateProviderConfig message;
  Config::Utility::translateOpaqueConfig(config.typed_config(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);

  capabilities_.provide_ca_cert = false;
  capabilities_.provide_identity_certs = true;
  capabilities_.generate_identity_certs = false;

  const std::string& cert = Config::DataSource::read(message.certificate(), true, api);
  const std::string& key = Config::DataSource::read(message.private_key(), true, api);
  Envoy::CertificateProvider::Certpair certpair = {cert, key};
  certpairs_.emplace_back(certpair);
}

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
