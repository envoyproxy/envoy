#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;

/**
 * Interface to allow for switching between new and old UpstreamRequest implementation.
 */
class RouterUpstreamRequest : public LinkedObject<RouterUpstreamRequest>,
                              public UpstreamToDownstream {
public:
  virtual void encodeUpstreamHeaders(bool end_stream) PURE;
  virtual void encodeUpstreamData(Buffer::Instance& data, bool end_stream) PURE;
  virtual void encodeUpstreamTrailers(const Http::RequestTrailerMap& trailers) PURE;
  virtual void encodeUpstreamMetadata(Http::MetadataMapPtr&& metadata_map_ptr) PURE;

  virtual void resetStream() PURE;
  virtual void setupPerTryTimeout() PURE;
  virtual void onPerTryTimeout() PURE;
  virtual void maybeEndDecode(bool end_stream) PURE;
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;
  virtual bool createPerTryTimeoutOnRequestComplete() PURE;
  virtual const StreamInfo::UpstreamTiming& upstreamTiming() PURE;
  virtual Upstream::HostDescriptionConstSharedPtr& upstreamHost() PURE;
  virtual bool outlierDetectionTimeoutRecorded() PURE;
  virtual void outlierDetectionTimeoutRecorded(bool recorded) PURE;
  virtual bool awaitingHeaders() PURE;
  virtual void recordTimeoutBudget(bool value) PURE;
  virtual void retried(bool value) PURE;
  virtual bool retried() PURE;
  virtual bool grpcRqSuccessDeferred() PURE;
  virtual void grpcRqSuccessDeferred(bool deferred) PURE;
  virtual void upstreamCanary(bool value) PURE;
  virtual bool upstreamCanary() PURE;
  virtual bool encodeComplete() const PURE;
};

// The base request for Upstream.
class UpstreamRequest : public Logger::Loggable<Logger::Id::router>,
                        public RouterUpstreamRequest,
                        public GenericConnectionPoolCallbacks {
public:
  UpstreamRequest(RouterFilterInterface& parent, std::unique_ptr<GenericConnPool>&& conn_pool);
  ~UpstreamRequest() override;

  // RouterUpstreamRequest
  void encodeUpstreamHeaders(bool end_stream) override;
  void encodeUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void encodeUpstreamTrailers(const Http::RequestTrailerMap& trailers) override;
  void encodeUpstreamMetadata(Http::MetadataMapPtr&& metadata_map_ptr) override;

  void resetStream() override;
  void setupPerTryTimeout() override;
  void onPerTryTimeout() override;
  void maybeEndDecode(bool end_stream) override;
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override;

  bool createPerTryTimeoutOnRequestComplete() override {
    return create_per_try_timeout_on_request_complete_;
  }
  const StreamInfo::UpstreamTiming& upstreamTiming() override { return upstream_timing_; }
  Upstream::HostDescriptionConstSharedPtr& upstreamHost() override { return upstream_host_; }
  void outlierDetectionTimeoutRecorded(bool recorded) override {
    outlier_detection_timeout_recorded_ = recorded;
  }
  bool outlierDetectionTimeoutRecorded() override { return outlier_detection_timeout_recorded_; }
  bool awaitingHeaders() override { return awaiting_headers_; }
  void recordTimeoutBudget(bool value) override { record_timeout_budget_ = value; }
  void retried(bool value) override { retried_ = value; }
  bool retried() override { return retried_; }
  bool grpcRqSuccessDeferred() override { return grpc_rq_success_deferred_; }
  void grpcRqSuccessDeferred(bool deferred) override { grpc_rq_success_deferred_ = deferred; }
  void upstreamCanary(bool value) override { upstream_canary_ = value; }
  bool upstreamCanary() override { return upstream_canary_; }
  bool encodeComplete() const override { return encode_complete_; }

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

  // UpstreamToDownstream (Http::ResponseDecoder)
  void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) override;
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  // UpstreamToDownstream (Http::StreamCallbacks)
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override { disableDataFromDownstreamForFlowControl(); }
  void onBelowWriteBufferLowWatermark() override { enableDataFromDownstreamForFlowControl(); }
  // UpstreamToDownstream
  const RouteEntry& routeEntry() const override;
  const Network::Connection& connection() const override;

  void disableDataFromDownstreamForFlowControl();
  void enableDataFromDownstreamForFlowControl();

  // GenericConnPool
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                   const StreamInfo::StreamInfo& info,
                   absl::optional<Http::Protocol> protocol) override;
  UpstreamToDownstream& upstreamToDownstream() override { return *this; }

  void clearRequestEncoder();
  void onStreamMaxDurationReached();

  struct DownstreamWatermarkManager : public Http::DownstreamWatermarkCallbacks {
    DownstreamWatermarkManager(UpstreamRequest& parent) : parent_(parent) {}

    // Http::DownstreamWatermarkCallbacks
    void onBelowWriteBufferLowWatermark() override;
    void onAboveWriteBufferHighWatermark() override;

    UpstreamRequest& parent_;
  };

  void readEnable();
  void encodeBodyAndTrailers();

  // Getters and setters
  RouterFilterInterface& parent() { return parent_; }

private:
  bool shouldSendEndStream() {
    // Only encode end stream if the full request has been received, the body
    // has been sent, and any trailers or metadata have also been sent.
    return encode_complete_ && !buffered_request_body_ && !encode_trailers_ &&
           downstream_metadata_map_vector_.empty();
  }

  RouterFilterInterface& parent_;
  std::unique_ptr<GenericConnPool> conn_pool_;
  bool grpc_rq_success_deferred_;
  Event::TimerPtr per_try_timeout_;
  std::unique_ptr<GenericUpstream> upstream_;
  absl::optional<Http::StreamResetReason> deferred_reset_reason_;
  Buffer::InstancePtr buffered_request_body_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  DownstreamWatermarkManager downstream_watermark_manager_{*this};
  Tracing::SpanPtr span_;
  StreamInfo::StreamInfoImpl stream_info_;
  StreamInfo::UpstreamTiming upstream_timing_;
  const MonotonicTime start_time_;
  // Copies of upstream headers/trailers. These are only set if upstream
  // access logging is configured.
  Http::ResponseHeaderMapPtr upstream_headers_;
  Http::ResponseTrailerMapPtr upstream_trailers_;
  Http::MetadataMapVector downstream_metadata_map_vector_;

  // Tracks the number of times the flow of data from downstream has been disabled.
  uint32_t downstream_data_disabled_{};
  bool calling_encode_headers_ : 1;
  bool upstream_canary_ : 1;
  bool decode_complete_ : 1;
  bool encode_complete_ : 1;
  bool encode_trailers_ : 1;
  bool retried_ : 1;
  bool awaiting_headers_ : 1;
  bool outlier_detection_timeout_recorded_ : 1;
  // Tracks whether we deferred a per try timeout because the downstream request
  // had not been completed yet.
  bool create_per_try_timeout_on_request_complete_ : 1;
  // True if the CONNECT headers have been sent but proxying payload is paused
  // waiting for response headers.
  bool paused_for_connect_ : 1;

  // Sentinel to indicate if timeout budget tracking is configured for the cluster,
  // and if so, if the per-try histogram should record a value.
  bool record_timeout_budget_ : 1;

  Event::TimerPtr max_stream_duration_timer_;
};

} // namespace Router
} // namespace Envoy
