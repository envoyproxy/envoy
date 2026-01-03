#pragma once

#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/http/codec.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

/**
 * Configuration for the reverse tunnel network filter.
 */
class ReverseTunnelFilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  static absl::StatusOr<std::shared_ptr<ReverseTunnelFilterConfig>>
  create(const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
         Server::Configuration::FactoryContext& context);

  std::chrono::milliseconds pingInterval() const { return ping_interval_; }
  bool autoCloseConnections() const { return auto_close_connections_; }
  const std::string& requestPath() const { return request_path_; }
  const std::string& requestMethod() const { return request_method_string_; }

  // Returns true if validation is configured.
  bool hasValidation() const {
    return node_id_formatter_ != nullptr || cluster_id_formatter_ != nullptr;
  }

  // Validates the extracted node_id and cluster_id against expected values.
  // Returns true if validation passes or no validation is configured.
  bool validateIdentifiers(absl::string_view node_id, absl::string_view cluster_id,
                           const StreamInfo::StreamInfo& stream_info) const;

  // Emits validation results as dynamic metadata if configured.
  void emitValidationMetadata(absl::string_view node_id, absl::string_view cluster_id,
                              bool validation_passed, StreamInfo::StreamInfo& stream_info) const;

  // Returns the required cluster name for validation.
  const std::string& requiredClusterName() const { return required_cluster_name_; }

private:
  ReverseTunnelFilterConfig(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
      Formatter::FormatterConstSharedPtr node_id_formatter,
      Formatter::FormatterConstSharedPtr cluster_id_formatter);

  const std::chrono::milliseconds ping_interval_;
  const bool auto_close_connections_;
  const std::string request_path_;
  const std::string request_method_string_;

  // Validation configuration.
  Formatter::FormatterConstSharedPtr node_id_formatter_;
  Formatter::FormatterConstSharedPtr cluster_id_formatter_;
  const bool emit_dynamic_metadata_{false};
  const std::string dynamic_metadata_namespace_;

  // Required cluster name for validation (empty means no validation).
  const std::string required_cluster_name_;
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
  COUNTER(validation_failed)

  struct ReverseTunnelStats {
    ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(GENERATE_COUNTER_STRUCT)
    static ReverseTunnelStats generateStats(const std::string& prefix, Stats::Scope& scope);
  };

  // Process reverse tunnel connection.
  void processAcceptedConnection(absl::string_view node_id, absl::string_view cluster_id,
                                 absl::string_view tenant_id);

  ReverseTunnelFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};

  // HTTP/1 codec and wiring.
  Http::ServerConnectionPtr codec_;
  Stats::Scope& stats_scope_;
  Server::OverloadManager& overload_manager_;

  // Stats counters.
  ReverseTunnelStats stats_;

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
                        const absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) override;
    StreamInfo::StreamInfo& streamInfo() override;
    AccessLog::InstanceSharedPtrVector accessLogHandlers() override;
    Http::RequestDecoderHandlePtr getRequestDecoderHandle() override;

  private:
    void processIfComplete(bool end_stream);

    ReverseTunnelFilter& parent_;
    Http::ResponseEncoder& encoder_;
    Http::RequestHeaderMapSharedPtr headers_;
    Buffer::OwnedImpl body_;
    bool complete_{false};
    StreamInfo::StreamInfoImpl stream_info_;
  };

  std::unique_ptr<RequestDecoderImpl> active_decoder_;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
