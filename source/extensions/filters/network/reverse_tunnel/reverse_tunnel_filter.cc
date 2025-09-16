#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Stats helper implementation.
ReverseTunnelFilter::ReverseTunnelStats
ReverseTunnelFilter::ReverseTunnelStats::generateStats(const std::string& prefix,
                                                       Stats::Scope& scope) {
  return {ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

// ReverseTunnelFilterConfig implementation.
ReverseTunnelFilterConfig::ReverseTunnelFilterConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config)
    : ping_interval_(proto_config.has_ping_interval()
                         ? std::chrono::milliseconds(
                               DurationUtil::durationToMilliseconds(proto_config.ping_interval()))
                         : std::chrono::milliseconds(2000)),
      auto_close_connections_(
          proto_config.auto_close_connections() ? proto_config.auto_close_connections() : false),
      request_path_(proto_config.request_path().empty() ? "/reverse_connections/request"
                                                        : proto_config.request_path()),
      request_method_(proto_config.request_method().empty() ? "GET"
                                                            : proto_config.request_method()),
      node_id_filter_state_key_(proto_config.has_validation_config()
                                    ? proto_config.validation_config().node_id_filter_state_key()
                                    : ""),
      cluster_id_filter_state_key_(
          proto_config.has_validation_config()
              ? proto_config.validation_config().cluster_id_filter_state_key()
              : ""),
      tenant_id_filter_state_key_(
          proto_config.has_validation_config()
              ? proto_config.validation_config().tenant_id_filter_state_key()
              : "") {}

// ReverseTunnelFilter implementation.
ReverseTunnelFilter::ReverseTunnelFilter(ReverseTunnelFilterConfigSharedPtr config,
                                         Stats::Scope& stats_scope,
                                         Server::OverloadManager& overload_manager)
    : config_(std::move(config)), stats_scope_(stats_scope), overload_manager_(overload_manager),
      stats_(ReverseTunnelStats::generateStats("reverse_tunnel.handshake.", stats_scope_)) {}

Network::FilterStatus ReverseTunnelFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "reverse_tunnel: new connection established",
                 read_callbacks_->connection());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ReverseTunnelFilter::onData(Buffer::Instance& data, bool) {
  if (!codec_) {
    Http::Http1Settings http1_settings;
    Http::Http1::CodecStats::AtomicPtr http1_stats_ptr;
    auto& http1_stats = Http::Http1::CodecStats::atomicGet(http1_stats_ptr, stats_scope_);
    codec_ = std::make_unique<Http::Http1::ServerConnectionImpl>(
        read_callbacks_->connection(), http1_stats, *this, http1_settings,
        Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        envoy::config::core::v3::HttpProtocolOptions::ALLOW, overload_manager_);
  }

  const Http::Status status = codec_->dispatch(data);
  if (!status.ok()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: codec dispatch error: {}", read_callbacks_->connection(),
                   status.message());
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::StopIteration;
}

void ReverseTunnelFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Http::RequestDecoder& ReverseTunnelFilter::newStream(Http::ResponseEncoder& response_encoder,
                                                     bool) {
  active_decoder_ = std::make_unique<RequestDecoderImpl>(*this, response_encoder);
  return *active_decoder_;
}

// Private methods.

