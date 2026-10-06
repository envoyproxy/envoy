#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

namespace {

class RequestDecoderHandleImpl : public Http::RequestDecoderHandle {
public:
  RequestDecoderHandleImpl(std::weak_ptr<bool> valid, Http::RequestDecoder& decoder)
      : valid_(std::move(valid)), decoder_(decoder) {}
  OptRef<Http::RequestDecoder> get() override {
    if (valid_.expired()) {
      return {};
    }
    return decoder_;
  }

private:
  std::weak_ptr<bool> valid_;
  Http::RequestDecoder& decoder_;
};

} // namespace

// Stats helper implementation.
ReverseTunnelFilter::ReverseTunnelStats
ReverseTunnelFilter::ReverseTunnelStats::generateStats(const std::string& prefix,
                                                       Stats::Scope& scope) {
  return {ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

// ReverseTunnelFilterConfig implementation.
absl::StatusOr<std::shared_ptr<ReverseTunnelFilterConfig>> ReverseTunnelFilterConfig::create(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Server::Configuration::FactoryContext& context, JwksFetcherFactory create_fetcher_fn) {

  Formatter::FormatterConstSharedPtr node_id_formatter;
  Formatter::FormatterConstSharedPtr cluster_id_formatter;
  Formatter::FormatterConstSharedPtr tenant_id_formatter;

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

    // Create tenant_id formatter if configured.
    if (!validation.tenant_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString tenant_id_format_config;
      tenant_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.tenant_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          tenant_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse tenant_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      tenant_id_formatter = std::move(formatter_or_error.value());
    }
  }

  // Build the JWT handshake validator if it is configured. This runs here rather than in the
  // constructor so that a bad configuration fails at config load time.
  JwtHandshakeValidatorPtr jwt_validator;
  if (proto_config.has_jwt_validator()) {
    auto validator_or_error = JwtHandshakeValidator::create(proto_config.jwt_validator(), context,
                                                            std::move(create_fetcher_fn));
    if (!validator_or_error.ok()) {
      return validator_or_error.status();
    }
    jwt_validator = std::move(validator_or_error.value());
  }

  return std::shared_ptr<ReverseTunnelFilterConfig>(new ReverseTunnelFilterConfig(
      proto_config, std::move(node_id_formatter), std::move(cluster_id_formatter),
      std::move(tenant_id_formatter), std::move(jwt_validator)));
}

ReverseTunnelFilterConfig::ReverseTunnelFilterConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Formatter::FormatterConstSharedPtr node_id_formatter,
    Formatter::FormatterConstSharedPtr cluster_id_formatter,
    Formatter::FormatterConstSharedPtr tenant_id_formatter, JwtHandshakeValidatorPtr jwt_validator)
    : ping_interval_(proto_config.has_ping_interval()
                         ? std::chrono::milliseconds(
                               DurationUtil::durationToMilliseconds(proto_config.ping_interval()))
                         : std::chrono::milliseconds(2000)),
      handshake_timeout_(proto_config.has_handshake_timeout()
                             ? std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
                                   proto_config.handshake_timeout()))
                             : std::chrono::milliseconds(10000)),
      request_path_(
          proto_config.request_path().empty()
              ? std::string(::Envoy::Extensions::Bootstrap::ReverseConnection::
                                ReverseConnectionUtility::DEFAULT_REVERSE_TUNNEL_REQUEST_PATH)
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
      tenant_id_formatter_(std::move(tenant_id_formatter)),
      emit_dynamic_metadata_(proto_config.has_validation() &&
                             proto_config.validation().emit_dynamic_metadata()),
      dynamic_metadata_namespace_(
          proto_config.has_validation() &&
                  !proto_config.validation().dynamic_metadata_namespace().empty()
              ? proto_config.validation().dynamic_metadata_namespace()
              : "envoy.filters.network.reverse_tunnel"),
      required_cluster_name_(proto_config.required_cluster_name()),
      use_http_upgrade_(proto_config.use_http_upgrade()),
      skip_rebalancing_(proto_config.skip_rebalancing()),
      enable_connection_limit_(proto_config.enable_connection_limit()),
      jwt_validator_(std::move(jwt_validator)) {}

ReverseTunnelFilterConfig::~ReverseTunnelFilterConfig() = default;

bool ReverseTunnelFilterConfig::validateConnectionLimit(absl::string_view node_id,
                                                        absl::string_view tenant_id) const {
  if (!enable_connection_limit_) {
    return true;
  }

  if (auto socket_manager = Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::
          getThreadLocalSocketManager()) {
    return socket_manager->canAcceptConnection(node_id, tenant_id);
  }
  ENVOY_LOG(warn,
            "reverse_tunnel: no socket manager found with connection limit enabled, rejecting.");
  return false;
}

ReverseTunnelValidationResult ReverseTunnelFilterConfig::validateIdentifiers(
    absl::string_view node_id, absl::string_view cluster_id, absl::string_view tenant_id,
    const Http::RequestHeaderMap& request_headers,
    const StreamInfo::StreamInfo& stream_info) const {

  if (!validateConnectionLimit(node_id, tenant_id)) {
    ENVOY_LOG(debug, "reverse_tunnel: connection limit reached. node_id: {}, tenant_id: {}",
              node_id, tenant_id);
    return ReverseTunnelValidationResult::Rejected;
  }

  // If no validation configured, pass validation.
  if (!node_id_formatter_ && !cluster_id_formatter_ && !tenant_id_formatter_) {
    return ReverseTunnelValidationResult::ValidationPassed;
  }

  // Give the formatter the parsed handshake headers so validation strings can read them with
  // %REQ(...)%, and %DYNAMIC_METADATA(namespace:claim)% can pick up any JWT claims published
  // earlier in the handshake.
  const Formatter::Context context(&request_headers);

  // Each check fails closed: an empty render, or the formatter's "-" placeholder for an absent
  // value, means the configured binding could not be evaluated and must reject the handshake
  // rather than skip the check. This mirrors the consumer side, where
  // RevConCluster::LoadBalancer::chooseHost treats both values as underivable and returns
  // nullptr.
  auto binding_failed = [](const std::string& expected, absl::string_view actual) {
    return expected.empty() || expected == "-" || expected != actual;
  };

  // Validate node_id if formatter is configured.
  if (node_id_formatter_) {
    const std::string expected_node_id = node_id_formatter_->format(context, stream_info);
    if (binding_failed(expected_node_id, node_id)) {
      ENVOY_LOG(debug, "reverse_tunnel: node_id validation failed. Expected: '{}', Actual: '{}'",
                expected_node_id, node_id);
      return ReverseTunnelValidationResult::ValidationFailed;
    }
  }

  // Validate cluster_id if formatter is configured.
  if (cluster_id_formatter_) {
    const std::string expected_cluster_id = cluster_id_formatter_->format(context, stream_info);
    if (binding_failed(expected_cluster_id, cluster_id)) {
      ENVOY_LOG(debug, "reverse_tunnel: cluster_id validation failed. Expected: '{}', Actual: '{}'",
                expected_cluster_id, cluster_id);
      return ReverseTunnelValidationResult::ValidationFailed;
    }
  }

  // Validate tenant_id if formatter is configured.
  if (tenant_id_formatter_) {
    const std::string expected_tenant_id = tenant_id_formatter_->format(context, stream_info);
    if (binding_failed(expected_tenant_id, tenant_id)) {
      ENVOY_LOG(debug, "reverse_tunnel: tenant_id validation failed. Expected: '{}', Actual: '{}'",
                expected_tenant_id, tenant_id);
      return ReverseTunnelValidationResult::ValidationFailed;
    }
  }

  return ReverseTunnelValidationResult::ValidationPassed;
}

void ReverseTunnelFilterConfig::emitValidationMetadata(
    absl::string_view node_id, absl::string_view cluster_id, absl::string_view tenant_id,
    ReverseTunnelValidationResult validation_result, StreamInfo::StreamInfo& stream_info) const {
  if (!emit_dynamic_metadata_) {
    return;
  }

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();

  // Emit actual identifiers.
  fields["node_id"].set_string_value(std::string(node_id));
  fields["cluster_id"].set_string_value(std::string(cluster_id));
  fields["tenant_id"].set_string_value(std::string(tenant_id));

  // Emit validation result.
  fields["validation_result"].set_string_value(toStringView(validation_result));

  // Set dynamic metadata on the stream info.
  stream_info.setDynamicMetadata(dynamic_metadata_namespace_, metadata);

  ENVOY_LOG(trace,
            "reverse_tunnel: emitted dynamic metadata to namespace '{}': node_id={}, "
            "cluster_id={}, tenant_id={}, validation_result={}",
            dynamic_metadata_namespace_, node_id, cluster_id, tenant_id,
            toStringView(validation_result));
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
  armHandshakeTimer();
  return Network::FilterStatus::Continue;
}

void ReverseTunnelFilter::armHandshakeTimer() {
  if (handshake_timer_ != nullptr) {
    return;
  }
  handshake_timer_ =
      read_callbacks_->connection().dispatcher().createTimer([this]() { onHandshakeTimeout(); });
  handshake_timer_->enableTimer(config_->handshakeTimeout());
}

void ReverseTunnelFilter::onHandshakeTimeout() {
  ENVOY_CONN_LOG(debug, "reverse_tunnel: handshake timed out", read_callbacks_->connection());
  stats_.timeout_.inc();
  // Release a socket duplicated in phase one but not yet registered so its fd is not leaked.
  pending_socket_.reset();
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void ReverseTunnelFilter::disarmHandshakeTimer() {
  if (handshake_timer_ != nullptr) {
    handshake_timer_->disableTimer();
  }
}

Network::FilterStatus ReverseTunnelFilter::onData(Buffer::Instance& data, bool) {
  if (!codec_) {
    Http::Http1Settings http1_settings;
    auto& http1_stats = config_->http1CodecStats(stats_scope_);
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
  // Reverse tunnel handshakes carry no body. Reject a declared Content-Length up front; a chunked
  // body is caught by decodeData as its bytes arrive. `Content-Length: 0` from older initiators is
  // accepted.
  const absl::string_view content_length = headers_->getContentLengthValue();
  if (!content_length.empty() && content_length != "0") {
    rejectBody();
    return;
  }
  // For an Upgrade request, the HTTP/1 server codec calls decodeHeaders with
  // end_stream=false because it now considers the connection a tunnel awaiting
  // more bytes. The handshake has no body, so process the headers immediately
  // rather than waiting for an end-of-stream that never comes.
  if (end_stream || Http::Utility::isUpgrade(*headers_)) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeData(Buffer::Instance& data, bool end_stream) {
  if (complete_) {
    return;
  }
  // Reverse tunnel handshakes carry no body. Any payload byte is rejected rather than buffered.
  if (data.length() > 0) {
    rejectBody();
    return;
  }
  if (end_stream) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::rejectBody() {
  if (complete_) {
    return;
  }
  complete_ = true;
  parent_.stats_.body_rejected_.inc();
  ENVOY_CONN_LOG(debug, "reverse_tunnel: rejecting handshake request with a body",
                 parent_.read_callbacks_->connection());
  sendLocalReply(Http::Code::BadRequest, "Reverse tunnel handshake must not carry a body", nullptr,
                 std::nullopt, "reverse_tunnel_body_rejected");
  parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeTrailers(Http::RequestTrailerMapPtr&&) {
  processIfComplete(true);
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeMetadata(Http::MetadataMapPtr&&) {}

void ReverseTunnelFilter::RequestDecoderImpl::sendLocalReply(
    Http::Code code, absl::string_view body,
    const std::function<void(Http::ResponseHeaderMap& headers)>& modify_headers,
    const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
  // A local reply is always a terminal rejection, so the handshake deadline no longer applies.
  parent_.disarmHandshakeTimer();
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
  return std::make_unique<RequestDecoderHandleImpl>(still_alive_, *this);
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
    sendLocalReply(Http::Code::NotFound, "Not a reverse tunnel request", nullptr, std::nullopt,
                   "reverse_tunnel_not_found");
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // When upgrade negotiation is enabled, require the request to advertise the
  // `reverse-tunnel` upgrade. The HTTP/1 server codec already validates the
  // `Connection: Upgrade` paired token, so we only re-check the `Upgrade` value here.
  if (parent_.config_->useHttpUpgrade()) {
    const auto upgrade = headers_->getUpgradeValue();
    if (!absl::EqualsIgnoreCase(upgrade, Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                             REVERSE_TUNNEL_UPGRADE_PROTOCOL)) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upgrade negotiation enabled but Upgrade header missing or "
                     "unexpected (got '{}')",
                     parent_.read_callbacks_->connection(), upgrade);
      sendLocalReply(
          Http::Code::UpgradeRequired, "Upgrade: reverse-tunnel required",
          [](Http::ResponseHeaderMap& h) {
            h.setReferenceKey(Http::Headers::get().Upgrade,
                              Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                  REVERSE_TUNNEL_UPGRADE_PROTOCOL);
          },
          std::nullopt, "reverse_tunnel_upgrade_required");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
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
                   std::nullopt, "reverse_tunnel_missing_headers");
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  const absl::string_view node_id = node_vals[0]->value().getStringView();
  const absl::string_view cluster_id = cluster_vals[0]->value().getStringView();
  const absl::string_view tenant_id = tenant_vals[0]->value().getStringView();

  // Reject a present-but-empty tenant id the same way as a missing header. An empty tenant
  // silently disables tenant scoping when the socket is registered, since
  // maybeBuildTenantScopedIdentifier returns the bare identifier for an empty tenant, while
  // empty node and cluster ids are already rejected at registration by
  // UpstreamSocketManager::addConnectionSocket.
  if (tenant_id.empty()) {
    parent_.stats_.parse_error_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: empty tenant-id header value",
                   parent_.read_callbacks_->connection());
    sendLocalReply(Http::Code::BadRequest, "Empty tenant-id header value", nullptr, std::nullopt,
                   "reverse_tunnel_empty_tenant_id");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // Get tenant isolation setting from socket manager (configured at bootstrap level).
  bool tenant_isolation_enabled = false;
  if (auto* socket_manager = Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::
          getThreadLocalSocketManager()) {
    tenant_isolation_enabled = socket_manager->tenantIsolationEnabled();
  }

  if (tenant_isolation_enabled) {
    const absl::string_view delimiter = ReverseTunnelFilterConfig::tenantDelimiter();
    const auto contains_delimiter = [&](absl::string_view value) -> bool {
      return value.find(delimiter) != absl::string_view::npos;
    };
    if (contains_delimiter(node_id) || contains_delimiter(cluster_id) ||
        contains_delimiter(tenant_id)) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: identifier contains reserved delimiter '{}' while tenant "
                     "isolation is enabled",
                     parent_.read_callbacks_->connection(), delimiter);
      sendLocalReply(
          Http::Code::BadRequest,
          fmt::format("Reverse tunnel identifiers must not contain '{}' when tenant isolation is "
                      "enabled",
                      delimiter),
          nullptr, std::nullopt, "reverse_tunnel_invalid_identifier");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  // Check for upstream cluster name header and validate if required.
  if (!parent_.config_->requiredClusterName().empty()) {
    const auto upstream_cluster_vals = headers_->get(
        Extensions::Bootstrap::ReverseConnection::reverseTunnelUpstreamClusterNameHeader());

    if (upstream_cluster_vals.empty()) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(
          debug, "reverse_tunnel: missing upstream cluster name header when enforcement is enabled",
          parent_.read_callbacks_->connection());
      sendLocalReply(Http::Code::BadRequest, "Missing upstream cluster name header", nullptr,
                     std::nullopt, "reverse_tunnel_missing_cluster_name_header");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

    const absl::string_view upstream_cluster_name =
        upstream_cluster_vals[0]->value().getStringView();
    if (upstream_cluster_name != parent_.config_->requiredClusterName()) {
      parent_.stats_.validation_failed_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upstream cluster name mismatch. Expected: '{}', Actual: '{}'",
                     parent_.read_callbacks_->connection(), parent_.config_->requiredClusterName(),
                     upstream_cluster_name);
      sendLocalReply(Http::Code::BadRequest, "Cluster name mismatch", nullptr, std::nullopt,
                     "reverse_tunnel_cluster_mismatch");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  auto& connection = parent_.read_callbacks_->connection();

  // Authenticate the handshake's bearer token (if configured) before validation and before the
  // socket is registered, so a forged or expired token can never yield a usable reverse tunnel.
  // On success this publishes the verified claims as dynamic metadata for %DYNAMIC_METADATA%
  // binding in the validation block below.
  if (parent_.config_->jwtEnabled() &&
      !parent_.config_->verifyHandshakeJwt(*headers_, connection.streamInfo())) {
    if (parent_.config_->jwtRequired()) {
      parent_.stats_.jwt_denied_.inc();
      ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed", connection);
      sendLocalReply(Http::Code::Unauthorized, "JWT authentication failed", nullptr, std::nullopt,
                     "reverse_tunnel_jwt_denied");
      connection.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
    // In audit mode (allow_missing_or_failed) count what enforcement would have rejected but let
    // the handshake proceed. No claims were published, so any %DYNAMIC_METADATA% binding will not
    // match.
    parent_.stats_.jwt_would_deny_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed (audit mode, allowing)",
                   connection);
  }

  // Validate node_id, cluster_id, and tenant_id if validation is configured.
  const ReverseTunnelValidationResult validation_result = parent_.config_->validateIdentifiers(
      node_id, cluster_id, tenant_id, *headers_, connection.streamInfo());

  // Emit validation metadata if configured.
  parent_.config_->emitValidationMetadata(node_id, cluster_id, tenant_id, validation_result,
                                          connection.streamInfo());

  if (validation_result != ReverseTunnelValidationResult::ValidationPassed) {
    if (validation_result == ReverseTunnelValidationResult::Rejected) {
      parent_.stats_.rejected_.inc();
    } else {
      parent_.stats_.validation_failed_.inc();
    }
    ENVOY_CONN_LOG(debug,
                   "reverse_tunnel: handshake denied for node '{}', cluster '{}', tenant '{}', "
                   "result: {}",
                   parent_.read_callbacks_->connection(), node_id, cluster_id, tenant_id,
                   toStringView(validation_result));
    sendLocalReply(
        validation_result == ReverseTunnelValidationResult::Rejected ? Http::Code::TooManyRequests
                                                                     : Http::Code::Forbidden,
        toStringView(validation_result), nullptr, std::nullopt, toStringView(validation_result));
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // Parse the optional initiator metadata carried on the handshake. All three are optional so older
  // initiators that do not advertise them are handled gracefully (empty values).
  int64_t initiation_time_ms = 0;
  const auto initiation_time_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelInitiationTimeHeader());
  if (!initiation_time_vals.empty()) {
    if (!absl::SimpleAtoi(initiation_time_vals[0]->value().getStringView(), &initiation_time_ms)) {
      ENVOY_CONN_LOG(warn, "reverse_tunnel: failed to parse initiation-time header value '{}'",
                     parent_.read_callbacks_->connection(),
                     initiation_time_vals[0]->value().getStringView());
      initiation_time_ms = 0;
    }
  }
  const auto initiator_worker_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelWorkerIdHeader());
  const absl::string_view initiator_worker_id =
      initiator_worker_vals.empty() ? absl::string_view{}
                                    : initiator_worker_vals[0]->value().getStringView();
  const auto initiator_connection_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelConnectionIdHeader());
  const absl::string_view initiator_connection_id =
      initiator_connection_vals.empty() ? absl::string_view{}
                                        : initiator_connection_vals[0]->value().getStringView();

  // Phase one. Duplicate the socket and resolve the registration target before responding, so a
  // tunnel that cannot be registered is answered with 503 rather than a 200 over a dropped socket.
  if (!parent_.prepareAcceptedConnection(node_id, cluster_id, tenant_id, initiation_time_ms,
                                         initiator_worker_id, initiator_connection_id)) {
    parent_.stats_.registration_failed_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: cannot register tunnel, rejecting handshake",
                   parent_.read_callbacks_->connection());
    sendLocalReply(Http::Code::ServiceUnavailable, "Reverse tunnel registration unavailable",
                   nullptr, std::nullopt, "reverse_tunnel_registration_failed");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // Stop reading further bytes on the handshake connection so no later pipelined request is
  // dispatched while the socket is handed off.
  parent_.read_callbacks_->connection().readDisable(true);

  // Send the acceptance response. In upgrade mode this is `101 Switching Protocols`.
  auto resp_headers = Http::ResponseHeaderMapImpl::create();
  if (parent_.config_->useHttpUpgrade()) {
    resp_headers->setStatus(101);
    resp_headers->setReferenceKey(Http::Headers::get().Connection,
                                  Http::Headers::get().ConnectionValues.Upgrade);
    resp_headers->setReferenceKey(
        Http::Headers::get().Upgrade,
        Bootstrap::ReverseConnection::ReverseConnectionUtility::REVERSE_TUNNEL_UPGRADE_PROTOCOL);
  } else {
    resp_headers->setStatus(200);
  }
  encoder_.encodeHeaders(*resp_headers, true);

  // Phase two runs once the acceptance response has reached the wire, so the response is ordered
  // ahead of any first byte a consumer writes into the tunnel.
  const uint64_t response_wire_bytes = encoder_.getStream().bytesMeter()->wireBytesSent();
  parent_.installAcceptanceFlushCallback(response_wire_bytes);
}

bool ReverseTunnelFilter::prepareAcceptedConnection(absl::string_view node_id,
                                                    absl::string_view cluster_id,
                                                    absl::string_view tenant_id,
                                                    int64_t initiation_time_ms,
                                                    absl::string_view initiator_worker_id,
                                                    absl::string_view initiator_connection_id) {
  Network::Connection& connection = read_callbacks_->connection();

  // The worker-local socket manager owns the idle pool. Without it the tunnel cannot be registered.
  if (Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::getThreadLocalSocketManager() ==
      nullptr) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: socket manager not available", connection);
    return false;
  }

  const Network::ConnectionSocketPtr& socket = connection.getSocket();
  if (!socket || !socket->isOpen()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: original socket not available or not open", connection);
    return false;
  }

  // Duplicate the fd now so the tunnel survives the handshake connection being closed in phase two.
  Network::IoHandlePtr wrapped_handle = socket->ioHandle().duplicate();
  if (!wrapped_handle || !wrapped_handle->isOpen()) {
    ENVOY_CONN_LOG(error, "reverse_tunnel: failed to duplicate socket handle", connection);
    return false;
  }

  auto wrapped_socket = std::make_unique<Network::ConnectionSocketImpl>(
      std::move(wrapped_handle), socket->connectionInfoProvider().localAddress(),
      socket->connectionInfoProvider().remoteAddress());
  wrapped_socket->ioHandle().resetFileEvents();

  // Hold the duplicated socket and identifiers until the acceptance response is flushed.
  pending_socket_ = std::move(wrapped_socket);
  pending_node_id_ = std::string(node_id);
  pending_cluster_id_ = std::string(cluster_id);
  pending_tenant_id_ = std::string(tenant_id);
  pending_initiator_worker_id_ = std::string(initiator_worker_id);
  pending_initiator_connection_id_ = std::string(initiator_connection_id);
  pending_initiation_time_ms_ = initiation_time_ms;
  return true;
}

void ReverseTunnelFilter::installAcceptanceFlushCallback(uint64_t response_wire_bytes) {
  // The bytes-sent callback reports the byte count of each write event, not a running total, so
  // accumulate until the whole acceptance response has reached the wire before detaching the
  // tunnel. A response split across writes under backpressure would otherwise never satisfy the
  // threshold.
  read_callbacks_->connection().addBytesSentCallback(
      [this, response_wire_bytes, sent = uint64_t{0}](uint64_t bytes_sent) mutable -> bool {
        sent += bytes_sent;
        if (sent < response_wire_bytes) {
          return true;
        }
        completeAcceptedConnection();
        return false;
      });
}

void ReverseTunnelFilter::completeAcceptedConnection() {
  Network::Connection& connection = read_callbacks_->connection();

  // The handshake is complete, so stop the deadline timer.
  disarmHandshakeTimer();

  auto* socket_manager =
      Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::getThreadLocalSocketManager();
  if (socket_manager == nullptr || pending_socket_ == nullptr) {
    // The worker-local socket manager went away between the two phases. Drop the duplicated fd.
    ENVOY_CONN_LOG(debug, "reverse_tunnel: socket manager unavailable at registration", connection);
    stats_.registration_failed_.inc();
    pending_socket_.reset();
    Bootstrap::ReverseConnection::ReverseConnectionUtility::applySslQuietClose(connection);
    connection.close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  Bootstrap::ReverseConnection::ReverseConnectionUtility::applySslQuietClose(connection);

  // Convert ping interval to seconds as required by the manager API.
  const std::chrono::seconds ping_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(config_->pingInterval());

  // The socket manager derives any tenant-scoped internal keys itself, so lifecycle logging keeps
  // the original node, cluster, and tenant fields.
  const bool tenant_isolation_enabled = socket_manager->tenantIsolationEnabled();
  const std::string socket_node_id =
      tenant_isolation_enabled
          ? Bootstrap::ReverseConnection::ReverseConnectionUtility::buildTenantScopedIdentifier(
                pending_tenant_id_, pending_node_id_)
          : pending_node_id_;
  const std::string socket_cluster_id =
      tenant_isolation_enabled
          ? Bootstrap::ReverseConnection::ReverseConnectionUtility::buildTenantScopedIdentifier(
                pending_tenant_id_, pending_cluster_id_)
          : pending_cluster_id_;

  const int socket_fd = pending_socket_->ioHandle().fdDoNotUse();
  socket_manager->addConnectionSocket(
      pending_node_id_, pending_cluster_id_, std::move(pending_socket_), ping_seconds,
      /* rebalanced= */ config_->skipRebalancing(), pending_tenant_id_,
      pending_initiator_worker_id_, pending_initiator_connection_id_);
  stats_.accepted_.inc();
  ENVOY_CONN_LOG(debug, "reverse_tunnel: registered tunnel socket for node '{}' cluster '{}'",
                 connection, pending_node_id_, pending_cluster_id_);

  if (auto extension = socket_manager->getUpstreamExtension()) {
    extension->reportConnection(socket_node_id, socket_cluster_id, pending_tenant_id_,
                                pending_initiation_time_ms_, socket_fd);
  }

  // Detach the handshake connection so the duplicated fd is the tunnel's only reader.
  connection.close(Network::ConnectionCloseType::NoFlush);
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
