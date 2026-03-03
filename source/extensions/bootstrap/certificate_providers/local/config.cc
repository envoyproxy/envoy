#include "source/extensions/bootstrap/certificate_providers/local/config.h"

#include "envoy/common/exception.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace CertificateProviders {
namespace Local {

void LocalCertificateProviderExtension::onServerInitialized(Server::Instance& server) {
  absl::Status status = server.secretManager().registerTlsCertificateProviderFactory(
      provider_name_, [provider_name = provider_name_, local_signer = local_signer_config_](
                          const std::string& certificate_name,
                          Server::Configuration::ServerFactoryContext& factory_context)
                          -> Secret::TlsCertificateConfigProviderSharedPtr {
        auto provider_or_error =
            TransportSockets::Tls::CertificateSelectors::OnDemand::
                findOrCreateLocalSignerCertificateProvider(certificate_name, factory_context,
                                                           local_signer);
        if (!provider_or_error.ok()) {
          ENVOY_LOG_EVERY_POW_2(error,
                                "failed to create local certificate provider '{}' for cert '{}': {}",
                                provider_name, certificate_name,
                                provider_or_error.status().message());
          return nullptr;
        }
        return *std::move(provider_or_error);
      });
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
