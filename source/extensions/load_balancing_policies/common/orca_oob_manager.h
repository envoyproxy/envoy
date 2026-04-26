#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/callback_impl.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/response_decoder_impl_base.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {

#define ALL_ORCA_OOB_STATS(COUNTER, GAUGE)                                                         \
  COUNTER(reports_received)                                                                        \
  COUNTER(report_errors)                                                                           \
  COUNTER(stream_failures)                                                                         \
  COUNTER(stream_terminated)                                                                       \
  GAUGE(active_sessions, Accumulate)

struct OrcaOobStats {
  ALL_ORCA_OOB_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Cluster-level manager owning per-host ORCA OOB streams. Modeled on
 * source/extensions/health_checkers/grpc/health_checker_impl.h: per-host nested OobSession holds
 * a CodecClient and Http stream callbacks; the manager reacts to PrioritySet::addMemberUpdateCb
 * to add/remove sessions. All activity runs on the supplied dispatcher's thread; the only
 * cross-thread surface is OrcaHostLbPolicyData atomics (workers read; this manager writes via the
 * shared OrcaLoadReportHandler).
 */
class OrcaOobManager : protected Logger::Loggable<Logger::Id::upstream> {
public:
  OrcaOobManager(std::chrono::milliseconds reporting_period,
                 const Upstream::PrioritySet& priority_set, Event::Dispatcher& dispatcher,
                 Random::RandomGenerator& random, Stats::Scope& stats_scope,
                 OrcaLoadReportHandlerSharedPtr report_handler);
  virtual ~OrcaOobManager();

  // Iterate priority set, open a session per existing host, register member-update callback.
  // Init order constraint: caller must invoke OrcaWeightManager::initialize() FIRST so the
  // OrcaHostLbPolicyData attachment exists before sessions decode their first report.
  absl::Status initialize();

protected:
  // Test seam: subclass overrides to inject a CodecClientForTest.
  virtual Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;

  Event::Dispatcher& dispatcher_;
  Random::RandomGenerator& random_;

private:
  class OobSession : public Event::DeferredDeletable,
                     public Http::ResponseDecoderImplBase,
                     public Http::StreamCallbacks,
                     public Http::ConnectionCallbacks {
  public:
    OobSession(OrcaOobManager& parent, Upstream::HostConstSharedPtr host,
               std::chrono::milliseconds initial_delay);
    ~OobSession() override;

    void disarm();

    const Upstream::HostConstSharedPtr& host() const { return host_; }

    // Http::StreamDecoder
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override {}

    // Http::ResponseDecoder
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void dumpState(std::ostream&, int) const override {}

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Http::ConnectionCallbacks
    void onGoAway(Http::GoAwayErrorCode error_code) override;

  private:
    // Network::ConnectionCallbacks bridge.
    class ConnectionCallbackImpl : public Network::ConnectionCallbacks {
    public:
      ConnectionCallbackImpl(OobSession& parent) : parent_(parent) {}
      void onEvent(Network::ConnectionEvent event) override { parent_.onConnectionEvent(event); }
      void onAboveWriteBufferHighWatermark() override {}
      void onBelowWriteBufferLowWatermark() override {}

    private:
      OobSession& parent_;
    };

    void connectAndStream();
    void onConnectionEvent(Network::ConnectionEvent event);
    void handleTransientFailure(absl::string_view reason);
    void handleTerminal(Grpc::Status::GrpcStatus status, absl::string_view reason);
    // Order matters: close(Abort) raises LocalClose synchronously, which re-enters
    // onConnectionEvent. Nulling codec_client_ first makes that re-entry a no-op.
    void tearDownCodec();
    void onRpcComplete(Grpc::Status::GrpcStatus status, absl::string_view message, bool end_stream);
    void onReport(const xds::data::orca::v3::OrcaLoadReport& report);
    void resetState();
    std::string authority() const;

    OrcaOobManager& parent_;
    const Upstream::HostConstSharedPtr host_;
    ConnectionCallbackImpl connection_callback_impl_{*this};
    Event::TimerPtr attempt_timer_;
    Event::TimerPtr inactivity_timer_;
    Http::CodecClientPtr codec_client_;
    Http::RequestEncoder* request_encoder_{nullptr};
    Grpc::Decoder decoder_;
    BackOffStrategyPtr backoff_;
    // We initiated a stream reset; suppress the resulting onResetStream callback.
    bool expect_reset_{false};
    // Server sent GOAWAY(NoError); defer teardown until the next decoded message so any in-flight
    // reports are still delivered. If the server is silent after GOAWAY, the inactivity_timer_
    // catches the stalled session.
    bool received_no_error_goaway_{false};
  };

  using OobSessionPtr = std::unique_ptr<OobSession>;

  void onHostsAdded(const Upstream::HostVector& hosts);
  void onHostsRemoved(const Upstream::HostVector& hosts);
  void onSessionTerminated(OobSession* session);

  static OrcaOobStats generateOrcaOobStats(Stats::Scope& scope);

  const std::chrono::milliseconds reporting_period_;
  const Upstream::PrioritySet& priority_set_;
  OrcaLoadReportHandlerSharedPtr report_handler_;
  Envoy::Common::CallbackHandlePtr member_update_cb_;
  // node_hash_map for pointer/reference stability across rehash.
  absl::node_hash_map<Upstream::HostConstSharedPtr, OobSessionPtr> oob_sessions_;
  OrcaOobStats oob_stats_;
};

/**
 * Production OrcaOobManager that allocates a real HTTP/2 codec client.
 */
class ProdOrcaOobManager : public OrcaOobManager {
public:
  using OrcaOobManager::OrcaOobManager;

protected:
  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
