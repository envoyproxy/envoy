#pragma once

#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"
#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.validate.h"

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
          envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth> {
public:
  ClientSslAuthConfigFactory() : FactoryBase(NetworkFilterNames::get().ClientSslAuth) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
