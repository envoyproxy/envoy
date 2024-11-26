#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"

#include "envoy/http/codes.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/json/json_loader.h"
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
    : config_(config), expects_proxy_protocol_(false), is_accept_request_(false),
      accept_rev_conn_proto_(Buffer::OwnedImpl()) {}

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

void ReverseConnFilter::getClusterDetailsUsingQueryParams(std::string* node_uuid,
                                                          std::string* cluster_uuid,
                                                          std::string* tenant_uuid) {
  if (node_uuid) {
    *node_uuid = getQueryParam(node_id_param);
  }
  if (cluster_uuid) {
    *cluster_uuid = getQueryParam(cluster_id_param);
  }
  if (tenant_uuid) {
    *tenant_uuid = getQueryParam(tenant_id_param);
  }
}

void ReverseConnFilter::getClusterDetailsUsingProtobuf(std::string* node_uuid,
                                                       std::string* cluster_uuid,
                                                       std::string* tenant_uuid) {

  envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeArg arg;
  if (!arg.ParseFromString(accept_rev_conn_proto_.toString())) {
    return;
  }
  ENVOY_STREAM_LOG(debug, "Received accept reverse connection request: {}", *decoder_callbacks_,
                   arg.DebugString());

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

  envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeRet ret;
  if (expects_proxy_protocol_) {
    // This is an older remote side and is sending us params using HTTP query
    // parameters.
    getClusterDetailsUsingQueryParams(&node_uuid, &cluster_uuid, &tenant_uuid);
    if (node_uuid.empty()) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadGateway, "node_id required", nullptr,
                                         absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  } else {
    getClusterDetailsUsingProtobuf(&node_uuid, &cluster_uuid, &tenant_uuid);
    if (node_uuid.empty()) {
      ret.set_status(
          envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeRet::REJECTED);
      ret.set_status_message("Failed to parse request message or required fields missing");
      decoder_callbacks_->sendLocalReply(Http::Code::BadGateway, ret.SerializeAsString(), nullptr,
                                         absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
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

  if (expects_proxy_protocol_) {
    decoder_callbacks_->sendLocalReply(Http::Code::OK, rc_accepted_response, nullptr, absl::nullopt,
                                       "");
  } else {
    ENVOY_STREAM_LOG(info, "Accepting reverse connection", *decoder_callbacks_);
    ret.set_status(
        envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeRet::ACCEPTED);
    ENVOY_STREAM_LOG(info, "return value", *decoder_callbacks_, ret.SerializeAsString());
    decoder_callbacks_->sendLocalReply(Http::Code::OK, ret.SerializeAsString(), nullptr,
                                       absl::nullopt, "");
  }
  connection->setActiveConnectionReused(true);
  connection->close(Network::ConnectionCloseType::NoFlush, "accepted_reverse_conn");
  saveDownstreamConnection(*connection, node_uuid, cluster_uuid);

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterHeadersStatus ReverseConnFilter::getReverseConnectionInfo() {
  bool is_responder = true;
  std::string role = getQueryParam(role_param);
  // If not set, service the request as a responder.
  if (role.empty()) {
    role = "responder";
  }
  if (role == "initiator") {
    is_responder = false;
  }
  const std::string& remote_node = getQueryParam(node_id_param);
  const std::string& remote_cluster = getQueryParam(cluster_id_param);
  ENVOY_LOG(
      info,
      "Received reverse connection info request with role: {}, remote node: {}, remote cluster: {}",
      role, remote_node, remote_cluster);
  size_t num_sockets = 0;
  bool send_all_rc_info = true;
  // With the local envoy as a responder, the API can be used to get the number
  // of reverse connections by remote node ID or remote cluster ID.
  if (is_responder && (!remote_node.empty() || !remote_cluster.empty())) {
    send_all_rc_info = false;
    if (!remote_node.empty()) {
      ENVOY_LOG(
          debug,
          "Getting number of reverse connections for remote node:{} with local envoy role: {}",
          remote_node, role);
      num_sockets = reverseConnectionHandler().getNumberOfSocketsByNode(remote_node);
    } else {
      ENVOY_LOG(
          debug,
          "Getting number of reverse connections for remote cluster: {} with local envoy role: {}",
          remote_cluster, role);
      num_sockets = reverseConnectionHandler().getNumberOfSocketsByCluster(remote_cluster);
    }
  }

  // With the local envoy as an initiator, the API can be used to get the number
  // of reverse connections by remote cluster ID.
  if (!is_responder && !remote_cluster.empty()) {
    send_all_rc_info = false;
    ENVOY_LOG(
        debug,
        "Getting number of reverse connections for remote cluster:{} with local envoy role: {}",
        remote_node, role);
    num_sockets = reverseConnectionManager().getNumberOfSockets(remote_cluster);
  }
  // Send the reverse connection count filtered by node or cluster ID.
  if (!send_all_rc_info) {
    std::string response = fmt::format("{{\"available_connections\":{}}}", num_sockets);
    try {
      Json::Factory::loadFromString(response);
    } catch (EnvoyException& e) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                         "failed to form valid json response", nullptr,
                                         absl::nullopt, "");
    }
    ENVOY_LOG(info, "Sending reverse connection info response: {}", response);
    decoder_callbacks_->sendLocalReply(Http::Code::OK, response, nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "Getting all reverse connection info with local envoy role: {}", role);
  // The default case: send the full node/cluster list.
  // Obtain the list of all remote nodes from which reverse
  // connections have been accepted by the local envoy acting as responder.
  std::list<std::string> accepted_rc_nodes;
  for (auto const& node : reverseConnectionHandler().getSocketCountMap()) {
    auto node_id = node.first;
    size_t rc_conn_count = node.second;
    if (rc_conn_count > 0) {
      accepted_rc_nodes.push_back(node_id);
    }
  }
  // Obtain the list of all remote clusters with which reverse
  // connections have been established with the local envoy acting as initiator.
  std::list<std::string> connected_rc_clusters;
  for (auto const& cluster : reverseConnectionManager().getSocketCountMap()) {
    auto cluster_id = cluster.first;
    size_t rc_conn_count = cluster.second;
    if (rc_conn_count > 0) {
      connected_rc_clusters.push_back(cluster_id);
    }
  }

  std::string response = fmt::format("{{\"accepted\":{},\"connected\":{}}}",
                                     Json::Factory::listAsJsonString(accepted_rc_nodes),
                                     Json::Factory::listAsJsonString(connected_rc_clusters));
  ENVOY_LOG(info, "getReverseConnectionInfo response: {}", response);
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
    if (getQueryParam("accept") == "true") {
      ENVOY_STREAM_LOG(info, "Accepting a reverse connection request that expects a proxy protocol",
                       *decoder_callbacks_);
      is_accept_request_ = true;
      expects_proxy_protocol_ = true;
      Http::FilterDataStatus ret_status = acceptReverseConnection();
      if (ret_status == Http::FilterDataStatus::StopIterationNoBuffer) {
        return Http::FilterHeadersStatus::StopIteration;
      }
      return Http::FilterHeadersStatus::Continue;
    } else {
      is_accept_request_ =
          matchRequestPath(request_path, ReverseConnFilter::reverse_connections_request_path);
    }
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
  ENVOY_STREAM_LOG(debug, "Adding downstream connection socket to connection socket pool",
                   *decoder_callbacks_);
  const Network::ConnectionSocketPtr& downstream_socket = downstream_connection.getSocket();
  downstream_socket->ioHandle().resetFileEvents();
  reverseConnectionHandler().addConnectionSocket(node_id, cluster_id, downstream_socket,
                                                 expects_proxy_protocol_, config_->pingInterval(),
                                                 false /* rebalanced */);
}

Http::FilterDataStatus ReverseConnFilter::decodeData(Buffer::Instance& data, bool) {
  if (is_accept_request_ && !expects_proxy_protocol_) {
    accept_rev_conn_proto_.move(data);
    if (accept_rev_conn_proto_.length() < expected_proto_size_) {
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
