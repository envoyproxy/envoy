#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

namespace {

using DrainAwareHcm =
    envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager;

constexpr uint64_t kDefaultDrainGoawayDelayMs = 5000;

absl::StatusOr<DrainDelayConfig> buildDrainDelayConfig(const DrainAwareHcm& proto_config) {
  const auto& hcm_config = proto_config.hcm_config();

  const auto metadata_delay =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, metadata_delay, 0));
  const auto shutdown_notice_delay =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, shutdown_notice_delay, 0));
  const auto goaway_delay = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(hcm_config, drain_timeout, kDefaultDrainGoawayDelayMs));

  // A zero delay means the step is not requested. Reject configs where a requested step can never
  // be sent or would be sent out of order. The final GOAWAY is always sent.
  if (metadata_delay.count() && !hcm_config.http2_protocol_options().allow_metadata()) {
    return absl::InvalidArgumentError("drain_aware_hcm: metadata_delay requires "
                                      "hcm_config.http2_protocol_options.allow_metadata");
  }
  if (std::max(metadata_delay, shutdown_notice_delay).count() &&
      !proto_config.enable_drain_with_goaway()) {
    return absl::InvalidArgumentError("drain_aware_hcm: metadata_delay or shutdown_notice_delay "
                                      "requires enable_drain_with_goaway");
  }
  if (metadata_delay.count() && metadata_delay >= goaway_delay) {
    return absl::InvalidArgumentError(
        "drain_aware_hcm: metadata_delay must be less than hcm_config.drain_timeout");
  }
  if (metadata_delay.count() && shutdown_notice_delay.count() &&
      metadata_delay >= shutdown_notice_delay) {
    return absl::InvalidArgumentError(
        "drain_aware_hcm: metadata_delay must be less than shutdown_notice_delay");
  }
  if (shutdown_notice_delay.count() && shutdown_notice_delay >= goaway_delay) {
    return absl::InvalidArgumentError(
        "drain_aware_hcm: shutdown_notice_delay must be less than hcm_config.drain_timeout");
  }

  return DrainDelayConfig{shutdown_notice_delay, metadata_delay, goaway_delay};
}

} // namespace

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
    if (tunnel_iohandle) {
      ENVOY_LOG_MISC(debug, "drain_aware_hcm: wired dial-replacement-on-drain for tunnel key='{}'",
                     tunnel_iohandle->connectionKey());
      // Capture the tunnel IoHandle (owned by this connection, so alive whenever the closure can
      // fire). The child forwards to its parent at invocation time; if the parent was torn down
      // first it has already nulled the back-pointer and the call is a no-op.
      // markTunnelDrainingAndDialReplacement is idempotent, so both triggers share it.
      auto redial = [tunnel_iohandle]() {
        tunnel_iohandle->markTunnelDrainingAndDialReplacement();
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
      std::move(codec), connection, factory_context_.drainDecision(),
      factory_context_.serverFactoryContext(), drain_delay_config_, std::move(on_local_drain),
      std::move(callbacks_wrapper));
}

absl::StatusOr<Network::FilterFactoryCb>
DrainAwareHttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  const auto& hcm_config = proto_config.hcm_config();
  auto singletons = HttpConnectionManager::Utility::createSingletons(context);

  auto drain_delay_config = buildDrainDelayConfig(proto_config);
  RETURN_IF_NOT_OK_REF(drain_delay_config.status());

  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<DrainAwareHttpConnectionManagerConfig>(
      hcm_config, context, *singletons.date_provider_, *singletons.route_config_provider_manager_,
      singletons.scoped_routes_config_provider_manager_.get(), *singletons.tracer_manager_,
      *singletons.filter_config_provider_manager_, proto_config.enable_drain_with_goaway(),
      drain_delay_config.value(), creation_status);
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
        server_context.mainThreadDispatcher().timeSource(), context.direction(), server_context);
    filter_manager.addReadFilter(std::move(hcm));
  };
}

REGISTER_FACTORY(DrainAwareHttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
