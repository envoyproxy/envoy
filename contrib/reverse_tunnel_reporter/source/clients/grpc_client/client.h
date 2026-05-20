#pragma once

#include <array>
#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/grpc/async_client.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/grpc_client.pb.h"
#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/stream_reverse_tunnels.pb.h"
#include "contrib/reverse_tunnel_reporter/source/reverse_tunnel_event_types.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace GrpcDisconnectionReason {

#define ITEMS(X)                                                                                   \
  X(BUFFER_OVERFLOW, buffer_overflow)                                                              \
  X(MAX_RETRIES_EXCEEDED, max_retries_exceeded)                                                    \
  X(NACK_RECEIVED, nack_received)                                                                  \
  X(REMOTE_CLOSE, remote_close)                                                                    \
  X(STREAM_CREATION_FAILED, stream_creation_failed)

#define ENUM_DECLARE(name, str) name,
enum class DisconnectReason { ITEMS(ENUM_DECLARE) COUNT };
#undef ENUM_DECLARE

#define ENUM_STRING(name, str) #str,
constexpr std::array<absl::string_view, static_cast<size_t>(DisconnectReason::COUNT)>
    DisconnectReasonStrings = {ITEMS(ENUM_STRING)};
#undef ENUM_STRING

constexpr absl::string_view toString(DisconnectReason r) {
  return DisconnectReasonStrings[static_cast<size_t>(r)];
}

} // namespace GrpcDisconnectionReason

using GrpcConfigProto =
    envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::grpc_client::GrpcClientConfig;
using StreamTunnelsReq = envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::
    grpc_client::StreamReverseTunnelsRequest;
using StreamTunnelsResp = envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::
    grpc_client::StreamReverseTunnelsResponse;

/// Parsed and validated configuration from the GrpcClientConfig proto, with defaults applied.
struct GrpcClientConfig {
  std::string stat_prefix;
  std::string cluster;
  std::chrono::milliseconds send_interval;
  std::chrono::milliseconds connect_retry_interval;
  uint32_t max_retries;
  uint32_t max_buffer;

  explicit GrpcClientConfig(const GrpcConfigProto& config);
};

/// Bidirectional gRPC streaming client that reports reverse-tunnel connection
/// state to a remote ReverseTunnelReportingService.
///
/// Protocol:
///   - On connect (and every reconnect) the client does a full push of all
///     known connections obtained from the reporter.
///   - Between connects the client sends incremental diffs (new connections /
///     removals) on a periodic send timer.
///   - Each request carries an incrementing nonce. The server ACKs by echoing
///     the nonce; a NACK carries an error_detail. If too many nonces remain
///     unacked the client disconnects and reconnects.
///   - The server may adjust the send interval via report_interval in its ACK.
///
/// Lifecycle: constructed by GrpcClientFactory, initialized via
/// onServerInitialized() which creates the gRPC channel and opens the first
/// stream and starts the send cycle. Empty messages are also sent (considered as heartbeat).
class GrpcClient : public ReverseTunnelReporterClient,
                   public Logger::Loggable<Logger::Id::connection>,
                   public Grpc::AsyncStreamCallbacks<StreamTunnelsResp> {
public:
  GrpcClient(Server::Configuration::ServerFactoryContext& context, const GrpcConfigProto& config);

  // ReverseTunnelReporterClient overrides
  void onServerInitialized(ReverseTunnelReporterWithState* reporter) override;

  void receiveEvents(ReverseTunnelEvent::TunnelUpdates updates) override;

  // RawAsyncStreamCallbacks overrides
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}

  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}

  void onReceiveMessage(Grpc::ResponsePtr<StreamTunnelsResp>&& message) override;

  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

