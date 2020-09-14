#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
#include "common/config/well_known_names.h"
#include "common/http/filter_manager.h"
#include "common/local_reply/local_reply.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;

struct UpstreamFilterFactory : public Http::FilterChainFactory {
  void createFilterChain(Http::FilterChainFactoryCallbacks&) override {}

  bool createUpgradeFilterChain(absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                Http::FilterChainFactoryCallbacks&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};

struct UpstreamRequestFilter : public Http::StreamDecoderFilter,
                               public GenericConnectionPoolCallbacks,
                               public Logger::Loggable<Logger::Id::router> {
  UpstreamRequestFilter(UpstreamRequest& parent, std::unique_ptr<GenericConnPool>&& conn_pool);
  ~UpstreamRequestFilter();

  void disableDataFromDownstreamForFlowControl();
  void enableDataFromDownstreamForFlowControl();

  // Http::StreamFilterBase
  void onDestroy() override;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    decoding_headers_ = true;
    encoding_headers_only_ = end_stream;
    conn_pool_->newStream(this);
    decoding_headers_ = false;

    if (!await_stream_) {
      return Http::FilterHeadersStatus::Continue;
    }

    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // GenericConnPool
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                   const StreamInfo::StreamInfo& info) override;
  UpstreamToDownstream& upstreamToDownstream() override { return active_request_; }

  struct ActiveUpstreamRequest : public UpstreamToDownstream {
    explicit ActiveUpstreamRequest(UpstreamRequestFilter& parent) : parent_(parent) {}

    // UpstreamToDownstream (Http::ResponseDecoder)
    void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) override;
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    // UpstreamToDownstream (Http::StreamCallbacks)
    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;
    void onAboveWriteBufferHighWatermark() override {
      parent_.disableDataFromDownstreamForFlowControl();
    }
    void onBelowWriteBufferLowWatermark() override {
      parent_.enableDataFromDownstreamForFlowControl();
    }
    // UpstreamToDownstream
    const RouteEntry& routeEntry() const override;
    const Network::Connection& connection() const override;

    // Http::StreamDecoder
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

    UpstreamRequestFilter& parent_;
  };

  void maybeEndDecode(bool end_stream);
  void clearRequestEncoder();

private:
  bool shouldSendEndStream();

  UpstreamRequest& parent_;
  struct DownstreamWatermarkManager : public Http::DownstreamWatermarkCallbacks {
    DownstreamWatermarkManager(UpstreamRequestFilter& parent) : parent_(parent) {}

    // Http::DownstreamWatermarkCallbacks
    void onBelowWriteBufferLowWatermark() override;
    void onAboveWriteBufferHighWatermark() override;

    UpstreamRequestFilter& parent_;
  };

  std::unique_ptr<GenericConnPool> conn_pool_;
  absl::optional<Http::StreamResetReason> deferred_reset_reason_;
  Buffer::WatermarkBufferPtr buffered_request_body_;
  DownstreamWatermarkManager downstream_watermark_manager_{*this};
  const MonotonicTime start_time_;

  // True if the CONNECT headers have been sent but proxying payload is paused
  // waiting for response headers.
  bool paused_for_connect_ : 1;
  bool calling_encode_headers_ : 1;
  bool decoding_headers_ : 1;
  bool encoding_headers_only_ : 1;
  bool await_stream_ : 1;

  ActiveUpstreamRequest active_request_{*this};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  std::unique_ptr<GenericUpstream> upstream_;
  uint32_t downstream_data_disabled_{};
};

