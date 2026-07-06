#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include <functional>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
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
  // Build the "dial a replacement tunnel" action shared by two triggers: on_local_drain (this
  // connection drains locally via max_connection_duration/graceful shutdown/listener drain) and
  // on_peer_goaway (the peer's client codec sends a GOAWAY). Either way we ask the initiator
  // IOHandle to dial a replacement immediately so new requests get a fresh tunnel while in-flight
  // streams finish on the old one. Opt-in via enable_drain_with_goaway.
  //
  // Recover the IOHandle by typed cast on the accepted socket: connection.localAddress() can't be
  // used because the listener exposes its bound rc:// address, not the socket's local port.
  std::function<void()> on_local_drain = nullptr;
  std::function<void()> on_peer_goaway = nullptr;
  if (enable_drain_with_goaway_) {
    Envoy::Extensions::Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*
        tunnel_iohandle = nullptr;
    if (connection.getSocket() != nullptr) {
      tunnel_iohandle = dynamic_cast<
          Envoy::Extensions::Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*>(
          &connection.getSocket()->ioHandle());
    }
    if (tunnel_iohandle != nullptr && tunnel_iohandle->parent() != nullptr) {
      std::string connection_key = tunnel_iohandle->connectionKey();
      ENVOY_LOG_MISC(debug, "drain_aware_hcm: wired dial-replacement-on-drain for tunnel key='{}'",
                     connection_key);
      // Capture the tunnel IoHandle (owned by this connection, so alive whenever the closure can
      // fire) and resolve parent() at invocation rather than caching the raw parent pointer: the
      // parent ReverseConnectionIOHandle may be torn down first, in which case it has nulled this
      // back-pointer. markTunnelDrainingAndDialReplacement is idempotent, so both triggers share
      // it.
      auto redial = [tunnel_iohandle, connection_key]() {
        if (auto* owner = tunnel_iohandle->parent(); owner != nullptr) {
          owner->markTunnelDrainingAndDialReplacement(connection_key);
        }
      };
      on_local_drain = redial;
      on_peer_goaway = redial;
    }
  }

  // If a peer GOAWAY should trigger a re-dial, interpose a callbacks wrapper between the codec and
  // the HCM so we observe the received GOAWAY (the default server path ignores it). Otherwise use
  // the HCM callbacks directly.
  std::unique_ptr<DrainAwareServerConnectionCallbacks> callbacks_wrapper;
  Http::ServerConnectionCallbacks* effective_callbacks = &callbacks;
  if (on_peer_goaway != nullptr) {
    callbacks_wrapper =
        std::make_unique<DrainAwareServerConnectionCallbacks>(callbacks, std::move(on_peer_goaway));
    effective_callbacks = callbacks_wrapper.get();
  }

  auto codec = createBaseCodec(connection, data, *effective_callbacks, overload_manager);
  if (codec == nullptr) {
    ENVOY_LOG_MISC(warn, "drain_aware_hcm: base createCodec returned nullptr for connection id={}",
                   connection.id());
    return codec;
  }

  // Use the listener-level DrainDecision, NOT serverFactoryContext().drainManager().
  // /drain_listeners fires the listener-level drain decision; the server-wide drain manager
  // only fires on hot-restart / full server shutdown.
  return std::make_unique<DrainAwareServerConnection>(
      std::move(codec), connection.dispatcher(), factory_context_.drainDecision(),
      std::move(on_local_drain), std::move(callbacks_wrapper));
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
      *singletons.filter_config_provider_manager_, proto_config.enable_drain_with_goaway(),
      creation_status);
  RETURN_IF_NOT_OK(creation_status);

  return [singletons, filter_config, &context](Network::FilterManager& filter_manager) -> void {
    auto& server_context = context.serverFactoryContext();
    Server::OverloadManager& overload_manager = context.shouldBypassOverloadManager()
                                                    ? server_context.nullOverloadManager()
                                                    : server_context.overloadManager();
    auto hcm = std::make_shared<Http::ConnectionManagerImpl>(
        filter_config, context.drainDecision(), server_context.api().randomGenerator(),
        server_context.httpContext(), server_context.runtime(), server_context.localInfo(),
        server_context.clusterManager(), overload_manager,
        server_context.mainThreadDispatcher().timeSource(), context.direction());
    filter_manager.addReadFilter(std::move(hcm));
  };
}

REGISTER_FACTORY(DrainAwareHttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