// RequestDecoderImpl
void ReverseTunnelFilter::RequestDecoderImpl::decodeHeaders(
    Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) {
  headers_ = std::move(headers);
  if (end_stream) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeData(Buffer::Instance& data, bool end_stream) {
  body_.add(data);
  if (end_stream) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeTrailers(Http::RequestTrailerMapPtr&&) {
  processIfComplete(true);
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeMetadata(Http::MetadataMapPtr&&) {}

void ReverseTunnelFilter::RequestDecoderImpl::sendLocalReply(
    Http::Code code, absl::string_view body,
    const std::function<void(Http::ResponseHeaderMap& headers)>& modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(static_cast<uint64_t>(code));
  headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
  if (modify_headers) {
    modify_headers(*headers);
  }
  const bool end_stream = body.empty();
  encoder_.encodeHeaders(*headers, end_stream);
  if (!end_stream) {
    Buffer::OwnedImpl buf(body);
    encoder_.encodeData(buf, true);
  }
}

StreamInfo::StreamInfo& ReverseTunnelFilter::RequestDecoderImpl::streamInfo() {
  return stream_info_;
}

AccessLog::InstanceSharedPtrVector ReverseTunnelFilter::RequestDecoderImpl::accessLogHandlers() {
  return {};
}

Http::RequestDecoderHandlePtr ReverseTunnelFilter::RequestDecoderImpl::getRequestDecoderHandle() {
  return nullptr;
}

void ReverseTunnelFilter::RequestDecoderImpl::processIfComplete(bool end_stream) {
  if (!end_stream || complete_) {
    return;
  }
  complete_ = true;

  // Validate method/path.
  const absl::string_view method = headers_->getMethodValue();
  const absl::string_view path = headers_->getPathValue();
  ENVOY_LOG(debug,
            "ReverseTunnelFilter::RequestDecoderImpl::processIfComplete: method: {}, path: {}",
            method, path);
  if (!absl::EqualsIgnoreCase(method, parent_.config_->requestMethod()) ||
      path != parent_.config_->requestPath()) {
    sendLocalReply(Http::Code::NotFound, "Not a reverse tunnel request", nullptr, absl::nullopt,
                   "reverse_tunnel_not_found");
    return;
  }

  // Extract node/cluster/tenant identifiers from HTTP headers.
  const auto node_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelNodeIdHeader());
  const auto cluster_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelClusterIdHeader());
  const auto tenant_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelTenantIdHeader());

  if (node_vals.empty() || cluster_vals.empty() || tenant_vals.empty()) {
    parent_.stats_.parse_error_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: missing required headers (node/cluster/tenant)",
                   parent_.read_callbacks_->connection());
    sendLocalReply(Http::Code::BadRequest, "Missing required reverse tunnel headers", nullptr,
                   absl::nullopt, "reverse_tunnel_missing_headers");
    return;
  }

  const absl::string_view node_id = node_vals[0]->value().getStringView();
  const absl::string_view cluster_id = cluster_vals[0]->value().getStringView();
  const absl::string_view tenant_id = tenant_vals[0]->value().getStringView();

  // Validate request using filter state if validation keys are configured.
  if (!parent_.validateRequestUsingFilterState(node_id, cluster_id, tenant_id)) {
    parent_.stats_.validation_failed_.inc();
    parent_.stats_.rejected_.inc();
    sendLocalReply(Http::Code::Forbidden, "Request validation failed", nullptr, absl::nullopt,
                   "reverse_tunnel_validation_failed");
    return;
  }

  // Respond with 200 OK.
  auto resp_headers = Http::ResponseHeaderMapImpl::create();
  resp_headers->setStatus(200);
  encoder_.encodeHeaders(*resp_headers, true);

  parent_.processAcceptedConnection(node_id, cluster_id, tenant_id);
  parent_.stats_.accepted_.inc();

  // Close the connection if configured to do so after handling the request.
  if (parent_.config_->autoCloseConnections()) {
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ReverseTunnelFilter::processAcceptedConnection(absl::string_view node_id,
                                                    absl::string_view cluster_id,
                                                    absl::string_view tenant_id) {
  ENVOY_CONN_LOG(info,
                 "reverse_tunnel: connection accepted for node '{}' in cluster '{}' (tenant: '{}')",
                 read_callbacks_->connection(), node_id, cluster_id, tenant_id);

  Network::Connection& connection = read_callbacks_->connection();

  // Lookup the reverse tunnel acceptor socket interface to retrieve the TLS registry.
  // Note: This is a global lookup that should be thread-safe but may return nullptr
  // if the socket interface isn't registered or we're in a test environment.
  auto* base_interface =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (base_interface == nullptr) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: socket interface not registered, skipping socket reuse",
                   connection);
    return;
  }

  const auto* acceptor =
      dynamic_cast<const Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor*>(
          base_interface);
  if (acceptor == nullptr) {
    ENVOY_CONN_LOG(error, "reverse_tunnel: reverse tunnel socket interface not found", connection);
    return;
  }

  // The TLS registry access must be done on the same thread where it was created.
  // In integration tests, this might not always be the case.
  auto* tls_registry = acceptor->getLocalRegistry();
  if (tls_registry == nullptr) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: thread local registry not available on this thread",
                   connection);
    return;
  }

  auto* socket_manager = tls_registry->socketManager();
  if (socket_manager == nullptr) {
    ENVOY_CONN_LOG(error, "reverse_tunnel: socket manager not available", connection);
    return;
  }

  // Wrap the downstream socket with our custom IO handle to manage its lifecycle.
  const Network::ConnectionSocketPtr& socket = connection.getSocket();
  if (!socket || !socket->isOpen()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: original socket not available or not open",
                   read_callbacks_->connection());
    return;
  }

  // Duplicate the original socket's IO handle for reuse.
  Network::IoHandlePtr wrapped_handle = socket->ioHandle().duplicate();
  if (!wrapped_handle || !wrapped_handle->isOpen()) {
    ENVOY_CONN_LOG(error, "reverse_tunnel: failed to duplicate socket handle", connection);
    return;
  }

  // Build a new ConnectionSocket from the duplicated handle, preserving addressing info.
  auto wrapped_socket = std::make_unique<Network::ConnectionSocketImpl>(
      std::move(wrapped_handle), socket->connectionInfoProvider().localAddress(),
      socket->connectionInfoProvider().remoteAddress());

  // Reset file events on the new socket.
  wrapped_socket->ioHandle().resetFileEvents();

  // Convert ping interval to seconds as required by the manager API.
  const std::chrono::seconds ping_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(config_->pingInterval());

  // Register the wrapped socket for reuse under the provided identifiers.
  // Note: The socket manager is expected to be thread-safe.
  if (socket_manager != nullptr) {
    ENVOY_CONN_LOG(trace, "reverse_tunnel: registering wrapped socket for reuse", connection);
    socket_manager->addConnectionSocket(std::string(node_id), std::string(cluster_id),
                                        std::move(wrapped_socket), ping_seconds,
                                        /*rebalanced=*/false);
    ENVOY_CONN_LOG(debug, "reverse_tunnel: successfully registered wrapped socket for reuse",
                   connection);
  }
}

