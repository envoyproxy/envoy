#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/handshake_initiator.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations.
class ReverseConnectionIOHandle;
class ReverseTunnelInitiatorExtension;

/**
 * Wrapper for reverse connections that manages the connection lifecycle and drives the configured
 * initiator handshake. Transport and teardown live here; protocol-specific handshake behavior
 * lives behind InitiatorHandshake.
 */
class RCConnectionWrapper : public Network::ConnectionCallbacks,
                            public Event::DeferredDeletable,
                            public Logger::Loggable<Logger::Id::main>,
                            public InitiatorHandshakeCallbacks {
public:
  /**
   * Constructor for RCConnectionWrapper.
   * @param parent reference to the parent ReverseConnectionIOHandle
   * @param connection the client connection to wrap
   * @param host the upstream host description
   * @param cluster_name the name of the cluster
   */
  RCConnectionWrapper(ReverseConnectionIOHandle& parent, Network::ClientConnectionPtr connection,
                      Upstream::HostDescriptionConstSharedPtr host,
                      const std::string& cluster_name);

  /**
   * Destructor for RCConnectionWrapper.
   * Performs defensive cleanup to prevent crashes during shutdown.
   */
  ~RCConnectionWrapper() override;

  // Network::ConnectionCallbacks overrides
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  /**
   * Initiate the reverse connection handshake.
   * @param src_tenant_id the tenant identifier
   * @param src_cluster_id the cluster identifier
   * @param src_node_id the node identifier
   * @param initiation_time_ms epoch millis to advertise as the tunnel's initiation time. When
   *        absent, the current system time is used.
   * @return ``absl::OkStatus()`` when the handshake request was dispatched, or an error status when
   *         it failed synchronously. On a synchronous failure onConnectionDone has already run, so
   *         the caller treats the attempt as terminal.
   */
  absl::Status connect(const std::string& src_tenant_id, const std::string& src_cluster_id,
                       const std::string& src_node_id,
                       std::optional<int64_t> initiation_time_ms = std::nullopt);

  /**
   * Release ownership of the connection.
   * @return the connection pointer (ownership transferred to caller)
   */
  Network::ClientConnectionPtr releaseConnection();

  /**
   * Perform graceful shutdown of the connection.
   */
  void shutdown();

  /**
   * Get the underlying connection.
   * @return pointer to the client connection
   */
  Network::ClientConnection* getConnection() { return connection_.get(); }

  /**
   * Get the host description.
   * @return shared pointer to the host description
   */
  Upstream::HostDescriptionConstSharedPtr getHost() { return host_; }

  /**
   * Release any bytes the responder coalesced with the handshake response. These are carried with
   * the tunnel so the accepted handle can replay them before reading the socket.
   * @return the residual buffer, or nullptr when nothing was coalesced.
   */
  Buffer::InstancePtr takeHandshakeResidual();

  // InitiatorHandshakeCallbacks overrides.
  Network::ClientConnection& connection() override;
  Upstream::HostDescriptionConstSharedPtr host() override;
  const std::string& clusterName() override;
  const std::string& srcTenantId() override { return src_tenant_id_; }
  const std::string& srcClusterId() override { return src_cluster_id_; }
  const std::string& srcNodeId() override { return src_node_id_; }
  const std::string& requestPath() override;
  bool useHttpUpgrade() override;
  const std::vector<envoy::config::core::v3::HeaderValueOption>& additionalHeaders() override;
  HandshakeHeadersConstSharedPtr effectiveHandshakeHeaders() override;
  void onHandshakeSucceeded() override;
  void onHandshakeDone(HandshakeOutcome outcome,
                       std::optional<std::chrono::milliseconds> retry_after) override;

private:
  // Fails the handshake when the response deadline expires.
  void onHandshakeTimeout();

  // Disables the handshake deadline without destroying the timer, which is unsafe from the timer's
  // own fire callback. The timer is freed with the wrapper.
  void disarmHandshakeTimer();

  // Get the downstream extension for accessing stats.
  ReverseTunnelInitiatorExtension* getDownstreamExtension() const;

  ReverseConnectionIOHandle& parent_;
  Network::ClientConnectionPtr connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
  std::string cluster_name_;
  std::string src_tenant_id_;
  std::string src_cluster_id_;
  std::string src_node_id_;
  bool shutdown_called_{false};
  // Set once the handshake reached a terminal outcome through this wrapper (the terminal
  // notification, or the deadline). Late handshake callbacks after this are dropped, so stats and
  // the parent notification are never double-reported.
  bool handshake_terminal_{false};

  // Deadline for receiving the handshake response. Disabled on success, failure, or shutdown.
  Event::TimerPtr handshake_timer_;

  // The protocol-specific handshake driving this connection. Created by the parent's factory.
  std::unique_ptr<InitiatorHandshake> handshake_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
