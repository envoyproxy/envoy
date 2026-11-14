#pragma once

#include <memory>

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/handshake_responder.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/filters/network/reverse_tunnel/jwt_handshake_validator.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

inline const Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor* getAcceptor() {
  auto* base_interface =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (base_interface == nullptr) {
    return nullptr;
  }

  return dynamic_cast<const Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor*>(
      base_interface);
}

enum class ReverseTunnelValidationResult { ValidationPassed, ValidationFailed, Rejected };

inline absl::string_view toStringView(ReverseTunnelValidationResult result) {
  switch (result) {
  case ReverseTunnelValidationResult::ValidationPassed:
    return "validation_passed";
  case ReverseTunnelValidationResult::ValidationFailed:
    return "validation_failed";
  case ReverseTunnelValidationResult::Rejected:
    return "rejected";
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

/**
 * Configuration for the reverse tunnel network filter.
 */
class ReverseTunnelFilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  static absl::StatusOr<std::shared_ptr<ReverseTunnelFilterConfig>>
  create(const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
         Server::Configuration::FactoryContext& context,
         JwksFetcherFactory create_fetcher_fn = nullptr);

  ~ReverseTunnelFilterConfig();

  std::chrono::milliseconds pingInterval() const { return ping_interval_; }
  std::chrono::milliseconds handshakeTimeout() const { return handshake_timeout_; }
  const std::string& requestPath() const { return request_path_; }

  // Returns the shared HTTP/1 codec stats, allocated once on the owning scope. Owning the stats on
  // the config keeps them alive for the whole life of every per-connection handshake codec.
  Http::Http1::CodecStats& http1CodecStats(Stats::Scope& scope) const {
    return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, scope);
  }
  const std::string& requestMethod() const { return request_method_string_; }
  static constexpr absl::string_view tenantDelimiter() {
    return Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
        TENANT_SCOPE_DELIMITER;
  }

  // Returns true if validation is configured.
  bool hasValidation() const {
    return node_id_formatter_ != nullptr || cluster_id_formatter_ != nullptr ||
           tenant_id_formatter_ != nullptr;
  }

  // Returns true if accepting another reverse connection for this node/tenant stays within the
  // configured per-worker connection cap (or no cap is configured).
  bool validateConnectionLimit(absl::string_view node_id, absl::string_view tenant_id) const;

  // Validates connection limit then, if configured, node_id/cluster_id/tenant_id against expected
  // values. Returns ValidationPassed when under the cap and identifiers match (or no identity
  // validation is configured); Rejected when the per-node connection cap would be exceeded;
  // ValidationFailed when an identity check fails. The parsed handshake request headers are
  // passed so validation format strings can reference them via %REQ(...)%.
  ReverseTunnelValidationResult
  validateIdentifiers(absl::string_view node_id, absl::string_view cluster_id,
                      absl::string_view tenant_id, const Http::RequestHeaderMap& request_headers,
                      const StreamInfo::StreamInfo& stream_info) const;

  // Returns true if JWT handshake authentication is configured.
  bool jwtEnabled() const { return jwt_validator_ != nullptr; }

  // Returns true if a missing/invalid token must reject the handshake (i.e. not audit mode).
  bool jwtRequired() const { return jwt_validator_ != nullptr && jwt_validator_->required(); }

  // Verifies the handshake's bearer token. Must only be called when jwtEnabled() is true. See
  // JwtHandshakeValidator::verify.
  bool verifyHandshakeJwt(const Http::RequestHeaderMap& headers,
                          StreamInfo::StreamInfo& stream_info) const {
    ASSERT(jwt_validator_ != nullptr);
    return jwt_validator_->verify(headers, stream_info);
  }

  // Emits validation results as dynamic metadata if configured.
  void emitValidationMetadata(absl::string_view node_id, absl::string_view cluster_id,
                              absl::string_view tenant_id,
                              ReverseTunnelValidationResult validation_result,
                              StreamInfo::StreamInfo& stream_info) const;

  // Returns the required cluster name for validation.
  const std::string& requiredClusterName() const { return required_cluster_name_; }

  // Returns whether the handshake is negotiated as an HTTP/1.1 Upgrade exchange.
  bool useHttpUpgrade() const { return use_http_upgrade_; }

  // Returns whether worker-thread rebalancing should be skipped for accepted connections.
  bool skipRebalancing() const { return skip_rebalancing_; }

  // Returns the responder handshake factory; HTTP/1.1 is the only built-in handshake.
  const Bootstrap::ReverseConnection::ResponderHandshakeConfigurator&
  handshakeConfigurator() const {
    return *handshake_configurator_;
  }

private:
  ReverseTunnelFilterConfig(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
      Formatter::FormatterConstSharedPtr node_id_formatter,
      Formatter::FormatterConstSharedPtr cluster_id_formatter,
      Formatter::FormatterConstSharedPtr tenant_id_formatter,
      JwtHandshakeValidatorPtr jwt_validator);

  const std::chrono::milliseconds ping_interval_;
  const std::chrono::milliseconds handshake_timeout_;
  const std::string request_path_;
  const std::string request_method_string_;

  // Validation configuration.
  Formatter::FormatterConstSharedPtr node_id_formatter_;
  Formatter::FormatterConstSharedPtr cluster_id_formatter_;
  Formatter::FormatterConstSharedPtr tenant_id_formatter_;
  const bool emit_dynamic_metadata_{false};
  const std::string dynamic_metadata_namespace_;

  // Required cluster name for validation (empty means no validation).
  const std::string required_cluster_name_;

  // When true, expect `Connection: Upgrade` + `Upgrade: reverse-tunnel` and respond `101`.
  const bool use_http_upgrade_{false};

  const bool skip_rebalancing_{false};

  // Whether this filter enforces the per-node concurrent connection cap (owned by the upstream
  // socket interface bootstrap extension) before completing a reverse tunnel handshake.
  const bool enable_connection_limit_{false};

  // JWT handshake authentication (experimental). nullptr when `jwt_validator` is not set.
  const JwtHandshakeValidatorPtr jwt_validator_;

  // HTTP/1 handshake codec stats, owned here so they outlive every per-connection codec. Declared
  // before the factory, which holds a reference to it.
  mutable Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;

  std::shared_ptr<const Bootstrap::ReverseConnection::ResponderHandshakeConfigurator>
      handshake_configurator_;
};

