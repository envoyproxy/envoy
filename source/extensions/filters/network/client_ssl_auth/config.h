#pragma once

#include "envoy/config/filter/network/client_ssl_auth/v2/client_ssl_auth.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

/**
 * Config registration for the client SSL auth filter. @see NamedNetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth> {
public:
  ClientSslAuthConfigFactory() : FactoryBase(NetworkFilterNames::get().CLIENT_SSL_AUTH) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