bool ReverseTunnelFilter::validateRequestUsingFilterState(absl::string_view node_uuid,
                                                          absl::string_view cluster_uuid,
                                                          absl::string_view tenant_uuid) {
  ENVOY_LOG(debug,
            "ReverseTunnelFilter::validateRequestUsingFilterState: called with node_uuid: {}, "
            "cluster_uuid: {}, tenant_uuid: {}",
            node_uuid, cluster_uuid, tenant_uuid);
  const Network::Connection& connection = read_callbacks_->connection();
  const StreamInfo::FilterState& filter_state = connection.streamInfo().filterState();

  // Validate node ID if key is configured.
  if (!config_->nodeIdFilterStateKey().empty()) {
    const StreamInfo::FilterState::Object* node_obj =
        filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(
            config_->nodeIdFilterStateKey());
    if (!node_obj) {
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: node ID validation failed. filter state key '{}' not found",
                     connection, config_->nodeIdFilterStateKey());
      return false;
    }

    // Try to get the value as a string.
    const auto* string_obj = dynamic_cast<const Envoy::Router::StringAccessorImpl*>(node_obj);
    if (!string_obj || string_obj->asString() != node_uuid) {
      ENVOY_CONN_LOG(debug, "reverse_tunnel: node ID validation failed. expected '{}', got '{}'",
                     connection, node_uuid, string_obj ? string_obj->asString() : "null");
      return false;
    }

    ENVOY_CONN_LOG(trace, "reverse_tunnel: node ID validation passed for '{}'", connection,
                   node_uuid);
  }

  // Validate cluster ID if key is configured.
  if (!config_->clusterIdFilterStateKey().empty()) {
    const StreamInfo::FilterState::Object* cluster_obj =
        filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(
            config_->clusterIdFilterStateKey());
    if (!cluster_obj) {
      ENVOY_CONN_LOG(
          debug, "reverse_tunnel: cluster ID validation failed. filter state key '{}' not found",
          connection, config_->clusterIdFilterStateKey());
      return false;
    }

    const auto* string_obj = dynamic_cast<const Envoy::Router::StringAccessorImpl*>(cluster_obj);
    if (!string_obj || string_obj->asString() != cluster_uuid) {
      ENVOY_CONN_LOG(debug, "reverse_tunnel: cluster ID validation failed. expected '{}', got '{}'",
                     connection, cluster_uuid, string_obj ? string_obj->asString() : "null");
      return false;
    }

    ENVOY_CONN_LOG(trace, "reverse_tunnel: cluster ID validation passed for '{}'", connection,
                   cluster_uuid);
  }

  // Validate tenant ID if key is configured.
  if (!config_->tenantIdFilterStateKey().empty()) {
    const StreamInfo::FilterState::Object* tenant_obj =
        filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(
            config_->tenantIdFilterStateKey());
    if (!tenant_obj) {
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: tenant ID validation failed. filter state key '{}' not found",
                     connection, config_->tenantIdFilterStateKey());
      return false;
    }

    const auto* string_obj = dynamic_cast<const Envoy::Router::StringAccessorImpl*>(tenant_obj);
    if (!string_obj || string_obj->asString() != tenant_uuid) {
      ENVOY_CONN_LOG(debug, "reverse_tunnel: tenant ID validation failed. expected '{}', got '{}'",
                     connection, tenant_uuid, string_obj ? string_obj->asString() : "null");
      return false;
    }

    ENVOY_CONN_LOG(trace, "reverse_tunnel: tenant ID validation passed for '{}'", connection,
                   tenant_uuid);
  }

  ENVOY_CONN_LOG(debug, "reverse_tunnel: all configured validations passed", connection);
  return true;
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