private:
  // Actions
  void connect();

  void disconnect();

  void send(bool full_push);

  // Helpers
  StreamTunnelsReq constructMessage(bool full_push);

  void setTimer(Event::TimerPtr& timer, const std::chrono::milliseconds& ms);

  // config
  Server::Configuration::ServerFactoryContext& context_;
  GrpcClientConfig config_;
  ReverseTunnelReporterWithState* reporter_{nullptr};

  // State management
  ReverseTunnelEvent::TunnelUpdates queued_updates_;
  int64_t nonce_current_{0}; // Monotonically increasing, bumped on every sendMessage.
  int64_t nonce_acked_{0};   // High watermark of server-acknowledged nonces.
  // Guards against processing events when onServerInitialized() failed (cluster not
  // found, client creation error). Without this the client silently queues to nowhere.
  bool initialized_{false};

  // grpc client and stream requirements
  Grpc::AsyncClient<StreamTunnelsReq, StreamTunnelsResp> async_client_;
  Grpc::AsyncStream<StreamTunnelsReq> stream_;
  const Protobuf::MethodDescriptor& service_method_;

  // timers
  Event::TimerPtr retry_timer_;
  Event::TimerPtr send_timer_;

  struct GrpcClientStats {
    GrpcClientStats(Server::Configuration::ServerFactoryContext& context,
                    const std::string& stat_prefix, const std::string& cluster_name)
        : context_{context}, stat_name_pool_(context.scope().symbolTable()),
          stat_prefix_(stat_name_pool_.add(stat_prefix)),

          disconnects_(stat_name_pool_.add("disconnects")),

          status_code_(stat_name_pool_.add("status_code")),
          disconnect_reason_(stat_name_pool_.add("disconnect_reason")),
          cluster_label_(stat_name_pool_.add("cluster")),
          cluster_value_(stat_name_pool_.add(cluster_name)),

          connection_attempts_counter_(
              getCounter(stat_name_pool_.add("connection_attempts"), getTags())),
          acks_received_counter_(getCounter(stat_name_pool_.add("acks_received"), getTags())),
          send_attempts_counter_(getCounter(stat_name_pool_.add("send_attempts"), getTags())),
          sent_accepted_cnt_counter_(
              getCounter(stat_name_pool_.add("sent_accepted_cnt"), getTags())),
          sent_removed_cnt_counter_(getCounter(stat_name_pool_.add("sent_removed_cnt"), getTags())),
          events_dropped_counter_(getCounter(stat_name_pool_.add("events_dropped"), getTags())),
          queued_updates_counter_(getCounter(stat_name_pool_.add("queued_events"), getTags())),
          out_of_order_acks_counter_(
              getCounter(stat_name_pool_.add("out_of_order_acks"), getTags())),

          send_interval_gauge_(getGauge(stat_name_pool_.add("send_interval"), getTags())),
          nonce_current_gauge_(getGauge(stat_name_pool_.add("nonce_current"), getTags())),
          nonce_acked_gauge_(getGauge(stat_name_pool_.add("nonce_acked"), getTags())) {}

    Stats::StatNameTagVector getTags() {
      return Stats::StatNameTagVector{
          {cluster_label_, cluster_value_},
      };
    }

    Stats::StatNameTagVector getTags(Grpc::Status::GrpcStatus status,
                                     GrpcDisconnectionReason::DisconnectReason reason) {
      Stats::StatName status_value = stat_name_pool_.add(std::to_string(status));
      Stats::StatName reason_value = stat_name_pool_.add(GrpcDisconnectionReason::toString(reason));

      return Stats::StatNameTagVector{
          {cluster_label_, cluster_value_},
          {status_code_, status_value},
          {disconnect_reason_, reason_value},
      };
    }

    Stats::Counter& getCounter(const Stats::StatName& name, Stats::StatNameTagVector&& tags) {
      return Stats::Utility::counterFromStatNames(context_.scope(), {stat_prefix_, name}, tags);
    }

    Stats::Gauge& getGauge(const Stats::StatName& name, Stats::StatNameTagVector&& tags) {
      return Stats::Utility::gaugeFromStatNames(context_.scope(), {stat_prefix_, name},
                                                Stats::Gauge::ImportMode::NeverImport, tags);
    }

    Server::Configuration::ServerFactoryContext& context_;
    Stats::StatNamePool stat_name_pool_;
    const Stats::StatName stat_prefix_;

    const Stats::StatName disconnects_;

    const Stats::StatName status_code_;
    const Stats::StatName disconnect_reason_;
    const Stats::StatName cluster_label_;
    const Stats::StatName cluster_value_;

    Stats::Counter& connection_attempts_counter_;
    Stats::Counter& acks_received_counter_;
    Stats::Counter& send_attempts_counter_;
    Stats::Counter& sent_accepted_cnt_counter_;
    Stats::Counter& sent_removed_cnt_counter_;
    Stats::Counter& events_dropped_counter_;
    Stats::Counter& queued_updates_counter_;
    Stats::Counter& out_of_order_acks_counter_;

    Stats::Gauge& send_interval_gauge_;
    Stats::Gauge& nonce_current_gauge_;
    Stats::Gauge& nonce_acked_gauge_;
  };

  GrpcClientStats stats_;

  friend class GrpcClientTest;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