// The base request for Upstream.
class UpstreamRequest : public Logger::Loggable<Logger::Id::router>,
                        public LinkedObject<UpstreamRequest>,
                        public Event::DeferredDeletable,
                        public Http::FilterManagerCallbacks {
public:
  UpstreamRequest(RouterFilterInterface& parent, std::unique_ptr<GenericConnPool>&& conn_pool);
  ~UpstreamRequest() override;

  void encodeUpstreamHeaders(bool end_stream);
  void encodeUpstreamData(Buffer::Instance& data, bool end_stream);
  void encodeUpstreamTrailers(Http::RequestTrailerMap& trailers);
  void encodeUpstreamMetadata(Http::MetadataMapPtr&& metadata_map_ptr);
  void maybeEndEncode(bool end_stream) { 
    filter_manager_.maybeEndDecode(end_stream);
  }

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);

  // Http::FilterManagerCallbacks
  void encodeHeaders(Http::ResponseHeaderMap& response_headers, bool end_stream) override;
  void encode100ContinueHeaders(Http::ResponseHeaderMap& response_headers) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  void encodeMetadata(Http::MetadataMapVector& metadata) override;
  void endStream() override { 
    // TODO(snowp): What happens here?
   }
  void setRequestTrailers(Http::RequestTrailerMapPtr&&) override {
    // TODO(snowp): Should an upstream filter be able to inject trailers into the HCM filter chain?
    // Or should these trailers only be injected at the upstream level?
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void setContinueHeaders(Http::ResponseHeaderMapPtr&& response_headers) override;
  void setResponseHeaders(Http::ResponseHeaderMapPtr&& response_headers) override;
  void setResponseTrailers(Http::ResponseTrailerMapPtr&& response_trailers) override;
  Http::RequestHeaderMapOptRef requestHeaders() override;
  Http::RequestTrailerMapOptRef requestTrailers() override;
  Http::ResponseHeaderMapOptRef continueHeaders() override;
  Http::ResponseHeaderMapOptRef responseHeaders() override;
  Http::ResponseTrailerMapOptRef responseTrailers() override;

  void onDecoderFilterBelowWriteBufferLowWatermark() override;
  void onDecoderFilterAboveWriteBufferHighWatermark() override;
  void upgradeFilterChainCreated() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void disarmRequestTimeout() override {}
  void resetIdleTimer() override {}
  void recreateStream(StreamInfo::FilterStateSharedPtr) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void resetStream() override;
  const Router::RouteEntry::UpgradeMap* upgradeMap() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Router::RouteConstSharedPtr route(const Router::RouteCallback&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void clearRouteCache() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  absl::optional<Router::ConfigConstSharedPtr> routeConfig() override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void requestRouteConfigUpdate(Event::Dispatcher&,
                                Http::RouteConfigUpdatedCallbackSharedPtr) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Tracing::Span& activeSpan() override { return *span_; }
  void onResponseDataTooLarge() override {}
  void onRequestDataTooLarge() override {}
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onLocalReply(Http::Code) override {}
  Tracing::Config& tracingConfig() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  const ScopeTrackedObject& scope() override;

  void onStreamMaxDurationReached();

  void setupPerTryTimeout();
  void onPerTryTimeout();
  void readEnable();
  // void encodeBodyAndTrailers();

  // Getters and setters
  Upstream::HostDescriptionConstSharedPtr& upstreamHost() { return upstream_host_; }
  void outlierDetectionTimeoutRecorded(bool recorded) {
    outlier_detection_timeout_recorded_ = recorded;
  }
  bool outlierDetectionTimeoutRecorded() { return outlier_detection_timeout_recorded_; }
  const StreamInfo::UpstreamTiming& upstreamTiming() { return upstream_timing_; }
  void retried(bool value) { retried_ = value; }
  bool retried() { return retried_; }
  bool grpcRqSuccessDeferred() { return grpc_rq_success_deferred_; }
  void grpcRqSuccessDeferred(bool deferred) { grpc_rq_success_deferred_ = deferred; }
  void upstreamCanary(bool value) { upstream_canary_ = value; }
  bool upstreamCanary() { return upstream_canary_; }
  bool awaitingHeaders() { return awaiting_headers_; }
  void recordTimeoutBudget(bool value) { record_timeout_budget_ = value; }
  bool createPerTryTimeoutOnRequestComplete() {
    return create_per_try_timeout_on_request_complete_;
  }
  bool encodeComplete() const { return encode_complete_; }
  RouterFilterInterface& parent() { return parent_; }

private:
  Http::RequestTrailerMapPtr request_trailers_;

  RouterFilterInterface& parent_;

  Event::TimerPtr per_try_timeout_;
  Tracing::SpanPtr span_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  Event::TimerPtr max_stream_duration_timer_;

  class NoopLocalReply : public LocalReply::LocalReply {
    void rewrite(const Http::RequestHeaderMap*, Http::ResponseHeaderMap&,
                 StreamInfo::StreamInfoImpl&, Http::Code&, std::string&,
                 absl::string_view&) const override {}
  };

  bool outlier_detection_timeout_recorded_ : 1;
  bool retried_ : 1;
  bool grpc_rq_success_deferred_;
  bool upstream_canary_ : 1;
  bool awaiting_headers_ : 1;
  bool encode_complete_ : 1;
  bool decode_complete_ : 1;

  Http::ResponseHeaderMapPtr continue_to_encode_;
  Http::ResponseHeaderMapPtr headers_to_encode_;
  Http::ResponseTrailerMapPtr trailers_to_encode_;

  // Copies of upstream headers/trailers. These are only set if upstream
  // access logging is configured.
  Http::ResponseHeaderMapPtr upstream_headers_;
  Http::ResponseTrailerMapPtr upstream_trailers_;
  Http::MetadataMapVector downstream_metadata_map_vector_;

  // Tracks whether we deferred a per try timeout because the downstream request
  // had not been completed yet.
  bool create_per_try_timeout_on_request_complete_ : 1;

  // Sentinel to indicate if timeout budget tracking is configured for the cluster,
  // and if so, if the per-try histogram should record a value.
  bool record_timeout_budget_ : 1;
  StreamInfo::UpstreamTiming upstream_timing_;

  const LocalReply::LocalReply& noopLocalReply() { CONSTRUCT_ON_FIRST_USE(NoopLocalReply); }

  UpstreamFilterFactory filter_factory_;
  Http::FilterManager filter_manager_;

  friend UpstreamRequestFilter;
};

} // namespace Router
} // namespace Envoy
