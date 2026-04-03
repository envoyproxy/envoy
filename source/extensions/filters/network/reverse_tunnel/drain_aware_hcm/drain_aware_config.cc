#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

Http::ServerConnectionPtr DrainAwareHttpConnectionManagerConfig::createBaseCodec(
    Network::Connection& connection, const Buffer::Instance& data,
    Http::ServerConnectionCallbacks& callbacks, Server::OverloadManager& overload_manager) {
  return HttpConnectionManager::HttpConnectionManagerConfig::createCodec(
      connection, data, callbacks, overload_manager);
}

Http::ServerConnectionPtr DrainAwareHttpConnectionManagerConfig::createCodec(
    Network::Connection& connection, const Buffer::Instance& data,
    Http::ServerConnectionCallbacks& callbacks, Server::OverloadManager& overload_manager) {
  auto codec = createBaseCodec(connection, data, callbacks, overload_manager);
  if (codec == nullptr) {
    ENVOY_LOG_MISC(warn, "drain_aware_hcm: base createCodec returned nullptr for connection id={}",
                   connection.id());
    return codec;
  }

  // Use the listener-level DrainDecision, NOT serverFactoryContext().drainManager().
  // /drain_listeners fires the listener-level drain decision; the server-wide drain manager
  // only fires on hot-restart / full server shutdown.
  return std::make_unique<DrainAwareServerConnection>(std::move(codec), connection.dispatcher(),
                                                      factory_context_.drainDecision());
}

absl::StatusOr<Network::FilterFactoryCb>
DrainAwareHttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  const auto& hcm_config = proto_config.hcm_config();
  auto singletons = HttpConnectionManager::Utility::createSingletons(context);

  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<DrainAwareHttpConnectionManagerConfig>(
      hcm_config, context, *singletons.date_provider_, *singletons.route_config_provider_manager_,
      singletons.scoped_routes_config_provider_manager_.get(), *singletons.tracer_manager_,
      *singletons.filter_config_provider_manager_, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  return [singletons, filter_config, &context](Network::FilterManager& filter_manager) -> void {
    auto& server_context = context.serverFactoryContext();
    Server::OverloadManager& overload_manager = context.listenerInfo().shouldBypassOverloadManager()
                                                    ? server_context.nullOverloadManager()
                                                    : server_context.overloadManager();
    auto hcm = std::make_shared<Http::ConnectionManagerImpl>(
        filter_config, context.drainDecision(), server_context.api().randomGenerator(),
        server_context.httpContext(), server_context.runtime(), server_context.localInfo(),
        server_context.clusterManager(), overload_manager,
        server_context.mainThreadDispatcher().timeSource(), context.listenerInfo().direction());
    filter_manager.addReadFilter(std::move(hcm));
  };
}

REGISTER_FACTORY(DrainAwareHttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
