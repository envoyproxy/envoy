#include "extensions/filters/http/oauth/config.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/oauth/v3/oauth.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/oauth/filter.h"
#include "extensions/filters/http/oauth/oauth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

Http::FilterFactoryCb OAuth2Config::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::oauth::v3::OAuth2& proto,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  if (!proto.has_config()) {
    throw EnvoyException("config must be present for global config");
  }

  const auto& proto_config = proto.config();
  const auto& credentials = proto_config.credentials();

  const auto client_secret_config_name = credentials.client_secret_config_name();
  const auto token_secret_config_name = credentials.token_secret_config_name();

  envoy::config::core::v3::ConfigSource config_source;
  auto* const api_config_source = config_source.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto* const grpc_service = api_config_source->add_grpc_services();
  grpc_service->mutable_envoy_grpc()->set_cluster_name(credentials.secrets_cluster());

  auto& secret_manager = context.clusterManager().clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider_client_secret = secret_manager.findOrCreateGenericSecretProvider(
      config_source, client_secret_config_name, transport_socket_factory);
  auto secret_provider_token_secret = secret_manager.findOrCreateGenericSecretProvider(
      config_source, token_secret_config_name, transport_socket_factory);

  auto secret_reader = std::make_shared<SDSSecretReader>(
      secret_provider_client_secret, secret_provider_token_secret, context.api());
  auto config = std::make_shared<FilterConfig>(proto_config, context.clusterManager(),
                                               secret_reader, context.scope(), stats_prefix);

  const std::chrono::milliseconds timeout_duration(
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 3000));

  return
      [&context, config, timeout_duration](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        std::unique_ptr<OAuth2Client> oauth_client = std::make_unique<OAuth2ClientImpl>(
            context.clusterManager(), config->clusterName(), timeout_duration);
        callbacks.addStreamDecoderFilter(
            std::make_shared<OAuth2Filter>(config, std::move(oauth_client)));
      };
}

/*
 * Static registration for the AWS Lambda filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OAuth2Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
