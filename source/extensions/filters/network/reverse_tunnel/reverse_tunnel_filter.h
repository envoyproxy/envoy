#pragma once

#include <functional>

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/http/codec.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"
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

  // HTTP/1 handshake codec stats, owned here so they outlive every per-connection codec.
  mutable Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
};

using ReverseTunnelFilterConfigSharedPtr = std::shared_ptr<ReverseTunnelFilterConfig>;

/**
 * Network filter that handles reverse tunnel connection acceptance/rejection.
 * This filter processes HTTP requests to a specific endpoint and uses
 * HTTP headers to receive required identifiers.
 *
 * The filter operates as a terminal filter when processing reverse tunnel requests,
 * meaning it stops the filter chain after processing and manages connection lifecycle.
 */
class ReverseTunnelFilter : public Network::ReadFilter,
                            public Http::ServerConnectionCallbacks,
                            public Logger::Loggable<Logger::Id::filter> {
public:
  ReverseTunnelFilter(ReverseTunnelFilterConfigSharedPtr config, Stats::Scope& stats_scope,
                      Server::OverloadManager& overload_manager);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created) override;
  void onGoAway(Http::GoAwayErrorCode) override {}

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
  bool prepareAcceptedConnection(absl::string_view node_id, absl::string_view cluster_id,
                                 absl::string_view tenant_id, int64_t initiation_time_ms,
                                 absl::string_view initiator_worker_id,
                                 absl::string_view initiator_connection_id);

  // Installs the bytes-sent callback that runs phase two once the acceptance response of
  // response_wire_bytes bytes has reached the wire.
  void installAcceptanceFlushCallback(uint64_t response_wire_bytes);

  // Phase two of acceptance. Registers the prepared socket, then detaches and closes the handshake
  // connection so the duplicated fd is the tunnel's single reader.
  void completeAcceptedConnection();

  ReverseTunnelFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};

  // HTTP/1 codec and wiring.
  Http::ServerConnectionPtr codec_;
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

  // Per-request decoder to buffer body and respond via encoder.
  class RequestDecoderImpl : public Http::RequestDecoder {
  public:
    RequestDecoderImpl(ReverseTunnelFilter& parent, Http::ResponseEncoder& encoder)
        : parent_(parent), encoder_(encoder),
          stream_info_(parent_.read_callbacks_->connection().streamInfo().timeSource(), nullptr,
                       StreamInfo::FilterState::LifeSpan::Connection) {}

    void decodeHeaders(Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::RequestTrailerMapPtr&&) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void sendLocalReply(Http::Code code, absl::string_view body,
                        const std::function<void(Http::ResponseHeaderMap& headers)>&,
                        const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) override;
    StreamInfo::StreamInfo& streamInfo() override;
    AccessLog::InstanceSharedPtrVector accessLogHandlers() override;
    Http::RequestDecoderHandlePtr getRequestDecoderHandle() override;

  private:
    void processIfComplete(bool end_stream);

    // Rejects a handshake request that carries a body, which reverse tunnel handshakes never do.
    void rejectBody();

    ReverseTunnelFilter& parent_;
    Http::ResponseEncoder& encoder_;
    Http::RequestHeaderMapSharedPtr headers_;
    bool complete_{false};
    StreamInfo::StreamInfoImpl stream_info_;

    // Liveness token for the weak request decoder handle. Reset when this decoder is destroyed so a
    // held handle reports the decoder as gone rather than dangling.
    const std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
  };

  std::unique_ptr<RequestDecoderImpl> active_decoder_;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
