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

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/hash.h"
#include "source/common/common/hex.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/filter_manager.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;
class UpstreamRequestFilterManagerCallbacks;
class UpstreamFilterManager;
class CodecFilter;

// The base request for Upstream.
class UpstreamRequest : public Logger::Loggable<Logger::Id::router>,
                        public UpstreamToDownstream,
                        public LinkedObject<UpstreamRequest>,
                        public GenericConnectionPoolCallbacks,
                        public Event::DeferredDeletable {
public:
  UpstreamRequest(RouterFilterInterface& parent, std::unique_ptr<GenericConnPool>&& conn_pool,
                  bool can_send_early_data, bool can_use_http3);
  ~UpstreamRequest() override;
  void deleteIsPending() override { cleanUp(); }

  // To be called from the destructor, or prior to deferred delete.
  void cleanUp();

  void encodeHeaders(bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeTrailers(Http::RequestTrailerMap& trailers);
  void encodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr);

  void resetStream();
  void setupPerTryTimeout();
  void maybeEndDecode(bool end_stream);
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

  // UpstreamToDownstream (Http::ResponseDecoder)
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) override;
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void dumpState(std::ostream& os, int indent_level) const override;

  // UpstreamToDownstream (Http::StreamCallbacks)
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override { disableDataFromDownstreamForFlowControl(); }
  void onBelowWriteBufferLowWatermark() override { enableDataFromDownstreamForFlowControl(); }
  // UpstreamToDownstream
  const Route& route() const override;
  const Network::Connection& connection() const override;
  const Http::ConnectionPool::Instance::StreamOptions& upstreamStreamOptions() const override {
    return stream_options_;
  }

  void disableDataFromDownstreamForFlowControl();
  void enableDataFromDownstreamForFlowControl();

  // GenericConnPool
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                   StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) override;
  UpstreamToDownstream& upstreamToDownstream() override;

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

  // Getters and setters
  Upstream::HostDescriptionConstSharedPtr& upstreamHost() { return upstream_host_; }
  void outlierDetectionTimeoutRecorded(bool recorded) {
    outlier_detection_timeout_recorded_ = recorded;
  }
  bool outlierDetectionTimeoutRecorded() { return outlier_detection_timeout_recorded_; }
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
  bool encodeComplete() const { return router_sent_end_stream_; }
  // Exposes streamInfo for the upstream stream.
  StreamInfo::StreamInfo& streamInfo() { return stream_info_; }
  bool hadUpstream() const { return had_upstream_; }

private:
  friend class UpstreamFilterManager;
  friend class CodecFilter;
  friend class UpstreamRequestFilterManagerCallbacks;
  StreamInfo::UpstreamTiming& upstreamTiming() {
    return stream_info_.upstreamInfo()->upstreamTiming();
  }
  void addResponseHeadersSize(uint64_t size) {
    response_headers_size_ = response_headers_size_.value_or(0) + size;
  }
  void resetPerTryIdleTimer();
  void onPerTryTimeout();
  void onPerTryIdleTimeout();

  RouterFilterInterface& parent_;
  std::unique_ptr<GenericConnPool> conn_pool_;
  bool grpc_rq_success_deferred_;
  Event::TimerPtr per_try_timeout_;
  Event::TimerPtr per_try_idle_timeout_;
  std::unique_ptr<GenericUpstream> upstream_;
  absl::optional<Http::StreamResetReason> deferred_reset_reason_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  DownstreamWatermarkManager downstream_watermark_manager_{*this};
  Tracing::SpanPtr span_;
  StreamInfo::StreamInfoImpl stream_info_;
  const MonotonicTime start_time_;
  // This is wrapped in an optional, since we want to avoid computing zero size headers when in
  // reality we just didn't get a response back.
  absl::optional<uint64_t> response_headers_size_{};
  // Copies of upstream headers/trailers. These are only set if upstream
  // access logging is configured.
  Http::ResponseHeaderMapPtr upstream_headers_;
  Http::ResponseTrailerMapPtr upstream_trailers_;
  std::shared_ptr<CodecFilter> codec_filter_;

  // Tracks the number of times the flow of data from downstream has been disabled.
  uint32_t downstream_data_disabled_{};
  bool calling_encode_headers_ : 1;
  bool upstream_canary_ : 1;
  bool router_sent_end_stream_ : 1;
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
  bool reset_stream_ : 1;

  // Sentinel to indicate if timeout budget tracking is configured for the cluster,
  // and if so, if the per-try histogram should record a value.
  bool record_timeout_budget_ : 1;
  // Track if one time clean up has been performed.
  bool cleaned_up_ : 1;
  bool had_upstream_ : 1;
  Http::ConnectionPool::Instance::StreamOptions stream_options_;
  Event::TimerPtr max_stream_duration_timer_;

  std::unique_ptr<UpstreamRequestFilterManagerCallbacks> fm_callbacks_;
  std::unique_ptr<Http::FilterManager> filter_manager_;
};

