#pragma once

#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"
#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

constexpr char ClientSslAuthName[] = "envoy.filters.network.client_ssl_auth";

/**
 * Config registration for the client SSL auth filter. @see NamedNetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth> {
public:
  ClientSslAuthConfigFactory() : FactoryBase(ClientSslAuthName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
