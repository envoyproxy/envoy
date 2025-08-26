#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

const std::string ReverseConnFilter::reverse_connections_path = "/reverse_connections";
const std::string ReverseConnFilter::reverse_connections_request_path =
    "/reverse_connections/request";
const std::string ReverseConnFilter::node_id_param = "node_id";
const std::string ReverseConnFilter::cluster_id_param = "cluster_id";
const std::string ReverseConnFilter::tenant_id_param = "tenant_id";
const std::string ReverseConnFilter::role_param = "role";
const std::string ReverseConnFilter::rc_accepted_response = "reverse connection accepted";

ReverseConnFilter::ReverseConnFilter(ReverseConnFilterConfigSharedPtr config)
    : config_(config), is_accept_request_(false), accept_rev_conn_proto_(Buffer::OwnedImpl()) {}

ReverseConnFilter::~ReverseConnFilter() {}

void ReverseConnFilter::onDestroy() {}

std::string ReverseConnFilter::getQueryParam(const std::string& key) {
  if (query_params_.data().empty()) {
    query_params_ = Http::Utility::QueryParamsMulti::parseQueryString(
        request_headers_->Path()->value().getStringView());
  }
  auto item = query_params_.getFirstValue(key);
  if (item.has_value()) {
    return item.value();
  } else {
    return "";
  }
}

void ReverseConnFilter::getClusterDetailsUsingProtobuf(std::string* node_uuid,
                                                       std::string* cluster_uuid,
                                                       std::string* tenant_uuid) {

  envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeArg arg;
  const std::string request_body = accept_rev_conn_proto_.toString();
  ENVOY_STREAM_LOG(debug, "Received protobuf request body length: {}", *decoder_callbacks_,
                   request_body.length());
  if (!arg.ParseFromString(request_body)) {
    ENVOY_STREAM_LOG(error, "Failed to parse protobuf from request body", *decoder_callbacks_);
    return;
  }
  ENVOY_STREAM_LOG(debug, "Successfully parsed protobuf: {}", *decoder_callbacks_,
                   arg.DebugString());
  ENVOY_STREAM_LOG(debug, "Extracted values - tenant='{}', cluster='{}', node='{}'",
                   *decoder_callbacks_, arg.tenant_uuid(), arg.cluster_uuid(), arg.node_uuid());

  if (node_uuid) {
    *node_uuid = arg.node_uuid();
  }
  if (cluster_uuid) {
    *cluster_uuid = arg.cluster_uuid();
  }
  if (tenant_uuid) {
    *tenant_uuid = arg.tenant_uuid();
  }
}

