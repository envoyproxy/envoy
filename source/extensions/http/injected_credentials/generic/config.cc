#include "source/extensions/http/injected_credentials/generic/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Generic {

namespace {
Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Server::Configuration::ServerFactoryContext& server_context,
                Init::Manager& init_manager) {
  if (config.has_sds_config()) {
    return server_context.secretManager().findOrCreateGenericSecretProvider(
        config.sds_config(), config.name(), server_context, init_manager);
  } else {
    return server_context.secretManager().findStaticGenericSecretProvider(config.name());
  }
}
} // namespace

Common::CredentialInjectorSharedPtr
GenericCredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const Generic& config, const std::string& /*stats_prefix*/,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) {
  const auto& credential_secret = config.credential();

  auto secret_provider = secretsProvider(credential_secret, context, init_manager);

  auto secret_reader = std::make_shared<const Common::SDSSecretReader>(
      std::move(secret_provider), context.threadLocal(), context.api());
  std::string header = config.header();
  if (header.empty()) {
    header = "Authorization";
  }
  return std::make_shared<GenericCredentialInjector>(header, config.header_value_prefix(),
                                                     secret_reader);
}

/**
 * Static registration for the basic auth credential injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(
    GenericCredentialInjectorFactory,
    Envoy::Extensions::Http::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
