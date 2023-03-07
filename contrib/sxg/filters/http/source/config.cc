#include "contrib/sxg/filters/http/source/config.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"
#include "contrib/sxg/filters/http/source/encoder.h"
#include "contrib/sxg/filters/http/source/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

namespace {
Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Secret::SecretManager& secret_manager,
                Server::Configuration::TransportSocketFactoryContext& transport_socket_factory,
                Init::Manager& init_manager) {
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(config.sds_config(), config.name(),
                                                            transport_socket_factory, init_manager);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}
} // namespace

Http::FilterFactoryCb FilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::sxg::v3alpha::SXG& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  const auto& certificate = proto_config.certificate();
  const auto& private_key = proto_config.private_key();

  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider_certificate =
      secretsProvider(certificate, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_certificate == nullptr) {
    throw EnvoyException("invalid certificate secret configuration");
  }
  auto secret_provider_private_key =
      secretsProvider(private_key, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_private_key == nullptr) {
    throw EnvoyException("invalid private_key secret configuration");
  }

  auto secret_reader = std::make_shared<SDSSecretReader>(
      secret_provider_certificate, secret_provider_private_key, context.api());
  auto config = std::make_shared<FilterConfig>(proto_config, context.timeSource(), secret_reader,
                                               stat_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    const EncoderPtr encoder = std::make_unique<EncoderImpl>(config);
    callbacks.addStreamFilter(std::make_shared<Filter>(config, encoder));
  };
}

REGISTER_FACTORY(FilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
