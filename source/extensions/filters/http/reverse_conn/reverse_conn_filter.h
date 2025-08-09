#pragma once

#include "envoy/extensions/bootstrap/reverse_connection_handshake/v3/reverse_connection_handshake.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_handshake/v3/reverse_connection_handshake.pb.validate.h"
#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.h"
#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_initiator.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Http {
namespace Utility {
using QueryParams = std::map<std::string, std::string>;
std::string queryParamsToString(const QueryParams& query_params);
} // namespace Utility
} // namespace Http

namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

namespace ReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

using ClusterNodeStorage = absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;
using ClusterNodeStorageSharedPtr = std::shared_ptr<ClusterNodeStorage>;

using TenantClusterStorage = absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;
using TenantClusterStorageSharedPtr = std::shared_ptr<TenantClusterStorage>;

class ReverseConnFilterConfig {
public:
  ReverseConnFilterConfig(
      const envoy::extensions::filters::http::reverse_conn::v3::ReverseConn& config)
      : ping_interval_(getValidPingInterval(config)) {}

  std::chrono::seconds pingInterval() const { return ping_interval_; }

private:
  static std::chrono::seconds getValidPingInterval(
      const envoy::extensions::filters::http::reverse_conn::v3::ReverseConn& config) {
    if (config.has_ping_interval()) {
      uint32_t value = config.ping_interval().value();
      // If ping_interval is explicitly set to 0, use default value
      return value > 0 ? std::chrono::seconds(value) : std::chrono::seconds(2);
    }
    // If ping_interval is not set, use default value
    return std::chrono::seconds(2);
  }

  const std::chrono::seconds ping_interval_;
};

using ReverseConnFilterConfigSharedPtr = std::shared_ptr<ReverseConnFilterConfig>;
static const char CRLF[] = "\r\n";
static const char DOUBLE_CRLF[] = "\r\n\r\n";

/**
 * Reverse connection filter for HTTP handshake processing.
 * This filter handles HTTP requests for reverse tunnel handshakes.
 */
class ReverseConnFilter : Logger::Loggable<Logger::Id::filter>, public Http::StreamDecoderFilter {
  friend class ReverseConnFilterTest;

public:
  ReverseConnFilter(ReverseConnFilterConfigSharedPtr config);
  ~ReverseConnFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  static const std::string reverse_connections_path;
  static const std::string reverse_connections_request_path;
  static const std::string stats_path;
  static const std::string tenant_path;
  static const std::string node_id_param;
  static const std::string cluster_id_param;
  static const std::string tenant_id_param;
  static const std::string role_param;
  static const std::string rc_accepted_response;

private:
  void saveDownstreamConnection(Network::Connection& downstream_connection,
                                const std::string& node_id, const std::string& cluster_id);
  std::string getQueryParam(const std::string& key);
  // API to get reverse connection information for the local envoy.
  // The API accepts the following headers:
  // - role: The role of the local envoy; can be either initiator or acceptor.
  // - node_id: The node ID of the remote envoy.
  // - cluster_id: The cluster ID of the remote envoy.
  // For info about the established reverse connections with the local envoy
  // as initiator, the API expects the cluster ID or node ID of the remote envoy.
  // For info about the reverse connections accepted by the local envoy as responder,
  // the API expects the cluster ID of the remote envoy that initiated the connections.
  // In both the above cases, the API returns a JSON response in the format:
  // "{available_connections: <number of available connections for the node/cluster>}"
  // In the default case (a request param is not provided), the API returns a JSON
  // object with the full list of nodes/clusters with which reverse connections are present
  // in the format: {"accepted": ["cluster_1", "cluster_2"], "connected": ["cluster_3"]}.
  Http::FilterHeadersStatus getReverseConnectionInfo();

  // Handle reverse connection info for responder role (uses upstream socket manager)
  Http::FilterHeadersStatus handleResponderInfo(const std::string& remote_node,
                                                const std::string& remote_cluster);

  // Handle reverse connection info for initiator role (uses downstream socket interface)
  Http::FilterHeadersStatus handleInitiatorInfo(const std::string& remote_node,
                                                const std::string& remote_cluster);
  // API to accept a reverse connection request. The handler obtains the cluster, tenant, etc
  // from the query parameters from the request and calls the UpstreamSocketManager to cache
  // the socket.
  Http::FilterDataStatus acceptReverseConnection();

  // Gets the details of the remote cluster such as the node UUID, cluster UUID,
  // and tenant UUID from the protobuf payload and populates them in the corresponding
  // out parameters.
  void getClusterDetailsUsingProtobuf(std::string* node_uuid, std::string* cluster_uuid,
                                      std::string* tenant_uuid);