using ReverseTunnelFilterConfigSharedPtr = std::shared_ptr<ReverseTunnelFilterConfig>;

/**
 * Network filter that handles reverse tunnel connection acceptance/rejection. The handshake
 * protocol (HTTP/1.1 by default) is delegated to a
 * Bootstrap::ReverseConnection::ResponderHandshake; this filter owns the policy checks, the stats,
 * and the two-phase socket acceptance.
 *
 * The filter operates as a terminal filter when processing reverse tunnel requests,
 * meaning it stops the filter chain after processing and manages connection lifecycle.
 */
class ReverseTunnelFilter : public Network::ReadFilter,
                            public Bootstrap::ReverseConnection::ResponderHandshakeCallbacks,
                            public Logger::Loggable<Logger::Id::filter> {
public:
  ReverseTunnelFilter(ReverseTunnelFilterConfigSharedPtr config, Stats::Scope& stats_scope,
                      Server::OverloadManager& overload_manager);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Bootstrap::ReverseConnection::ResponderHandshakeCallbacks
  Network::Connection& connection() override;
  const Bootstrap::ReverseConnection::HandshakeRequestSpec& requestSpec() override;
  Stats::Scope& statsScope() override;
  Server::OverloadManager& overloadManager() override;
  Bootstrap::ReverseConnection::HandshakePolicyResult
  evaluatePolicy(const Http::RequestHeaderMap& headers, absl::string_view node_id,
                 absl::string_view cluster_id, absl::string_view tenant_id) override;
  void
  onFramingRejection(Bootstrap::ReverseConnection::HandshakeFramingRejection rejection) override;
  void onTerminalRejection() override;
  bool
  prepareAcceptedConnection(const Bootstrap::ReverseConnection::AcceptedTunnelInfo& info) override;
  void onAcceptanceResponseEncoded(uint64_t response_wire_bytes) override;

private:
// Stats definition.
#define ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(COUNTER)                                                \
  COUNTER(parse_error)                                                                             \
  COUNTER(accepted)                                                                                \
  COUNTER(rejected)                                                                                \
  COUNTER(validation_failed)                                                                       \
  COUNTER(jwt_denied)                                                                              \
  COUNTER(jwt_would_deny)                                                                          \
  COUNTER(timeout)                                                                                 \
  COUNTER(body_rejected)                                                                           \
  COUNTER(registration_failed)

  struct ReverseTunnelStats {
    ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(GENERATE_COUNTER_STRUCT)
    static ReverseTunnelStats generateStats(const std::string& prefix, Stats::Scope& scope);
  };

  // Arms the handshake deadline timer so a peer that never completes the handshake, or never reads
  // the acceptance response, cannot hold the connection open forever.
  void armHandshakeTimer();

  // Closes the handshake on deadline, releasing any socket duplicated but not yet registered.
  void onHandshakeTimeout();

  // Disables the handshake deadline timer once the handshake reaches a terminal outcome.
  void disarmHandshakeTimer();

  // Phase one of acceptance. Resolves the socket manager and duplicates the fd before the response
  // is sent. Returns false if the tunnel cannot be registered, so the caller answers 503 without
  // leaving a duplicated fd behind. On success the duplicated socket and identifiers are held until
  // the acceptance response is flushed.
  bool
  prepareAcceptedConnectionInternal(const Bootstrap::ReverseConnection::AcceptedTunnelInfo& info);

  // Phase two of acceptance. Registers the prepared socket, then detaches and closes the handshake
  // connection so the duplicated fd is the tunnel's single reader.
  void completeAcceptedConnection();

  ReverseTunnelFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};

  // Framing spec of the configured handshake, served to the responder.
  Bootstrap::ReverseConnection::HandshakeRequestSpec request_spec_;

  Stats::Scope& stats_scope_;
  Server::OverloadManager& overload_manager_;

  // Stats counters.
  ReverseTunnelStats stats_;

  // Handshake deadline timer, spanning the first byte to registration.
  Event::TimerPtr handshake_timer_;

  // Socket duplicated in phase one and registered in phase two, held between the two phases along
  // with the identifiers needed to register it.
  Network::ConnectionSocketPtr pending_socket_;
  std::string pending_node_id_;
  std::string pending_cluster_id_;
  std::string pending_tenant_id_;
  std::string pending_initiator_worker_id_;
  std::string pending_initiator_connection_id_;
  int64_t pending_initiation_time_ms_{0};

  // The per-connection handshake, created once read callbacks are available.
  Bootstrap::ReverseConnection::ResponderHandshakePtr handshake_;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
