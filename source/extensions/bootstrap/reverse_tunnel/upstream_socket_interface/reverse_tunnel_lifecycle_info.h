#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/network/socket.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

using LifecycleLogMetadata = std::vector<std::pair<std::string, std::string>>;

struct ReverseTunnelLifecycleInfo {
  std::string node_id;
  std::string cluster_id;
  std::string tenant_id;
  Network::Address::InstanceConstSharedPtr local_address;
  Network::Address::InstanceConstSharedPtr remote_address;
  std::string worker;
  int fd{-1};
  bool handed_off_to_upstream{false};
  bool upstream_lifecycle_filter_attached{false};
  bool socket_dead_notified{false};
  bool close_log_emitted{false};
  std::string close_reason;
};

inline constexpr absl::string_view kAccessLogMetadataNamespace = "envoy.reverse_tunnel.lifecycle";

inline constexpr absl::string_view kFilterStateNodeId = "envoy.reverse_tunnel.node_id";
inline constexpr absl::string_view kFilterStateClusterId = "envoy.reverse_tunnel.cluster_id";
inline constexpr absl::string_view kFilterStateTenantId = "envoy.reverse_tunnel.tenant_id";
inline constexpr absl::string_view kFilterStateWorker = "envoy.reverse_tunnel.worker";
inline constexpr absl::string_view kFilterStateFd = "envoy.reverse_tunnel.fd";

inline constexpr absl::string_view kLifecycleEventTunnelSetup = "tunnel_setup";
inline constexpr absl::string_view kLifecycleEventIdlePingSent = "idle_ping_sent";
inline constexpr absl::string_view kLifecycleEventIdlePingAck = "idle_ping_ack";
inline constexpr absl::string_view kLifecycleEventIdlePingMiss = "idle_ping_miss";
inline constexpr absl::string_view kLifecycleEventIdlePingTimeout = "idle_ping_timeout";
inline constexpr absl::string_view kLifecycleEventSocketHandoff = "socket_handoff";
inline constexpr absl::string_view kLifecycleEventTunnelClosed = "tunnel_closed";
inline constexpr absl::string_view kLifecycleEventHttp2KeepaliveTimeout = "http2_keepalive_timeout";

inline constexpr absl::string_view kLifecycleSocketStateIdle = "idle";
inline constexpr absl::string_view kLifecycleSocketStateHandedOff = "handed_off";
inline constexpr absl::string_view kLifecycleSocketStateInUse = "in_use";

inline constexpr absl::string_view kLifecycleHandoffKindPoolToUpstream = "pool_to_upstream";

inline constexpr absl::string_view kLifecycleCloseReasonIdlePeerClose = "idle_peer_close";
inline constexpr absl::string_view kLifecycleCloseReasonIdleReadError = "idle_read_error";
inline constexpr absl::string_view kLifecycleCloseReasonIdlePingWriteFailure =
    "idle_ping_write_failure";
inline constexpr absl::string_view kLifecycleCloseReasonIdlePingTimeout = "idle_ping_timeout";
inline constexpr absl::string_view kLifecycleCloseReasonRemoteClose = "remote_close";
inline constexpr absl::string_view kLifecycleCloseReasonLocalClose = "local_close";
inline constexpr absl::string_view kLifecycleCloseReasonExplicitClose = "explicit_close";

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