  bool matchRequestPath(const absl::string_view& request_path, const std::string& api_path);

  // Get the upstream socket manager from the thread-local registry
  ReverseConnection::UpstreamSocketManager* getUpstreamSocketManager() {
    auto* upstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    if (!upstream_interface) {
      ENVOY_LOG(debug, "Upstream reverse socket interface not found");
      return nullptr;
    }

    auto* upstream_socket_interface =
        dynamic_cast<const ReverseConnection::ReverseTunnelAcceptor*>(upstream_interface);
    if (!upstream_socket_interface) {
      ENVOY_LOG(error, "Failed to cast to ReverseTunnelAcceptor");
      return nullptr;
    }

    auto* tls_registry = upstream_socket_interface->getLocalRegistry();
    if (!tls_registry) {
      ENVOY_LOG(error, "Thread local registry not found for upstream socket interface");
      return nullptr;
    }

    return tls_registry->socketManager();
  }

  // Get the downstream socket interface (for initiator role)
  const ReverseConnection::ReverseTunnelInitiator* getDownstreamSocketInterface() {
    auto* downstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface");
    if (!downstream_interface) {
      ENVOY_LOG(debug, "Downstream reverse socket interface not found");
      return nullptr;
    }

    auto* downstream_socket_interface =
        dynamic_cast<const ReverseConnection::ReverseTunnelInitiator*>(downstream_interface);
    if (!downstream_socket_interface) {
      ENVOY_LOG(error, "Failed to cast to ReverseTunnelInitiator");
      return nullptr;
    }

    return downstream_socket_interface;
  }

  // Get the upstream socket interface extension for production cross-thread aggregation
  ReverseConnection::ReverseTunnelAcceptorExtension* getUpstreamSocketInterfaceExtension() {
    auto* upstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    if (!upstream_interface) {
      ENVOY_LOG(debug, "Upstream reverse socket interface not found");
      return nullptr;
    }

    auto* upstream_socket_interface =
        dynamic_cast<const ReverseConnection::ReverseTunnelAcceptor*>(upstream_interface);
    if (!upstream_socket_interface) {
      ENVOY_LOG(error, "Failed to cast to ReverseTunnelAcceptor");
      return nullptr;
    }

    // Get the extension which provides cross-thread aggregation capabilities
    return upstream_socket_interface->getExtension();
  }

  // Get the downstream socket interface extension for production cross-thread aggregation
  ReverseConnection::ReverseTunnelInitiatorExtension* getDownstreamSocketInterfaceExtension() {
    auto* downstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface");
    if (!downstream_interface) {
      ENVOY_LOG(debug, "Downstream reverse socket interface not found");
      return nullptr;
    }

    auto* downstream_socket_interface =
        dynamic_cast<const ReverseConnection::ReverseTunnelInitiator*>(downstream_interface);
    if (!downstream_socket_interface) {
      ENVOY_LOG(error, "Failed to cast to ReverseTunnelInitiator");
      return nullptr;
    }

    // Get the extension which provides cross-thread aggregation capabilities
    return downstream_socket_interface->getExtension();
  }

  // Determine the role of this envoy instance based on available socket interfaces
  std::string determineRole() {
    auto* upstream_manager = getUpstreamSocketManager();
    auto* downstream_interface = getDownstreamSocketInterface();

    if (upstream_manager && !downstream_interface) {
      return "responder"; // Cloud envoy - accepts reverse connections
    } else if (!upstream_manager && downstream_interface) {
      return "initiator"; // On-prem envoy - initiates reverse connections
    } else if (upstream_manager && downstream_interface) {
      return "both"; // Supports both roles
    } else {
      return "unknown"; // No reverse connection interfaces available
    }
  }

  const ReverseConnFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Network::ClientConnectionPtr connection_;

  Http::RequestHeaderMap* request_headers_;
  Http::Utility::QueryParamsMulti query_params_;

  // Cluster where outgoing RC request is being sent to
  std::string remote_cluster_id_;

  // True, if the request path indicate that is an accept request that is not
  // meant to initiate reverse connections.
  bool is_accept_request_;

  // Holds the body size parsed from the Content-length header. Will be used by
  // decodeData() to decide if it has to wait for more data before parsing the
  // bytes into a protobuf object.
  uint32_t expected_proto_size_;

  // Data collection buffer used to maintain all the bytes of the
  // serialised 'ReverseConnHandshakeArg' proto.
  Buffer::OwnedImpl accept_rev_conn_proto_;
};

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
