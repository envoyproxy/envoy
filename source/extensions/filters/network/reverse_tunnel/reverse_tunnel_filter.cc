#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/http1/http1_handshake_responder.h"
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

  // Select the responder handshake. The top-level framing fields configure the built-in HTTP/1
  // handshake and may not be mixed with an explicit handshake selection.
  const bool legacy_handshake_fields_set =
      !proto_config.request_path().empty() ||
      proto_config.request_method() != envoy::config::core::v3::METHOD_UNSPECIFIED ||
      proto_config.use_http_upgrade();
  Bootstrap::ReverseConnection::ResponderHandshakeConfiguratorSharedPtr handshake_configurator;
  if (proto_config.has_http1_handshake()) {
    if (legacy_handshake_fields_set) {
      return absl::InvalidArgumentError(
          "reverse_tunnel: http1_handshake cannot be set together with request_path, "
          "request_method, or use_http_upgrade");
    }
  } else if (proto_config.has_custom_handshake()) {
    if (legacy_handshake_fields_set) {
      return absl::InvalidArgumentError(
          "reverse_tunnel: custom_handshake cannot be set together with request_path, "
          "request_method, or use_http_upgrade");
    }
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Bootstrap::ReverseConnection::ResponderHandshakeConfiguratorFactory>(
        proto_config.custom_handshake().name());
    auto typed_config =
        Config::Utility::translateAnyToFactoryConfig(proto_config.custom_handshake().typed_config(),
                                                     context.messageValidationVisitor(), factory);
    auto custom_configurator = factory.createConfigurator(*typed_config, context);
    if (!custom_configurator.ok()) {
      return custom_configurator.status();
    }
    handshake_configurator = std::move(custom_configurator.value());
  }
  if (handshake_configurator == nullptr) {
    handshake_configurator =
        std::make_shared<Bootstrap::ReverseConnection::Http1HandshakeResponderConfigurator>();
  }

  return std::shared_ptr<ReverseTunnelFilterConfig>(new ReverseTunnelFilterConfig(
      proto_config, std::move(node_id_formatter), std::move(cluster_id_formatter),
      std::move(tenant_id_formatter), std::move(jwt_validator), std::move(handshake_configurator)));
}

namespace {
// The effective HTTP/1 framing comes from ``http1_handshake`` when set, otherwise from the
// top-level fields; mixing the two forms is rejected at config load.
std::string effectiveRequestPath(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config) {
  if (proto_config.has_http1_handshake() &&
      !proto_config.http1_handshake().request_path().empty()) {
    return proto_config.http1_handshake().request_path();
  }
  if (proto_config.request_path().empty()) {
    return std::string(::Envoy::Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
                           DEFAULT_REVERSE_TUNNEL_REQUEST_PATH);
  }
  return proto_config.request_path();
}

envoy::config::core::v3::RequestMethod effectiveRequestMethod(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config) {
  envoy::config::core::v3::RequestMethod method =
      proto_config.has_http1_handshake() ? proto_config.http1_handshake().request_method()
                                         : proto_config.request_method();
  if (method == envoy::config::core::v3::METHOD_UNSPECIFIED) {
    method = envoy::config::core::v3::GET;
  }
  return method;
}

bool effectiveUseHttpUpgrade(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config) {
  return proto_config.has_http1_handshake() ? proto_config.http1_handshake().use_http_upgrade()
                                            : proto_config.use_http_upgrade();
}
} // namespace

ReverseTunnelFilterConfig::ReverseTunnelFilterConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Formatter::FormatterConstSharedPtr node_id_formatter,
    Formatter::FormatterConstSharedPtr cluster_id_formatter,
    Formatter::FormatterConstSharedPtr tenant_id_formatter, JwtHandshakeValidatorPtr jwt_validator,
    Bootstrap::ReverseConnection::ResponderHandshakeConfiguratorSharedPtr handshake_configurator)
    : ping_interval_(proto_config.has_ping_interval()
                         ? std::chrono::milliseconds(
                               DurationUtil::durationToMilliseconds(proto_config.ping_interval()))
                         : std::chrono::milliseconds(2000)),
      handshake_timeout_(proto_config.has_handshake_timeout()
                             ? std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
                                   proto_config.handshake_timeout()))
                             : std::chrono::milliseconds(10000)),
      request_path_(effectiveRequestPath(proto_config)),
      request_method_string_(
          envoy::config::core::v3::RequestMethod_Name(effectiveRequestMethod(proto_config))),
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
      use_http_upgrade_(effectiveUseHttpUpgrade(proto_config)),
      skip_rebalancing_(proto_config.skip_rebalancing()),
      enable_connection_limit_(proto_config.enable_connection_limit()),
      jwt_validator_(std::move(jwt_validator)),
      handshake_configurator_(std::move(handshake_configurator)) {}

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
  fields["node_id"].set_string_value(node_id);
  fields["cluster_id"].set_string_value(cluster_id);
  fields["tenant_id"].set_string_value(tenant_id);

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
    : config_(std::move(config)),
      request_spec_{config_->requestMethod(), config_->requestPath(), config_->useHttpUpgrade()},
      stats_scope_(stats_scope), overload_manager_(overload_manager),
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
  const std::chrono::milliseconds handshake_timeout =
      config_->handshakeConfigurator()->handshakeTimeout();
  handshake_timer_->enableTimer(handshake_timeout == std::chrono::milliseconds{0}
                                    ? config_->handshakeTimeout()
                                    : handshake_timeout);
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
  return handshake_->onData(data);
}

void ReverseTunnelFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  handshake_ = config_->handshakeConfigurator()->createHandshake(*this);
}

Network::Connection& ReverseTunnelFilter::connection() { return read_callbacks_->connection(); }

