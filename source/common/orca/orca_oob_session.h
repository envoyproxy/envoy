#pragma once

#include <chrono>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/response_decoder_impl_base.h"

namespace Envoy {
namespace Orca {

/**
 * All OOB stats. @see stats_macros.h
 */
#define ALL_ORCA_OOB_STATS(COUNTER, GAUGE)                                                         \
  COUNTER(streams_started)                                                                         \
  COUNTER(streams_closed)                                                                          \
  COUNTER(streams_failed)                                                                          \
  COUNTER(reports_received)                                                                        \
  COUNTER(reports_failed)                                                                          \
  COUNTER(stream_open_failures)                                                                    \
  GAUGE(active_streams, NeverImport)

struct OrcaOobStats {
  ALL_ORCA_OOB_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Callback interface for OOB report consumers.
 */
class OrcaOobCallbacks {
public:
  virtual ~OrcaOobCallbacks() = default;
  virtual void onOrcaOobReport(const xds::data::orca::v3::OrcaLoadReport& report) PURE;
};

/**
 * Manages a single OOB gRPC stream to one upstream host. Opens an HTTP/2
 * connection via host->createConnection() and calls
 * xds.service.orca.v3.OpenRcaService/StreamCoreMetrics with a client-chosen
 * report_interval; server streams back OrcaLoadReport messages.
 *
 * ConnectionCallbacks are delegated via inner classes to avoid diamond
 * inheritance on watermark methods (pattern from GrpcActiveHealthCheckSession).
 *
 * stop() is safe from onOrcaOobReport(); destroying the session from that
 * callback is not -- the stack is inside the codec decode path.
 */
class OrcaOobSession : public Http::ResponseDecoderImplBase,
                       public Http::StreamCallbacks,
                       public Event::DeferredDeletable,
                       public Logger::Loggable<Logger::Id::upstream> {
public:
  OrcaOobSession(Upstream::HostSharedPtr host, Event::Dispatcher& dispatcher,
                 Random::RandomGenerator& random, std::chrono::milliseconds reporting_period,
                 OrcaOobCallbacks& callbacks, OrcaOobStats& stats);
  // stop() is called from the destructor and must remain non-virtual.
  ~OrcaOobSession() override;

  // Establish the gRPC stream. No-op if server_supports_oob_ is false.
  void start();
  // Tear down the stream and cancel any pending reconnect. Safe to call from
  // onOrcaOobReport(); synchronous session destruction from that callback is not.
  void stop();

protected:
  // Virtual for test injection of a mock CodecClient.
  virtual Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data);

private:
  void createClient();
  void startStream();
  // Idempotent; no-op if suppress_reconnect_ is set or timer already armed.
  void scheduleReconnect();
  // Clears per-stream state (active_streams_ gauge, encoder) without bumping
  // any failure counter. Idempotent.
  void clearStreamState();
  void softClose();              // Clean shutdown: streams_closed_++ then reconnect.
  void handleTransientFailure(); // Failure: streams_failed_++ then reconnect.
  void handlePermanentFailure();
  // Returns true (and handles the failure) if grpc_status is set and non-Ok:
  // Unimplemented latches permanent failure, any other non-Ok trips a
  // transient failure. Returns false if absent or Ok.
  bool handleIfGrpcFailure(absl::optional<Grpc::Status::GrpcStatus> grpc_status);
  // close(NoFlush) + deferredDelete of client_. Use from paths that run inside
  // a codec decode callback (sync reset would UAF the ActiveRequest). No-op if
  // client_ is null.
  void closeClientDeferred();

  // Http::ResponseDecoderImplBase
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void decodeMetadata(Http::MetadataMapPtr&&) override {}
  void dumpState(std::ostream&, int) const override {}

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason, absl::string_view) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Delegated handlers called from inner callback classes.
  void onEvent(Network::ConnectionEvent event);
  void onGoAway(Http::GoAwayErrorCode error_code);

  // Network::ConnectionCallbacks impl.
  class ConnectionCallbackImpl : public Network::ConnectionCallbacks {
  public:
    ConnectionCallbackImpl(OrcaOobSession& parent) : parent_(parent) {}
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

  private:
    OrcaOobSession& parent_;
  };

  // Http::ConnectionCallbacks impl.
  class HttpConnectionCallbackImpl : public Http::ConnectionCallbacks {
  public:
    HttpConnectionCallbackImpl(OrcaOobSession& parent) : parent_(parent) {}
    void onGoAway(Http::GoAwayErrorCode error_code) override { parent_.onGoAway(error_code); }

  private:
    OrcaOobSession& parent_;
  };

  void onOrcaLoadReportReceived(Buffer::InstancePtr&& message);

  // Consumer destroys the session on host removal; no detection here.
  Upstream::HostSharedPtr host_;
  Event::Dispatcher& dispatcher_;
  Random::RandomGenerator& random_;
  std::chrono::milliseconds reporting_period_;
  OrcaOobCallbacks& callbacks_;
  OrcaOobStats& stats_;

  Http::CodecClientPtr client_;
  Http::RequestEncoder* request_encoder_{};
  Grpc::Decoder grpc_decoder_;
  bool stream_closed_{true};

  ConnectionCallbackImpl connection_cb_{*this};
  HttpConnectionCallbackImpl http_connection_cb_{*this};

  Event::TimerPtr reconnect_timer_;
  BackOffStrategyPtr backoff_strategy_;

  // Latched false on permanent remote/config failure (stops retry loop).
  bool server_supports_oob_{true};

  // Guards against re-entrant scheduleReconnect() from close/reset callbacks.
  // Set by start() during stale-client teardown (cleared before createClient),
  // and by stop() (left true; next start() clears it).
  bool suppress_reconnect_{false};
};

} // namespace Orca
} // namespace Envoy
