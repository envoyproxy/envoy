#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"

#include <optional>

#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/http1/http1_handshake_initiator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// RCConnectionWrapper constructor implementation
RCConnectionWrapper::RCConnectionWrapper(ReverseConnectionIOHandle& parent,
                                         Network::ClientConnectionPtr connection,
                                         Upstream::HostDescriptionConstSharedPtr host,
                                         const std::string& cluster_name)
    : parent_(parent), connection_(std::move(connection)), host_(std::move(host)),
      cluster_name_(cluster_name) {
  ENVOY_LOG(debug, "RCConnectionWrapper: created for reverse connections");
}

// RCConnectionWrapper destructor implementation.
RCConnectionWrapper::~RCConnectionWrapper() {
  ENVOY_LOG(debug, "RCConnectionWrapper destructor called");
  if (!shutdown_called_) {
    this->shutdown();
  }
}

void RCConnectionWrapper::onEvent(Network::ConnectionEvent event) {
  // Any close before the handshake completes is terminal for this attempt. A successful handshake
  // removes these callbacks, so a close seen here always means the peer or local stack dropped the
  // connection first.
  if (event != Network::ConnectionEvent::RemoteClose &&
      event != Network::ConnectionEvent::LocalClose) {
    return;
  }
  if (!connection_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: connection is null, skipping event handling");
    return;
  }

  // Store connection info before it gets invalidated.
  const std::string connection_key =
      connection_->connectionInfoProvider().localAddress()->asString();
  const uint64_t connection_id = connection_->id();

  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, connection {} closed before handshake",
            connection_id, connection_key);

  // Do not call shutdown() here, as that may trigger cleanup during event processing. Notify the
  // parent instead, which treats the premature close as a terminal handshake failure.
  parent_.onConnectionDone(HandshakeOutcome::failure("Connection closed"), this, true);
}

absl::Status RCConnectionWrapper::connect(const std::string& src_tenant_id,
                                          const std::string& src_cluster_id,
                                          const std::string& src_node_id,
                                          std::optional<int64_t> initiation_time_ms) {
  src_tenant_id_ = src_tenant_id;
  src_cluster_id_ = src_cluster_id;
  src_node_id_ = src_node_id;

  // Register connection callbacks.
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);
  connection_->connect();

  ENVOY_LOG(debug,
            "RCConnectionWrapper: connection: {}, sending reverse connection creation "
            "request through HTTP",
            connection_->id());

  handshake_ = parent_.handshakeConfigurator()->createHandshake(*this);
  const absl::Status start_status = handshake_->start(initiation_time_ms);
  if (!start_status.ok()) {
    // The handshake already reported the terminal outcome; the caller treats the attempt as
    // terminal.
    return start_status;
  }

  // Arm the handshake response deadline so a peer that completes TCP and TLS but never answers
  // cannot hold the attempt open forever.
  handshake_timer_ = connection_->dispatcher().createTimer([this]() { onHandshakeTimeout(); });
  handshake_timer_->enableTimer(parent_.handshakeTimeout());

  return absl::OkStatus();
}

void RCConnectionWrapper::onHandshakeTimeout() {
  ENVOY_LOG(debug, "RCConnectionWrapper: connection {} handshake timed out",
            connection_ ? connection_->id() : 0);
  if (handshake_terminal_) {
    return;
  }
  handshake_terminal_ = true;
  ENVOY_LOG(trace, "handshake failed: {}", HandshakeFailureReason::timeout().getDetailedName());
  handshake_->cancel();

  auto* ext = getDownstreamExtension();
  if (ext) {
    ext->incrementHandshakeStats(cluster_name_, false,
                                 HandshakeFailureReason::timeout().getNameForStats());
  }
  parent_.onConnectionDone(HandshakeOutcome::failure(HandshakeFailureReason::timeout()), this,
                           false, std::nullopt);
}

void RCConnectionWrapper::disarmHandshakeTimer() {
  if (handshake_timer_ != nullptr) {
    handshake_timer_->disableTimer();
  }
}

