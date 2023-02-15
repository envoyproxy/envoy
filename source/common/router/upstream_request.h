#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.validate.h"
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
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;
class UpstreamRequestFilterManagerCallbacks;
class UpstreamFilterManager;
class UpstreamCodecFilter;

/* The Upstream request is the base class for forwarding HTTP upstream.
 *
 * This supports both classic forwarding, and upstream filter mode, dictated by
 * allow_upstream_filters_ which is set via envoy.reloadable_features.allow_upstream_filters.
 *
 * In 'classic' mode, the UpstreamRequest requests a new stream, internally
 * buffers data and trailers in |buffered_request_body_| and |downstream_metadata_map_vector_|
 * and forwards the router's headers, buffered body, buffered metadata, and
 * router's trailers to the |upstream_| once the stream is established.
 *
 * In 'classic' mode, when the stream is established, the UpstreamRequest
 * handles upstream data by directly implementing UpstreamToDownstream. As
 * upstream headers/body/metadata/trailers are received, they are forwarded
 * directly to the router via the RouterFilterInterface |parent_|
 *
 * For upstream filter mode, things are more involved.
 *
 * On the new request path, payload (headers/body/metadata/data) still arrives via
 * the accept[X]fromRouter functions. Said data is immediately passed off to the
 * UpstreamFilterManager, which passes each item through the filter chain until
 * it arrives at the last filter in the chain, the UpstreamCodecFilter. If the upstream
 * stream is not established, the UpstreamCodecFilter returns StopAllIteration, and the
 * FilterManager will buffer data, using watermarks to push back to the router
 * filter if buffers become overrun. When an upstream connection is established,
 * the UpstreamCodecFilter will send data upstream.
 *
 * On the new response path, payload arrives from upstream via the UpstreamCodecFilter's
 * CodecBridge. It is passed off directly to the FilterManager, traverses the
 * filter chain, and completion is signaled via the
 * UpstreamRequestFilterManagerCallbacks's encode[X] functions. These somewhat
 * confusingly pass through the UpstreamRequest's legacy decode[X] functions
 * (required due to the UpstreamToDownstream interface, but will be renamed once
 * the classic mode is deprecated), and are finally passed to the router via the
 * RouterFilterInterface onUpstream[X] functions.
 *
 * There is some required communication between the UpstreamRequest and
 * UpstreamCodecFilter. This is accomplished via the UpstreamStreamFilterCallbacks
 * interface, with the UpstreamFilterManager acting as intermediary.
 */
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

  void acceptHeadersFromRouter(bool end_stream);
  void acceptDataFromRouter(Buffer::Instance& data, bool end_stream);
  void acceptTrailersFromRouter(Http::RequestTrailerMap& trailers);
  void acceptMetadataFromRouter(Http::MetadataMapPtr&& metadata_map_ptr);

  void acceptHeadersFromRouterOld(bool end_stream);
  void acceptDataFromRouterOld(Buffer::Instance& data, bool end_stream);
  void acceptTrailersFromRouterOld(Http::RequestTrailerMap& trailers);
  void acceptMetadataFromRouterOld(Http::MetadataMapPtr&& metadata_map_ptr);

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
  OptRef<const Network::Connection> connection() const override;
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
                   const Network::ConnectionInfoProvider& address_provider,
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
  void encodeBodyAndTrailers();

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
  friend class UpstreamCodecFilter;
  friend class UpstreamRequestFilterManagerCallbacks;
  StreamInfo::UpstreamTiming& upstreamTiming() {
    return stream_info_.upstreamInfo()->upstreamTiming();
  }
  bool shouldSendEndStream() {
    // Only encode end stream if the full request has been received, the body
    // has been sent, and any trailers or metadata have also been sent.
    return router_sent_end_stream_ && !buffered_request_body_ && !encode_trailers_ &&
           downstream_metadata_map_vector_.empty();
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
  OptRef<UpstreamToDownstream> upstream_interface_;
  std::list<Http::UpstreamCallbacks*> upstream_callbacks_;

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
  bool allow_upstream_filters_ : 1;
  Http::ConnectionPool::Instance::StreamOptions stream_options_;
  Event::TimerPtr max_stream_duration_timer_;

  std::unique_ptr<UpstreamRequestFilterManagerCallbacks> filter_manager_callbacks_;
  std::unique_ptr<Http::FilterManager> filter_manager_;

  // TODO(alyssawilk) remove these with allow_upstream_filters_
  Buffer::InstancePtr buffered_request_body_;
  Http::MetadataMapVector downstream_metadata_map_vector_;
};

class UpstreamRequestFilterManagerCallbacks : public Http::FilterManagerCallbacks,
                                              public Event::DeferredDeletable,
                                              public Http::UpstreamStreamFilterCallbacks {
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

  // These functions are delegated to the downstream HCM/FM
  OptRef<const Tracing::Config> tracingConfig() const override;
  const ScopeTrackedObject& scope() override;
  Tracing::Span& activeSpan() override;
  void resetStream(Http::StreamResetReason reset_reason,
                   absl::string_view transport_failure_reason) override;
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override;

  // Intentional no-op functions.
  void onResponseDataTooLarge() override {}
  void onRequestDataTooLarge() override {}
  void endStream() override {}
  void disarmRequestTimeout() override {}
  void resetIdleTimer() override {}
  void onLocalReply(Http::Code) override {}
  // Upgrade filter chains not supported.
  const Router::RouteEntry::UpgradeMap* upgradeMap() override { return nullptr; }

  // Unsupported functions.
  void recreateStream(StreamInfo::FilterStateSharedPtr) override {
    IS_ENVOY_BUG("recreateStream called from upstream filter");
  }
  void upgradeFilterChainCreated() override {
    IS_ENVOY_BUG("upgradeFilterChainCreated called from upstream filter");
  }
  OptRef<UpstreamStreamFilterCallbacks> upstreamCallbacks() override { return {*this}; }

  // Http::UpstreamStreamFilterCallbacks
  StreamInfo::StreamInfo& upstreamStreamInfo() override { return upstream_request_.streamInfo(); }
  OptRef<GenericUpstream> upstream() override {
    return makeOptRefFromPtr(upstream_request_.upstream_.get());
  }
  void dumpState(std::ostream& os, int indent_level = 0) const override {
    upstream_request_.dumpState(os, indent_level);
  }
  bool pausedForConnect() const override { return upstream_request_.paused_for_connect_; }
  void setPausedForConnect(bool value) override { upstream_request_.paused_for_connect_ = value; }
  const Http::ConnectionPool::Instance::StreamOptions& upstreamStreamOptions() const override {
    return upstream_request_.upstreamStreamOptions();
  }
  void addUpstreamCallbacks(Http::UpstreamCallbacks& callbacks) override {
    upstream_request_.upstream_callbacks_.push_back(&callbacks);
  }
  void setUpstreamToDownstream(UpstreamToDownstream& upstream_to_downstream_interface) override {
    upstream_request_.upstream_interface_ = upstream_to_downstream_interface;
  }

  Http::RequestTrailerMapPtr trailers_;
  Http::ResponseHeaderMapPtr informational_headers_;
  Http::ResponseHeaderMapPtr response_headers_;
  Http::ResponseTrailerMapPtr response_trailers_;
  UpstreamRequest& upstream_request_;
};

} // namespace Router
} // namespace Envoy
