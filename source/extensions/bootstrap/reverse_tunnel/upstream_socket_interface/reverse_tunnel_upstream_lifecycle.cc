#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_upstream_lifecycle.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace {

UpstreamSocketManager* getThreadLocalSocketManager() {
  auto* upstream_interface =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (upstream_interface == nullptr) {
    return nullptr;
  }

  auto* acceptor = const_cast<ReverseTunnelAcceptor*>(
      dynamic_cast<const ReverseTunnelAcceptor*>(upstream_interface));
  if (acceptor == nullptr) {
    return nullptr;
  }

  auto* tls_registry = acceptor->getLocalRegistry();
  return (tls_registry != nullptr) ? tls_registry->socketManager() : nullptr;
}

void maybeSetStringFilterState(StreamInfo::FilterState& filter_state, absl::string_view key,
                               absl::string_view value) {
  if (value.empty() || filter_state.hasDataWithName(key)) {
    return;
  }

  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>(value),
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection);
}

void maybeSetUint64FilterState(StreamInfo::FilterState& filter_state, absl::string_view key,
                               uint64_t value) {
  if (filter_state.hasDataWithName(key)) {
    return;
  }

  filter_state.setData(key, std::make_shared<StreamInfo::UInt64AccessorImpl>(value),
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection);
}

} // namespace

void ReverseTunnelUpstreamLifecycleFilter::initializeReadFilterCallbacks(
    Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus ReverseTunnelUpstreamLifecycleFilter::onNewConnection() {
  ASSERT(read_callbacks_ != nullptr);
  read_callbacks_->connection().addConnectionCallbacks(*this);

  const auto& socket = read_callbacks_->connection().getSocket();
  if (!socket) {
    return Network::FilterStatus::Continue;
  }

  // Safe to use fdDoNotUse() here: the lifecycle filter is a connection callback,
  // guaranteeing the socket is alive during onNewConnection() and onEvent(). The
  // UpstreamReverseConnectionIOHandle calls markSocketDead() before releasing the
  // socket, preventing FD recycling before lifecycle tracking cleanup. All reverse
  // tunnel FD-indexed maps follow this same ownership pattern.
  fd_ = socket->ioHandle().fdDoNotUse();
  auto* socket_manager = getThreadLocalSocketManager();
  if (socket_manager == nullptr) {
    return Network::FilterStatus::Continue;
  }

  const auto* lifecycle = socket_manager->getLifecycleInfo(fd_);
  if (lifecycle == nullptr) {
    return Network::FilterStatus::Continue;
  }

  lifecycle_ = *lifecycle;
  socket_manager->markUpstreamLifecycleFilterAttached(fd_);
  if (const auto& filter_state = read_callbacks_->connection().streamInfo().filterState();
      filter_state != nullptr) {
    maybeSetStringFilterState(*filter_state, kFilterStateNodeId, lifecycle_->node_id);
    maybeSetStringFilterState(*filter_state, kFilterStateClusterId, lifecycle_->cluster_id);
    maybeSetStringFilterState(*filter_state, kFilterStateTenantId, lifecycle_->tenant_id);
    maybeSetStringFilterState(*filter_state, kFilterStateWorker, lifecycle_->worker);
    if (lifecycle_->fd >= 0) {
      maybeSetUint64FilterState(*filter_state, kFilterStateFd, lifecycle_->fd);
    }
  }

  return Network::FilterStatus::Continue;
}

void ReverseTunnelUpstreamLifecycleFilter::onEvent(Network::ConnectionEvent event) {
  if (!lifecycle_.has_value()) {
    return;
  }

  auto* socket_manager = getThreadLocalSocketManager();
  if (socket_manager == nullptr) {
    return;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    lifecycle_->close_reason = std::string(kLifecycleCloseReasonRemoteClose);
    socket_manager->setCloseReason(fd_, lifecycle_->close_reason);
    socket_manager->maybeEmitDeferredCloseLog(fd_, lifecycle_->close_reason);
    return;
  }

  if (event != Network::ConnectionEvent::LocalClose) {
    return;
  }

  absl::string_view close_reason = read_callbacks_->connection().localCloseReason();
  if (close_reason.empty()) {
    close_reason = kLifecycleCloseReasonLocalClose;
  }

  lifecycle_->close_reason = std::string(close_reason);
  socket_manager->setCloseReason(fd_, close_reason);

  if (close_reason != StreamInfo::LocalCloseReasons::get().Http2PingTimeout ||
      keepalive_timeout_logged_) {
    socket_manager->maybeEmitDeferredCloseLog(fd_, close_reason);
    return;
  }

  keepalive_timeout_logged_ = true;
  if (auto* extension = socket_manager->getUpstreamExtension()) {
    extension->emitConnectionLifecycleLog(kLifecycleEventHttp2KeepaliveTimeout,
                                          read_callbacks_->connection().streamInfo(), *lifecycle_,
                                          AccessLog::AccessLogType::UpstreamEnd, {}, close_reason);
  }
  socket_manager->maybeEmitDeferredCloseLog(fd_, close_reason);
}

Network::FilterFactoryCb ReverseTunnelUpstreamLifecycleConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message&, Server::Configuration::UpstreamFactoryContext&) {
  return [](Network::FilterManager& filter_manager) {
    filter_manager.addReadFilter(std::make_shared<ReverseTunnelUpstreamLifecycleFilter>());
  };
}

ProtobufTypes::MessagePtr ReverseTunnelUpstreamLifecycleConfigFactory::createEmptyConfigProto() {
  return std::make_unique<Protobuf::Empty>();
}

REGISTER_FACTORY(ReverseTunnelUpstreamLifecycleConfigFactory,
                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
