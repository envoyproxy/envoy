#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/drain_aware_hcm.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/drain_aware_hcm.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

class DrainAwareHttpConnectionManagerConfig
    : public HttpConnectionManager::HttpConnectionManagerConfig {
public:
  DrainAwareHttpConnectionManagerConfig(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          config,
      Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
      Router::RouteConfigProviderManager& route_config_provider_manager,
      Config::ConfigProviderManager* scoped_routes_config_provider_manager,
      Tracing::TracerManager& tracer_manager,
      HttpConnectionManager::FilterConfigProviderManager& filter_config_provider_manager,
      absl::Status& creation_status)
      : HttpConnectionManager::HttpConnectionManagerConfig(
            config, context, date_provider, route_config_provider_manager,
            scoped_routes_config_provider_manager, tracer_manager, filter_config_provider_manager,
            creation_status),
        factory_context_(context) {}

  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks,
                                        Server::OverloadManager& overload_manager) override;

protected:
  // Virtual seam for testing: allows tests to override the base codec creation
  // to exercise the nullptr defensive check in createCodec().
  virtual Http::ServerConnectionPtr createBaseCodec(Network::Connection& connection,
                                                    const Buffer::Instance& data,
                                                    Http::ServerConnectionCallbacks& callbacks,
                                                    Server::OverloadManager& overload_manager);

private:
  Server::Configuration::FactoryContext& factory_context_;
};

class DrainAwareHttpConnectionManagerFilterConfigFactory
    : public Common::ExceptionFreeFactoryBase<envoy::extensions::filters::network::reverse_tunnel::
                                                  v3::DrainAwareHttpConnectionManager> {
public:
  DrainAwareHttpConnectionManagerFilterConfigFactory()
      : ExceptionFreeFactoryBase(NetworkFilterNames::get().ReverseTunnelDrainAwareHcm, true) {}

private:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const envoy::extensions::filters::network::reverse_tunnel::v3::
                                        DrainAwareHttpConnectionManager& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