Http::FilterDataStatus ReverseConnFilter::acceptReverseConnection() {
  std::string node_uuid, cluster_uuid, tenant_uuid;

  envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeRet ret;
  getClusterDetailsUsingProtobuf(&node_uuid, &cluster_uuid, &tenant_uuid);
  if (node_uuid.empty()) {
    ret.set_status(envoy::extensions::bootstrap::reverse_connection_handshake::v3::
                       ReverseConnHandshakeRet::REJECTED);
    ret.set_status_message("Failed to parse request message or required fields missing");
    decoder_callbacks_->sendLocalReply(Http::Code::BadGateway, ret.SerializeAsString(), nullptr,
                                       absl::nullopt, "");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Network::Connection* connection =
      &const_cast<Network::Connection&>(*decoder_callbacks_->connection());
  Envoy::Ssl::ConnectionInfoConstSharedPtr ssl = connection->ssl();
  ENVOY_STREAM_LOG(
      info,
      "Received accept reverse connection request. tenant '{}', cluster '{}', node '{}' FD: {}",
      *decoder_callbacks_, tenant_uuid, cluster_uuid, node_uuid,
      connection->getSocket()->ioHandle().fdDoNotUse());

  if ((ssl != nullptr) && (ssl->peerCertificatePresented())) {
    absl::Span<const std::string> dnsSans = ssl->dnsSansPeerCertificate();
    for (const std::string& dns : dnsSans) {
      auto parts = StringUtil::splitToken(dns, "=");
      if (parts.size() == 2) {
        if (parts[0] == "tenantId") {
          tenant_uuid = std::string(parts[1]);
        } else if (parts[0] == "clusterId") {
          cluster_uuid = std::string(parts[1]);
        }
      }
    }
  }

  ENVOY_STREAM_LOG(info, "Accepting reverse connection", *decoder_callbacks_);
  ret.set_status(envoy::extensions::bootstrap::reverse_connection_handshake::v3::
                     ReverseConnHandshakeRet::ACCEPTED);
  ENVOY_STREAM_LOG(info, "return value", *decoder_callbacks_);

  // Create response with explicit Content-Length
  std::string response_body = ret.SerializeAsString();
  ENVOY_STREAM_LOG(info, "Response body length: {}, content: '{}'", *decoder_callbacks_,
                   response_body.length(), response_body);
  ENVOY_STREAM_LOG(info, "Protobuf debug string: '{}'", *decoder_callbacks_, ret.DebugString());

  decoder_callbacks_->sendLocalReply(
      Http::Code::OK, response_body,
      [&response_body](Http::ResponseHeaderMap& headers) {
        headers.setContentType("application/octet-stream");
        headers.setContentLength(response_body.length());
        headers.setConnection("close");
      },
      absl::nullopt, "");

  ENVOY_STREAM_LOG(info, "DEBUG: About to save connection with node_uuid='{}' cluster_uuid='{}'",
                   *decoder_callbacks_, node_uuid, cluster_uuid);
  saveDownstreamConnection(*connection, node_uuid, cluster_uuid);
  connection->setSocketReused(true);

  // Reset file events on the connection socket
  if (connection->getSocket()) {
    connection->getSocket()->ioHandle().resetFileEvents();
  }

  connection->close(Network::ConnectionCloseType::NoFlush, "accepted_reverse_conn");
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterHeadersStatus ReverseConnFilter::getReverseConnectionInfo() {
  // Determine role based on query param or auto-detect from available interfaces
  std::string role = getQueryParam(role_param);
  if (role.empty()) {
    role = determineRole();
    ENVOY_LOG(debug, "Auto-detected role: {}", role);
  }

  bool is_responder = (role == "responder" || role == "both");
  bool is_initiator = (role == "initiator" || role == "both");

  const std::string& remote_node = getQueryParam(node_id_param);
  const std::string& remote_cluster = getQueryParam(cluster_id_param);
  ENVOY_LOG(
      info,
      "Received reverse connection info request with role: {}, remote node: {}, remote cluster: {}",
      role, remote_node, remote_cluster);

  // Handle based on role
  if (is_responder) {
    return handleResponderInfo(remote_node, remote_cluster);
  } else if (is_initiator) {
    return handleInitiatorInfo(remote_node, remote_cluster);
  } else {
    ENVOY_LOG(error, "Unknown role: {}", role);
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "Unknown role", nullptr,
                                       absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }
}

Http::FilterHeadersStatus
ReverseConnFilter::handleResponderInfo(const std::string& remote_node,
                                       const std::string& remote_cluster) {
  ENVOY_LOG(debug,
            "ReverseConnFilter: Received reverse connection info request with remote_node: {} "
            "remote_cluster: {}",
            remote_node, remote_cluster);

  // Production-ready cross-thread aggregation
  auto* upstream_extension = getUpstreamSocketInterfaceExtension();
  if (upstream_extension == nullptr) {
    ENVOY_LOG(error, "No upstream extension available for stats collection");
    std::string response = R"({"accepted":[],"connected":[]})";
    decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // For specific node or cluster query
  if (!remote_node.empty() || !remote_cluster.empty()) {
    // Get connection count for specific remote node/cluster using stats
    auto stats_map = upstream_extension->getCrossWorkerStatMap();
    size_t num_connections = 0;

    if (!remote_node.empty()) {
      std::string node_stat_name = fmt::format("reverse_connections.nodes.{}", remote_node);
      // Search for the stat with scope prefix since getCrossWorkerStatMap returns full stat names
      for (const auto& [stat_name, value] : stats_map) {
        if (stat_name.find(node_stat_name) != std::string::npos) {
          num_connections = value;
          break;
        }
      }
    } else {
      std::string cluster_stat_name =
          fmt::format("reverse_connections.clusters.{}", remote_cluster);
      // Search for the stat with scope prefix since getCrossWorkerStatMap returns full stat names
      for (const auto& [stat_name, value] : stats_map) {
        if (stat_name.find(cluster_stat_name) != std::string::npos) {
          num_connections = value;
          break;
        }
      }
    }

    std::string response = fmt::format("{{\"available_connections\":{}}}", num_connections);
    ENVOY_LOG(info, "handleResponderInfo response for {}: {}",
              remote_node.empty() ? remote_cluster : remote_node, response);
    decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "ReverseConnFilter: Using upstream socket manager to get connection stats");

  auto [connected_nodes, accepted_connections] =
      upstream_extension->getConnectionStatsSync(std::chrono::milliseconds(1000));

  // Convert vectors to lists for JSON serialization
  std::list<std::string> accepted_connections_list(accepted_connections.begin(),
                                                   accepted_connections.end());
  std::list<std::string> connected_nodes_list(connected_nodes.begin(), connected_nodes.end());

  ENVOY_LOG(debug, "Stats aggregation completed: {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  // Create JSON response
  std::string response = fmt::format("{{\"accepted\":{},\"connected\":{}}}",
                                     Json::Factory::listAsJsonString(accepted_connections_list),
                                     Json::Factory::listAsJsonString(connected_nodes_list));

  ENVOY_LOG(info, "handleResponderInfo production stats-based response: {}", response);
  decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus
ReverseConnFilter::handleInitiatorInfo(const std::string& remote_node,
                                       const std::string& remote_cluster) {
  ENVOY_LOG(debug, "Getting reverse connection info for initiator role");

  // Check if downstream socket interface is available
  auto* downstream_interface = getDownstreamSocketInterface();
  if (downstream_interface == nullptr) {
    ENVOY_LOG(error, "Failed to get downstream socket interface for initiator role");
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Failed to get downstream socket interface", nullptr,
                                       absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Get the downstream socket interface extension to check established connections
  auto* downstream_extension = getDownstreamSocketInterfaceExtension();
  if (downstream_extension == nullptr) {
    ENVOY_LOG(error, "Failed to get downstream socket interface extension for initiator role");
    std::string response = R"({"accepted":[],"connected":[]})";
    decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // For specific node or cluster query
  if (!remote_node.empty() || !remote_cluster.empty()) {
    // Get connection count for specific remote node/cluster using stats
    // For initiator, stats format includes state suffix: reverse_connections.nodes.<node>.connected
    auto stats_map = downstream_extension->getCrossWorkerStatMap();
    size_t num_connections = 0;

    if (!remote_node.empty()) {
      std::string node_stat_name =
          fmt::format("reverse_connections.host.{}.connected", remote_node);
      // Search for the stat with scope prefix since getCrossWorkerStatMap returns full stat names
      for (const auto& [stat_name, value] : stats_map) {
        if (stat_name.find(node_stat_name) != std::string::npos) {
          num_connections = value;
          break;
        }
      }
    } else {
      std::string cluster_stat_name =
          fmt::format("reverse_connections.cluster.{}.connected", remote_cluster);
      // Search for the stat with scope prefix since getCrossWorkerStatMap returns full stat names
      for (const auto& [stat_name, value] : stats_map) {
        if (stat_name.find(cluster_stat_name) != std::string::npos) {
          num_connections = value;
          break;
        }
      }
    }

    std::string response = fmt::format("{{\"available_connections\":{}}}", num_connections);
    ENVOY_LOG(info, "handleInitiatorInfo response for {}: {}",
              remote_node.empty() ? remote_cluster : remote_node, response);
    decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "ReverseConnFilter: Using downstream socket manager to get connection stats");

  // Use the production stats-based approach with Envoy's proven stats system
  auto [connected_nodes, accepted_connections] =
      downstream_extension->getConnectionStatsSync(std::chrono::milliseconds(1000));

  // Convert vectors to lists for JSON serialization
  std::list<std::string> accepted_connections_list(accepted_connections.begin(),
                                                   accepted_connections.end());
  std::list<std::string> connected_nodes_list(connected_nodes.begin(), connected_nodes.end());

  ENVOY_LOG(debug, "Stats aggregation completed: {} connected nodes, {} accepted connections",
            connected_nodes.size(), accepted_connections.size());

  // Create production-ready JSON response for multi-tenant environment
  std::string response = fmt::format("{{\"accepted\":{},\"connected\":{}}}",
                                     Json::Factory::listAsJsonString(accepted_connections_list),
                                     Json::Factory::listAsJsonString(connected_nodes_list));

  ENVOY_LOG(info, "handleInitiatorInfo production stats-based response: {}", response);
  decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus ReverseConnFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                           bool) {
  // check that request path starts with "/reverse_connections"
  const absl::string_view request_path = request_headers.Path()->value().getStringView();
  const bool should_intercept_request =
      matchRequestPath(request_path, ReverseConnFilter::reverse_connections_path);

  if (!should_intercept_request) {
    ENVOY_STREAM_LOG(trace, "Not intercepting HTTP request for path ", *decoder_callbacks_,
                     request_path);
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_STREAM_LOG(trace, "Intercepting HTTP request for path ", *decoder_callbacks_, request_path);
  request_headers_ = &request_headers;

  const absl::string_view method = request_headers.Method()->value().getStringView();
  if (method == Http::Headers::get().MethodValues.Post) {
    is_accept_request_ =
        matchRequestPath(request_path, ReverseConnFilter::reverse_connections_request_path);
    if (is_accept_request_) {
      absl::string_view length =
          request_headers_->get(Http::Headers::get().ContentLength)[0]->value().getStringView();
      expected_proto_size_ = static_cast<uint32_t>(std::stoi(std::string(length)));
      ENVOY_STREAM_LOG(info, "Expecting a reverse connection accept request with {} bytes",
                       *decoder_callbacks_, length);
      return Http::FilterHeadersStatus::StopIteration;
    }
  } else if (method == Http::Headers::get().MethodValues.Get) {
    return getReverseConnectionInfo();
  }
  return Http::FilterHeadersStatus::Continue;
}

bool ReverseConnFilter::matchRequestPath(const absl::string_view& request_path,
                                         const std::string& api_path) {
  if (request_path.compare(0, api_path.size(), api_path) == 0) {
    return true;
  }
  return false;
}

void ReverseConnFilter::saveDownstreamConnection(Network::Connection& downstream_connection,
                                                 const std::string& node_id,
                                                 const std::string& cluster_id) {
  ENVOY_STREAM_LOG(debug, "Adding downstream connection socket to upstream socket manager",
                   *decoder_callbacks_);

  auto* socket_manager = getUpstreamSocketManager();
  if (!socket_manager) {
    ENVOY_STREAM_LOG(error, "Failed to get upstream socket manager", *decoder_callbacks_);
    return;
  }

  // Instead of moving the socket, duplicate the file descriptor
  const Network::ConnectionSocketPtr& original_socket = downstream_connection.getSocket();
  if (!original_socket || !original_socket->isOpen()) {
    ENVOY_STREAM_LOG(error, "Original socket is not available or not open", *decoder_callbacks_);
    return;
  }

  // Duplicate the file descriptor
  Network::IoHandlePtr duplicated_handle = original_socket->ioHandle().duplicate();
  if (!duplicated_handle || !duplicated_handle->isOpen()) {
    ENVOY_STREAM_LOG(error, "Failed to duplicate file descriptor", *decoder_callbacks_);
    return;
  }

  ENVOY_STREAM_LOG(debug,
                   "Successfully duplicated file descriptor: original_fd={}, duplicated_fd={}",
                   *decoder_callbacks_, original_socket->ioHandle().fdDoNotUse(),
                   duplicated_handle->fdDoNotUse());

  // Create a new socket with the duplicated handle
  Network::ConnectionSocketPtr duplicated_socket = std::make_unique<Network::ConnectionSocketImpl>(
      std::move(duplicated_handle), original_socket->connectionInfoProvider().localAddress(),
      original_socket->connectionInfoProvider().remoteAddress());

  // Reset file events on the duplicated socket to clear any inherited events
  duplicated_socket->ioHandle().resetFileEvents();

  // Add the duplicated socket to the manager
  socket_manager->addConnectionSocket(node_id, cluster_id, std::move(duplicated_socket),
                                      config_->pingInterval(), false /* rebalanced */);

  ENVOY_STREAM_LOG(debug,
                   "Successfully added duplicated socket to upstream socket manager. Original "
                   "connection remains functional.",
                   *decoder_callbacks_);
}

Http::FilterDataStatus ReverseConnFilter::decodeData(Buffer::Instance& data, bool) {
  if (is_accept_request_) {
    accept_rev_conn_proto_.move(data);
    if (expected_proto_size_ > 0 && accept_rev_conn_proto_.length() < expected_proto_size_) {
      ENVOY_STREAM_LOG(debug,
                       "Waiting for more data, expected_proto_size_={}, current_buffer_size={}",
                       *decoder_callbacks_, expected_proto_size_, accept_rev_conn_proto_.length());
      return Http::FilterDataStatus::StopIterationAndBuffer;
    } else {
      return acceptReverseConnection();
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus ReverseConnFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void ReverseConnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
