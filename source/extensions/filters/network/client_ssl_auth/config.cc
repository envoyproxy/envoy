#include "extensions/filters/network/client_ssl_auth/config.h"

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/network/client_ssl_auth/client_ssl_auth.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

Network::FilterFactoryCb
ClientSslAuthConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config;
  Config::FilterJson::translateClientSslAuthFilter(json_config, proto_config);
  return createFilter(proto_config, context);
}

Network::FilterFactoryCb ClientSslAuthConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth&>(proto_config),
      context);
}

ProtobufTypes::MessagePtr ClientSslAuthConfigFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth()};
}

std::string ClientSslAuthConfigFactory::name() { return NetworkFilterNames::get().CLIENT_SSL_AUTH; }

Network::FilterFactoryCb ClientSslAuthConfigFactory::createFilter(
    const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.auth_api_cluster().empty());
  ASSERT(!proto_config.stat_prefix().empty());

  ClientSslAuthConfigSharedPtr filter_config(
      ClientSslAuthConfig::create(proto_config, context.threadLocal(), context.clusterManager(),
                                  context.dispatcher(), context.scope(), context.random()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ClientSslAuthFilter>(filter_config));
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ClientSslAuthConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
