#include "source/extensions/bootstrap/certificate_providers/local/config.h"

#include "envoy/common/exception.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/certificate_providers/local/local_certificate_provider.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace CertificateProviders {
namespace Local {

void LocalCertificateProviderExtension::onServerInitialized(Server::Instance& server) {
  absl::Status status = server.secretManager().registerTlsCertificateProvider(
      provider_name_,
      std::make_shared<::Envoy::Extensions::CertificateProviders::Local::
                           LocalNamedTlsCertificateProvider>(local_signer_config_));
  if (!status.ok()) {
    throw EnvoyException(std::string(status.message()));
  }
}

Server::BootstrapExtensionPtr LocalCertificateProviderFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::certificate_providers::local::v3::
          LocalCertificateProvider&>(config, context.messageValidationVisitor());
  return std::make_unique<LocalCertificateProviderExtension>(message);
}

REGISTER_FACTORY(LocalCertificateProviderFactory,
                 Server::Configuration::BootstrapExtensionFactory);

} // namespace Local
} // namespace CertificateProviders
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