class UpstreamRequestFilterManagerCallbacks : public Http::FilterManagerCallbacks,
                                              public Event::DeferredDeletable {
public:
  UpstreamRequestFilterManagerCallbacks(UpstreamRequest& upstream_request)
      : upstream_request_(upstream_request) {}
  void encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    upstream_request_.decodeHeaders(std::move(response_headers_), end_stream);
  }
  void encode1xxHeaders(Http::ResponseHeaderMap&) override {
    upstream_request_.decode1xxHeaders(std::move(informational_headers_));
  }
  void encodeData(Buffer::Instance& data, bool end_stream) override {
    upstream_request_.decodeData(data, end_stream);
  }
  void encodeTrailers(Http::ResponseTrailerMap&) override {
    upstream_request_.decodeTrailers(std::move(response_trailers_));
  }
  void encodeMetadata(Http::MetadataMapPtr&& metadata) override {
    upstream_request_.decodeMetadata(std::move(metadata));
  }
  void setRequestTrailers(Http::RequestTrailerMapPtr&& request_trailers) override {
    trailers_ = std::move(request_trailers);
  }
  void setInformationalHeaders(Http::ResponseHeaderMapPtr&& response_headers) override {
    informational_headers_ = std::move(response_headers);
  }
  void setResponseHeaders(Http::ResponseHeaderMapPtr&& response_headers) override {
    response_headers_ = std::move(response_headers);
  }
  void setResponseTrailers(Http::ResponseTrailerMapPtr&& response_trailers) override {
    response_trailers_ = std::move(response_trailers);
  }
  Http::RequestHeaderMapOptRef requestHeaders() override;
  Http::RequestTrailerMapOptRef requestTrailers() override;
  Http::ResponseHeaderMapOptRef informationalHeaders() override {
    if (informational_headers_) {
      return {*informational_headers_};
    }
    return {};
  }
  Http::ResponseHeaderMapOptRef responseHeaders() override {
    if (response_headers_) {
      return {*response_headers_};
    }
    return {};
  }
  Http::ResponseTrailerMapOptRef responseTrailers() override {
    if (response_trailers_) {
      return {*response_trailers_};
    }
    return {};
  }
  // If the filter manager determines a decoder filter has available, tell
  // the router to resume the flow of data from downstream.
  void onDecoderFilterBelowWriteBufferLowWatermark() override {
    upstream_request_.onBelowWriteBufferLowWatermark();
  }
  // If the filter manager determines a decoder filter has too much data, tell
  // the router to stop the flow of data from downstream.
  void onDecoderFilterAboveWriteBufferHighWatermark() override {
    upstream_request_.onAboveWriteBufferHighWatermark();
  }

  // TODO(alyssawilk) classify these.
  const Router::RouteEntry::UpgradeMap* upgradeMap() override { return nullptr; }
  Router::RouteConstSharedPtr route(const Router::RouteCallback&) override { return nullptr; }
  absl::optional<Router::ConfigConstSharedPtr> routeConfig() override { return {}; }
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return {}; }

  // These functions are delegated to the downstream HCM/FM
  const Tracing::Config& tracingConfig() override;
  const ScopeTrackedObject& scope() override;
  Tracing::Span& activeSpan() override;
  void resetStream() override;
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override;

  // Intentional no-op functions.
  void onResponseDataTooLarge() override {}
  void onRequestDataTooLarge() override {}
  void endStream() override {}
  void disarmRequestTimeout() override {}
  void resetIdleTimer() override {}
  void onLocalReply(Http::Code) override {}

  // Unsupported functions.
  void requestRouteConfigUpdate(Http::RouteConfigUpdatedCallbackSharedPtr) override {
    IS_ENVOY_BUG("requestRouteConfigUpdate called from upstream filter");
  }
  void setRoute(Router::RouteConstSharedPtr) override {
    IS_ENVOY_BUG("set route cache called from upstream filter");
  }
  void recreateStream(StreamInfo::FilterStateSharedPtr) override {
    IS_ENVOY_BUG("recreateStream called from upstream filter");
  }
  void clearRouteCache() override { IS_ENVOY_BUG("clear route cache called from upstream filter"); }
  void upgradeFilterChainCreated() override {
    IS_ENVOY_BUG("upgradeFilterChainCreated called from upstream filter");
  }

  Http::RequestTrailerMapPtr trailers_;
  Http::ResponseHeaderMapPtr informational_headers_;
  Http::ResponseHeaderMapPtr response_headers_;
  Http::ResponseTrailerMapPtr response_trailers_;
  UpstreamRequest& upstream_request_;
};

// This is the last filter in the upstream filter chain.
// It takes request headers/body/data from the filter manager and encodes them to the upstream
// codec. It also registers the CodecBridge with the upstream stream, and takes response
// headers/body/data from the upstream stream and sends them to the filter manager.
class CodecFilter : public Http::StreamDecoderFilter, public Logger::Loggable<Logger::Id::router> {
public:
  CodecFilter(UpstreamRequest& request) : request_(request), bridge_(*this) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  virtual Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  void shipHeadersIfPaused(Http::RequestHeaderMap& headers);

  // This bridge connects the upstream stream to the filter manager.
  class CodecBridge : public UpstreamToDownstream {
  public:
    CodecBridge(CodecFilter& filter) : filter_(filter) {}
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) override;
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void dumpState(std::ostream& os, int indent_level) const override;

    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override {
      filter_.request_.onResetStream(reason, transport_failure_reason);
    }
    void onAboveWriteBufferHighWatermark() override {
      filter_.request_.onAboveWriteBufferHighWatermark();
    }
    void onBelowWriteBufferLowWatermark() override {
      filter_.request_.onBelowWriteBufferLowWatermark();
    }
    // UpstreamToDownstream
    const Route& route() const override { return filter_.request_.route(); }
    const Network::Connection& connection() const override { return filter_.request_.connection(); }
    const Http::ConnectionPool::Instance::StreamOptions& upstreamStreamOptions() const override {
      return filter_.request_.upstreamStreamOptions();
    }

  private:
    bool seen_1xx_headers_{};
    CodecFilter& filter_;
  };
  Http::StreamDecoderFilterCallbacks* callbacks_;
  UpstreamRequest& request_;
  CodecBridge bridge_;
  absl::optional<bool> latched_end_stream_;
};

} // namespace Router
} // namespace Envoy