void RCConnectionWrapper::onHandshakeSucceeded() {
  if (handshake_terminal_) {
    return;
  }
  disarmHandshakeTimer();

  ENVOY_LOG(debug, "handshake succeeded");

  // Track handshake success stats.
  auto* ext = getDownstreamExtension();
  if (ext) {
    ext->incrementHandshakeStats(cluster_name_, true, "");
  }
}

void RCConnectionWrapper::onHandshakeDone(HandshakeOutcome outcome,
                                          std::optional<std::chrono::milliseconds> retry_after) {
  if (handshake_terminal_) {
    return;
  }
  handshake_terminal_ = true;
  disarmHandshakeTimer();

  if (!outcome.accepted && outcome.failure_reason.has_value()) {
    ENVOY_LOG(trace, "handshake failed: {}", outcome.message);
    // Track handshake failure stats.
    auto* ext = getDownstreamExtension();
    if (ext) {
      ext->incrementHandshakeStats(cluster_name_, false, outcome.failure_reason->getNameForStats());
    }
  }

  parent_.onConnectionDone(outcome, this, false, retry_after);
}

ReverseTunnelInitiatorExtension* RCConnectionWrapper::getDownstreamExtension() const {
  return parent_.getDownstreamExtension();
}

Network::ClientConnection& RCConnectionWrapper::connection() { return *connection_; }

Upstream::HostDescriptionConstSharedPtr RCConnectionWrapper::host() { return host_; }

const std::string& RCConnectionWrapper::clusterName() { return cluster_name_; }

const std::string& RCConnectionWrapper::requestPath() { return parent_.requestPath(); }

bool RCConnectionWrapper::useHttpUpgrade() { return parent_.useHttpUpgrade(); }

const std::vector<envoy::config::core::v3::HeaderValueOption>&
RCConnectionWrapper::additionalHeaders() {
  return parent_.additionalHeaders();
}

HandshakeHeadersConstSharedPtr RCConnectionWrapper::effectiveHandshakeHeaders() {
  // Live formatters are preferred; the snapshot can predate extension init and is reused across
  // re-dials.
  ReverseTunnelInitiatorExtension* ext = getDownstreamExtension();
  if (ext != nullptr && ext->handshakeHeaders() != nullptr) {
    return ext->handshakeHeaders();
  }
  return parent_.handshakeHeaders();
}

Network::ClientConnectionPtr RCConnectionWrapper::releaseConnection() {
  if (!connection_) {
    return nullptr;
  }

  connection_->removeConnectionCallbacks(*this);
  // Disarm the deadline so it cannot fire on a connection that is no longer ours, then detach the
  // handshake without a terminal outcome; any residual it captured stays readable via
  // takeHandshakeResidual(). The handshake is absent when the connection is released pre-connect.
  disarmHandshakeTimer();
  if (handshake_ != nullptr) {
    handshake_->cancel();
  }
  return std::move(connection_);
}

Buffer::InstancePtr RCConnectionWrapper::takeHandshakeResidual() {
  if (!handshake_) {
    return nullptr;
  }
  return handshake_->takeResidual();
}

void RCConnectionWrapper::shutdown() {
  if (shutdown_called_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown already called, skipping");
    return;
  }
  shutdown_called_ = true;

  // Stop the handshake deadline and reads before tearing down the connection.
  handshake_timer_.reset();

  if (!connection_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: Connection already null, nothing to shutdown");
    return;
  }

  // The handshake is absent when the wrapper is torn down before connect() ran.
  if (handshake_ != nullptr) {
    handshake_->cancel();
  }

  // Get connection info for logging.
  uint64_t connection_id = connection_->id();
  Network::Connection::State state = connection_->state();
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutting down connection ID: {}, state: {}", connection_id,
            static_cast<int>(state));

  connection_->removeConnectionCallbacks(*this);

  // Close before deferred-delete so ConnectionImpl is not destroyed with an open socket.
  if (connection_->state() == Network::Connection::State::Open) {
    if (connection_->getSocket()) {
      connection_->getSocket()->ioHandle().resetFileEvents();
    }
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
  connection_->dispatcher().deferredDelete(std::move(connection_));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