const Bootstrap::ReverseConnection::HandshakeRequestSpec& ReverseTunnelFilter::requestSpec() {
  return request_spec_;
}

Stats::Scope& ReverseTunnelFilter::statsScope() { return stats_scope_; }

Server::OverloadManager& ReverseTunnelFilter::overloadManager() { return overload_manager_; }

void ReverseTunnelFilter::onFramingRejection(
    Bootstrap::ReverseConnection::HandshakeFramingRejection rejection) {
  switch (rejection) {
  case Bootstrap::ReverseConnection::HandshakeFramingRejection::ParseError:
    stats_.parse_error_.inc();
    return;
  case Bootstrap::ReverseConnection::HandshakeFramingRejection::BodyRejected:
    stats_.body_rejected_.inc();
    return;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void ReverseTunnelFilter::onTerminalRejection() { disarmHandshakeTimer(); }

Bootstrap::ReverseConnection::HandshakePolicyResult
ReverseTunnelFilter::evaluatePolicy(const Http::RequestHeaderMap& headers,
                                    absl::string_view node_id, absl::string_view cluster_id,
                                    absl::string_view tenant_id) {
  Network::Connection& conn = read_callbacks_->connection();

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
      stats_.parse_error_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: identifier contains reserved delimiter '{}' while tenant "
                     "isolation is enabled",
                     conn, delimiter);
      return {false, Http::Code::BadRequest,
              fmt::format("Reverse tunnel identifiers must not contain '{}' when tenant isolation "
                          "is enabled",
                          delimiter),
              "reverse_tunnel_invalid_identifier"};
    }
  }

  // Check for upstream cluster name header and validate if required.
  if (!config_->requiredClusterName().empty()) {
    const auto upstream_cluster_vals =
        headers.get(Bootstrap::ReverseConnection::reverseTunnelUpstreamClusterNameHeader());

    if (upstream_cluster_vals.empty()) {
      stats_.parse_error_.inc();
      ENVOY_CONN_LOG(
          debug, "reverse_tunnel: missing upstream cluster name header when enforcement is enabled",
          conn);
      return {false, Http::Code::BadRequest, "Missing upstream cluster name header",
              "reverse_tunnel_missing_cluster_name_header"};
    }

    const absl::string_view upstream_cluster_name =
        upstream_cluster_vals[0]->value().getStringView();
    if (upstream_cluster_name != config_->requiredClusterName()) {
      stats_.validation_failed_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upstream cluster name mismatch. Expected: '{}', Actual: '{}'",
                     conn, config_->requiredClusterName(), upstream_cluster_name);
      return {false, Http::Code::BadRequest, "Cluster name mismatch",
              "reverse_tunnel_cluster_mismatch"};
    }
  }

  // Authenticate the handshake's bearer token (if configured) before validation and before the
  // socket is registered, so a forged or expired token can never yield a usable reverse tunnel.
  // On success this publishes the verified claims as dynamic metadata for %DYNAMIC_METADATA%
  // binding in the validation block below.
  if (config_->jwtEnabled() && !config_->verifyHandshakeJwt(headers, conn.streamInfo())) {
    if (config_->jwtRequired()) {
      stats_.jwt_denied_.inc();
      ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed", conn);
      return {false, Http::Code::Unauthorized, "JWT authentication failed",
              "reverse_tunnel_jwt_denied"};
    }
    // In audit mode (allow_missing_or_failed) count what enforcement would have rejected but let
    // the handshake proceed. No claims were published, so any %DYNAMIC_METADATA% binding will not
    // match.
    stats_.jwt_would_deny_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed (audit mode, allowing)", conn);
  }

  // Validate node_id, cluster_id, and tenant_id if validation is configured.
  const ReverseTunnelValidationResult validation_result =
      config_->validateIdentifiers(node_id, cluster_id, tenant_id, headers, conn.streamInfo());

  // Emit validation metadata if configured.
  config_->emitValidationMetadata(node_id, cluster_id, tenant_id, validation_result,
                                  conn.streamInfo());

  if (validation_result != ReverseTunnelValidationResult::ValidationPassed) {
    if (validation_result == ReverseTunnelValidationResult::Rejected) {
      stats_.rejected_.inc();
    } else {
      stats_.validation_failed_.inc();
    }
    ENVOY_CONN_LOG(debug,
                   "reverse_tunnel: handshake denied for node '{}', cluster '{}', tenant '{}', "
                   "result: {}",
                   conn, node_id, cluster_id, tenant_id, toStringView(validation_result));
    return {
        false,
        validation_result == ReverseTunnelValidationResult::Rejected ? Http::Code::TooManyRequests
                                                                     : Http::Code::Forbidden,
        std::string(toStringView(validation_result)), std::string(toStringView(validation_result))};
  }

  return {true, Http::Code::OK, ""};
}

bool ReverseTunnelFilter::prepareAcceptedConnection(
    const Bootstrap::ReverseConnection::AcceptedTunnelInfo& info) {
  if (!prepareAcceptedConnectionInternal(info)) {
    stats_.registration_failed_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: cannot register tunnel, rejecting handshake",
                   read_callbacks_->connection());
    return false;
  }
  return true;
}

bool ReverseTunnelFilter::prepareAcceptedConnectionInternal(
    const Bootstrap::ReverseConnection::AcceptedTunnelInfo& info) {
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
  pending_node_id_ = info.node_id;
  pending_cluster_id_ = info.cluster_id;
  pending_tenant_id_ = info.tenant_id;
  pending_initiator_worker_id_ = info.initiator_worker_id;
  pending_initiator_connection_id_ = info.initiator_connection_id;
  pending_initiation_time_ms_ = info.initiation_time_ms;
  return true;
}

void ReverseTunnelFilter::onAcceptanceResponseEncoded(uint64_t response_wire_bytes) {
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
