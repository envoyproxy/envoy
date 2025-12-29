#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
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
#include "source/server/generic_factory_context.h"

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
absl::StatusOr<std::shared_ptr<ReverseTunnelFilterConfig>> ReverseTunnelFilterConfig::create(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Server::Configuration::FactoryContext& context) {

  Formatter::FormatterConstSharedPtr node_id_formatter;
  Formatter::FormatterConstSharedPtr cluster_id_formatter;

  // Create formatters for validation if configured.
  if (proto_config.has_validation()) {
    Server::GenericFactoryContextImpl generic_context(context.serverFactoryContext(),
                                                      context.messageValidationVisitor());

    const auto& validation = proto_config.validation();

    // Create node_id formatter if configured.
    if (!validation.node_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString node_id_format_config;
      node_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.node_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          node_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse node_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      node_id_formatter = std::move(formatter_or_error.value());
    }

    // Create cluster_id formatter if configured.
    if (!validation.cluster_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString cluster_id_format_config;
      cluster_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.cluster_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          cluster_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse cluster_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      cluster_id_formatter = std::move(formatter_or_error.value());
    }
  }

  return std::shared_ptr<ReverseTunnelFilterConfig>(new ReverseTunnelFilterConfig(
      proto_config, std::move(node_id_formatter), std::move(cluster_id_formatter)));
}

ReverseTunnelFilterConfig::ReverseTunnelFilterConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Formatter::FormatterConstSharedPtr node_id_formatter,
    Formatter::FormatterConstSharedPtr cluster_id_formatter)
    : ping_interval_(proto_config.has_ping_interval()
                         ? std::chrono::milliseconds(
                               DurationUtil::durationToMilliseconds(proto_config.ping_interval()))
                         : std::chrono::milliseconds(2000)),
      auto_close_connections_(
          proto_config.auto_close_connections() ? proto_config.auto_close_connections() : false),
      request_path_(proto_config.request_path().empty() ? "/reverse_connections/request"
                                                        : proto_config.request_path()),
      request_method_string_([&proto_config]() -> std::string {
        envoy::config::core::v3::RequestMethod method = proto_config.request_method();
        if (method == envoy::config::core::v3::METHOD_UNSPECIFIED) {
          method = envoy::config::core::v3::GET;
        }
        return envoy::config::core::v3::RequestMethod_Name(method);
      }()),
      node_id_formatter_(std::move(node_id_formatter)),
      cluster_id_formatter_(std::move(cluster_id_formatter)),
      emit_dynamic_metadata_(proto_config.has_validation() &&
                             proto_config.validation().emit_dynamic_metadata()),
      dynamic_metadata_namespace_(
          proto_config.has_validation() &&
                  !proto_config.validation().dynamic_metadata_namespace().empty()
              ? proto_config.validation().dynamic_metadata_namespace()
              : "envoy.filters.network.reverse_tunnel") {}

bool ReverseTunnelFilterConfig::validateIdentifiers(
    absl::string_view node_id, absl::string_view cluster_id,
    const StreamInfo::StreamInfo& stream_info) const {

  // If no validation configured, pass validation.
  if (!node_id_formatter_ && !cluster_id_formatter_) {
    return true;
  }

  // Validate node_id if formatter is configured.
  if (node_id_formatter_) {
    const std::string expected_node_id = node_id_formatter_->format({}, stream_info);
    if (!expected_node_id.empty() && expected_node_id != node_id) {
      ENVOY_LOG(debug, "reverse_tunnel: node_id validation failed. Expected: '{}', Actual: '{}'",
                expected_node_id, node_id);
      return false;
    }
  }

  // Validate cluster_id if formatter is configured.
  if (cluster_id_formatter_) {
    const std::string expected_cluster_id = cluster_id_formatter_->format({}, stream_info);
    if (!expected_cluster_id.empty() && expected_cluster_id != cluster_id) {
      ENVOY_LOG(debug, "reverse_tunnel: cluster_id validation failed. Expected: '{}', Actual: '{}'",
                expected_cluster_id, cluster_id);
      return false;
    }
  }

  return true;
}

void ReverseTunnelFilterConfig::emitValidationMetadata(absl::string_view node_id,
                                                       absl::string_view cluster_id,
                                                       bool validation_passed,
                                                       StreamInfo::StreamInfo& stream_info) const {
  if (!emit_dynamic_metadata_) {
    return;
  }

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();

  // Emit actual identifiers.
  fields["node_id"].set_string_value(std::string(node_id));
  fields["cluster_id"].set_string_value(std::string(cluster_id));

  // Emit validation result.
  fields["validation_result"].set_string_value(validation_passed ? "allowed" : "denied");

  // Set dynamic metadata on the stream info.
  stream_info.setDynamicMetadata(dynamic_metadata_namespace_, metadata);

  ENVOY_LOG(trace,
            "reverse_tunnel: emitted dynamic metadata to namespace '{}': node_id={}, "
            "cluster_id={}, validation_result={}",
            dynamic_metadata_namespace_, node_id, cluster_id,
            validation_passed ? "allowed" : "denied");
}

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
    // Close connection on codec error.
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
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
  ENVOY_LOG(trace,
            "ReverseTunnelFilter::RequestDecoderImpl::processIfComplete: method: {}, path: {}",
            method, path);
  if (!absl::EqualsIgnoreCase(method, parent_.config_->requestMethod()) ||
      path != parent_.config_->requestPath()) {
    sendLocalReply(Http::Code::NotFound, "Not a reverse tunnel request", nullptr, absl::nullopt,
                   "reverse_tunnel_not_found");
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
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
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  const absl::string_view node_id = node_vals[0]->value().getStringView();
  const absl::string_view cluster_id = cluster_vals[0]->value().getStringView();
  const absl::string_view tenant_id = tenant_vals[0]->value().getStringView();

  // Validate node_id and cluster_id if validation is configured.
  auto& connection = parent_.read_callbacks_->connection();
  const bool validation_passed =
      parent_.config_->validateIdentifiers(node_id, cluster_id, connection.streamInfo());

  // Emit validation metadata if configured.
  parent_.config_->emitValidationMetadata(node_id, cluster_id, validation_passed,
                                          connection.streamInfo());

  if (!validation_passed) {
    parent_.stats_.validation_failed_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: validation failed for node '{}', cluster '{}'",
                   parent_.read_callbacks_->connection(), node_id, cluster_id);
    sendLocalReply(Http::Code::Forbidden, "Validation failed", nullptr, absl::nullopt,
                   "reverse_tunnel_validation_failed");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
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
  ENVOY_CONN_LOG(debug,
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
                                        std::move(wrapped_socket), ping_seconds);
    ENVOY_CONN_LOG(debug, "reverse_tunnel: successfully registered wrapped socket for reuse",
                   connection);
  }

  // Report the connection to the extension -> reporter.
  if (auto extension = socket_manager->getUpstreamExtension()) {
    extension->reportConnection(std::string(node_id), std::string(cluster_id),
                                std::string(tenant_id));
  }
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
