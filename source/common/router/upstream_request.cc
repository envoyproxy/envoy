#include "common/router/upstream_request.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/router.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {

UpstreamRequestFilter::UpstreamRequestFilter(UpstreamRequest& parent,
                                             std::unique_ptr<GenericConnPool>&& conn_pool)
    : parent_(parent), conn_pool_(std::move(conn_pool)),
      start_time_(parent_.parent().callbacks()->dispatcher().timeSource().monotonicTime()),
      paused_for_connect_(false), calling_encode_headers_(false), decoding_headers_(false),
      encoding_headers_only_(false), await_stream_(true), continue_headers_encoded_(false) {}

UpstreamRequestFilter::~UpstreamRequestFilter() { clearRequestEncoder(); }

void UpstreamRequestFilter::resetStream() {
  if (conn_pool_->cancelAnyPendingStream()) {
    ENVOY_STREAM_LOG(debug, "canceled pool request", *parent_.parent_.callbacks());
    ASSERT(!upstream_);
  }

  if (upstream_) {
    ENVOY_STREAM_LOG(debug, "resetting pool request", *parent_.parent_.callbacks());
    upstream_->resetStream();
    clearRequestEncoder();
  }
}

UpstreamRequest::UpstreamRequest(RouterFilterInterface& parent,
                                 std::unique_ptr<GenericConnPool>&& conn_pool)
    : parent_(parent), outlier_detection_timeout_recorded_(false), retried_(false),
      grpc_rq_success_deferred_(false), upstream_canary_(false), awaiting_headers_(true),
      encode_complete_(false), decode_complete_(false), destroyed_(false),
      create_per_try_timeout_on_request_complete_(false),
      record_timeout_budget_(parent_.cluster()->timeoutBudgetStats().has_value()),
      filter_manager_(*this, parent_.callbacks()->dispatcher(), *parent_.callbacks()->connection(),
                      parent_.callbacks()->streamId(), true,
                      std::numeric_limits<uint32_t>::max(), // TODO(snowp): This should probably not
                                                            // be infinite!
                      // parent_.callbacks()->decoderBufferLimit(),
                      filter_factory_, noopLocalReply(), conn_pool->protocol(),
                      parent_.callbacks()->dispatcher().timeSource(), nullptr,
                      StreamInfo::FilterState::FilterChain) {
  auto filter = std::make_shared<UpstreamRequestFilter>(*this, std::move(conn_pool));
  filter_manager_.addStreamDecoderFilter(filter);
  filter_ = filter.get();
  if (parent_.config().start_child_span_) {
    span_ = parent_.callbacks()->activeSpan().spawnChild(
        parent_.callbacks()->tracingConfig(), "router " + parent.cluster()->name() + " egress",
        parent.timeSource().systemTime());
    if (parent.attemptCount() != 1) {
      // This is a retry request, add this metadata to span.
      span_->setTag(Tracing::Tags::get().RetryCount, std::to_string(parent.attemptCount() - 1));
    }
  }

  filter_manager_.streamInfo().healthCheck(parent_.callbacks()->streamInfo().healthCheck());
}

UpstreamRequest::~UpstreamRequest() {

  if (!destroyed_) {
    ENVOY_LOG_MISC(info, "IN DTOR");
    onDeferredDelete();
  }
}
void UpstreamRequest::onDeferredDelete() {
  ENVOY_LOG_MISC(info, "DONG DEFERRED DELELTE");
  ASSERT(!destroyed_);
  destroyed_ = true;

  if (per_try_timeout_ != nullptr) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }

  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::finalizeUpstreamSpan(
        *span_, responseHeaders() ? &responseHeaders()->get() : nullptr,
        responseTrailers() ? &responseTrailers()->get() : nullptr, filter_manager_.streamInfo(),
        Tracing::EgressConfig::get());
  }

  if (max_stream_duration_timer_ != nullptr) {
    max_stream_duration_timer_->disableTimer();
  }

  filter_manager_.streamInfo().setUpstreamTiming(upstream_timing_);
  filter_manager_.streamInfo().onRequestComplete();
  for (const auto& upstream_log : parent_.config().upstream_logs_) {
    upstream_log->log(
        parent_.downstreamHeaders(), responseHeaders() ? &responseHeaders()->get() : nullptr,
        responseTrailers() ? &responseTrailers()->get() : nullptr, filter_manager_.streamInfo());
  }

  filter_manager_.destroyFilters();
}

void UpstreamRequest::onDecoderFilterBelowWriteBufferLowWatermark() {
  parent_.cluster()->stats().upstream_flow_control_drained_total_.inc();
  parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
}
void UpstreamRequest::onDecoderFilterAboveWriteBufferHighWatermark() {
  parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
}
void UpstreamRequest::setContinueHeaders(Http::ResponseHeaderMapPtr&& response_headers) {
  continue_to_encode_ = std::move(response_headers);
}
void UpstreamRequest::setResponseHeaders(Http::ResponseHeaderMapPtr&& response_headers) {
  headers_to_encode_ = std::move(response_headers);
}
void UpstreamRequest::setResponseTrailers(Http::ResponseTrailerMapPtr&& response_trailers) {
  trailers_to_encode_ = std::move(response_trailers);
}
Http::RequestHeaderMapOptRef UpstreamRequest::requestHeaders() {
  return parent_.downstreamHeaders() ? absl::make_optional(std::ref(*parent_.downstreamHeaders()))
                                     : absl::nullopt;
}
Http::RequestTrailerMapOptRef UpstreamRequest::requestTrailers() {
  return parent_.downstreamTrailers() ? absl::make_optional(std::ref(*parent_.downstreamTrailers()))
                                      : absl::nullopt;
}

Http::ResponseHeaderMapOptRef UpstreamRequest::continueHeaders() {
  return continue_to_encode_ ? absl::make_optional(std::ref(*continue_to_encode_))
                             : parent_.callbacks()->continueHeaders();
}
Http::ResponseHeaderMapOptRef UpstreamRequest::responseHeaders() {
  return headers_to_encode_ ? absl::make_optional(std::ref(*headers_to_encode_))
                            : parent_.callbacks()->responseHeaders();
}
Http::ResponseTrailerMapOptRef UpstreamRequest::responseTrailers() {
  return trailers_to_encode_ ? absl::make_optional(std::ref(*trailers_to_encode_))
                             : parent_.callbacks()->responseTrailers();
}

void UpstreamRequest::encode100ContinueHeaders(Http::ResponseHeaderMap& headers) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  ASSERT(100 == Http::Utility::getResponseStatus(headers));
  parent_.onUpstream100ContinueHeaders(std::move(continue_to_encode_), *this);
}

void UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());
  maybeEndDecode(end_stream);
  parent_.onUpstreamData(data, *this, end_stream);
}

void UpstreamRequest::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  const uint64_t response_code = Http::Utility::getResponseStatus(headers);

  // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
  // byte.
  upstream_timing_.onFirstUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
  maybeEndDecode(end_stream);

  awaiting_headers_ = false;
  if (!parent_.config().upstream_logs_.empty()) {
    upstream_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);
  }
  filter_manager_.streamInfo().response_code_ = static_cast<uint32_t>(response_code);

  parent_.onUpstreamHeaders(response_code, std::move(headers_to_encode_), *this, end_stream);
}

void UpstreamRequest::encodeTrailers(Http::ResponseTrailerMap&) {
  maybeEndDecode(true);
  parent_.onUpstreamTrailers(std::move(trailers_to_encode_), *this);
}
void UpstreamRequest::encodeMetadata(Http::MetadataMapVector& metadata) {
  parent_.onUpstreamMetadata(metadata);
}

void UpstreamRequestFilter::ActiveUpstreamRequest::decode100ContinueHeaders(
    Http::ResponseHeaderMapPtr&& headers) {
  ScopeTrackerScopeState scope(&parent_.parent_.parent_.callbacks()->scope(),
                               parent_.parent_.parent_.callbacks()->dispatcher());

  if (!parent_.continue_headers_encoded_) {
    parent_.continue_headers_encoded_ = true;
    parent_.decoder_callbacks_->encode100ContinueHeaders(std::move(headers));
  }
}

void UpstreamRequestFilter::disableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not slow down other upstream requests. If we've
  // already seen the full downstream request (downstream_end_stream_) then
  // disabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.parent_.upstreamRequests().size() == 1 || parent_.parent_.downstreamEndStream());
  parent_.parent_.cluster()->stats().upstream_flow_control_backed_up_total_.inc();
  parent_.parent_.callbacks()->onDecoderFilterAboveWriteBufferHighWatermark();
  ++downstream_data_disabled_;
}
void UpstreamRequestFilter::enableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not overflow any write buffers in other upstream
  // requests. If we've already seen the full downstream request
  // (downstream_end_stream_) then enabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.parent_.upstreamRequests().size() == 1 || parent_.parent_.downstreamEndStream());
  parent_.parent_.cluster()->stats().upstream_flow_control_drained_total_.inc();
  parent_.parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
  ASSERT(downstream_data_disabled_ != 0);
  if (downstream_data_disabled_ > 0) {
    --downstream_data_disabled_;
  }
}
void UpstreamRequestFilter::onDestroy() {
  if (!parent_.decode_complete_) {
    if (conn_pool_->cancelAnyPendingStream()) {
      ENVOY_STREAM_LOG(debug, "canceled pool request", *parent_.parent_.callbacks());
      // TODO(snowp): Do we need to reset the upstream here?
      //  ASSERT(!upstream_);
    }
  }

  // If desired, fire the per-try histogram when the UpstreamRequest
  // completes.
  if (parent_.record_timeout_budget_) {
    Event::Dispatcher& dispatcher = parent_.parent_.callbacks()->dispatcher();
    const MonotonicTime end_time = dispatcher.timeSource().monotonicTime();
    const std::chrono::milliseconds response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
    Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats =
        parent_.parent_.cluster()->timeoutBudgetStats();
    tb_stats->get().upstream_rq_timeout_budget_per_try_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time,
                                           parent_.parent_.timeout().per_try_timeout_));
  }
}

void UpstreamRequestFilter::ActiveUpstreamRequest::decodeHeaders(
    Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  // We drop 1xx other than 101 on the floor; 101 upgrade headers need to be passed to the client as
  // part of the final response. 100-continue headers are handled in onUpstream100ContinueHeaders.
  //
  // We could in principle handle other headers here, but this might result in the double invocation
  // of decodeHeaders() (once for informational, again for non-informational), which is likely an
  // easy to miss corner case in the filter and HCM contract.
  //
  // This filtering is done early in upstream request, unlike 100 coalescing which is performed in
  // the router filter, since the filtering only depends on the state of a single upstream, and we
  // don't want to confuse accounting such as onFirstUpstreamRxByteReceived() with informational
  // headers.
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  if (Http::CodeUtility::is1xx(response_code) &&
      response_code != enumToInt(Http::Code::SwitchingProtocols)) {
    return;
  }

  ScopeTrackerScopeState scope(&parent_.parent_.parent_.callbacks()->scope(),
                               parent_.parent_.parent_.callbacks()->dispatcher());

  if (!parent_.parent_.parent_.config().upstream_logs_.empty()) {
    parent_.parent_.upstream_headers_ =
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*headers);
  }

  if (parent_.paused_for_connect_ && response_code == 200) {
    parent_.decoder_callbacks_->continueDecoding();
  }

  parent_.decoder_callbacks_->encodeHeaders(std::move(headers), end_stream);
}

void UpstreamRequestFilter::ActiveUpstreamRequest::decodeData(Buffer::Instance& data,
                                                              bool end_stream) {
  if (parent_.deferred_reset_reason_) {
    onResetStream(parent_.deferred_reset_reason_.value(), absl::string_view());
    return;
  }

  ScopeTrackerScopeState scope(&parent_.decoder_callbacks_->scope(),
                               parent_.decoder_callbacks_->dispatcher());

  parent_.parent_.filter_manager_.streamInfo().addBytesReceived(data.length());
  parent_.decoder_callbacks_->encodeData(data, end_stream);
}

void UpstreamRequestFilter::ActiveUpstreamRequest::decodeTrailers(
    Http::ResponseTrailerMapPtr&& trailers) {
  if (parent_.deferred_reset_reason_) {
    onResetStream(parent_.deferred_reset_reason_.value(), absl::string_view());
    return;
  }

  ScopeTrackerScopeState scope(&parent_.parent_.parent_.callbacks()->scope(),
                               parent_.parent_.parent_.callbacks()->dispatcher());

  if (!parent_.parent_.parent_.config().upstream_logs_.empty()) {
    parent_.parent_.upstream_trailers_ =
        Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers);
  }
  parent_.decoder_callbacks_->encodeTrailers(std::move(trailers));
}
const RouteEntry& UpstreamRequestFilter::ActiveUpstreamRequest::routeEntry() const {
  return *parent_.parent_.parent_.routeEntry();
}

const Network::Connection& UpstreamRequestFilter::ActiveUpstreamRequest::connection() const {
  return *parent_.parent_.parent_.callbacks()->connection();
}

void UpstreamRequestFilter::ActiveUpstreamRequest::decodeMetadata(
    Http::MetadataMapPtr&& metadata_map) {
  ScopeTrackerScopeState scope(&parent_.parent_.parent_.callbacks()->scope(),
                               parent_.parent_.parent_.callbacks()->dispatcher());

  parent_.decoder_callbacks_->encodeMetadata(std::move(metadata_map));
}

Http::FilterDataStatus UpstreamRequestFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (deferred_reset_reason_) {
    active_request_.onResetStream(deferred_reset_reason_.value(), absl::string_view());
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.parent_.callbacks(), data.length());
  decoder_callbacks_->streamInfo().addBytesSent(data.length());
  upstream_->encodeData(data, end_stream);
  if (end_stream) {
    parent_.upstream_timing_.onLastUpstreamTxByteSent(
        parent_.parent_.callbacks()->dispatcher().timeSource());
  }
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus
UpstreamRequestFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (deferred_reset_reason_) {
    active_request_.onResetStream(deferred_reset_reason_.value(), absl::string_view());
    return Http::FilterTrailersStatus::StopIteration;
  }
  ENVOY_STREAM_LOG(trace, "proxying trailers", *parent_.parent_.callbacks());
  upstream_->encodeTrailers(trailers);
  parent_.upstream_timing_.onLastUpstreamTxByteSent(
      parent_.parent_.callbacks()->dispatcher().timeSource());
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterMetadataStatus UpstreamRequestFilter::decodeMetadata(Http::MetadataMap& metadata_map) {
  ENVOY_STREAM_LOG(trace, "Encode metadata: {}", *parent_.parent_.callbacks(), metadata_map);
  // TODO(snowp): This introduces a copy , fix
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.emplace_back(std::make_unique<Http::MetadataMap>(metadata_map));
  upstream_->encodeMetadata(metadata_map_vector);

  return Http::FilterMetadataStatus::Continue;
}
void UpstreamRequest::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    upstream_timing_.onLastUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
    decode_complete_ = true;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  filter_manager_.streamInfo().onUpstreamHostSelected(host);
  upstream_host_ = host;
  parent_.callbacks()->streamInfo().onUpstreamHostSelected(host);
  parent_.onUpstreamHostSelected(host);
}

void UpstreamRequest::encodeUpstreamHeaders(bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  filter_manager_.maybeEndDecode(end_stream);
  filter_manager_.decodeHeaders(*parent_.downstreamHeaders(), end_stream);
}

void UpstreamRequest::encodeUpstreamData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  filter_manager_.maybeEndDecode(end_stream);
  filter_manager_.decodeData(data, end_stream);
}

void UpstreamRequest::encodeUpstreamTrailers(Http::RequestTrailerMap& trailers) {
  filter_manager_.maybeEndDecode(true);
  filter_manager_.decodeTrailers(trailers);
}

void UpstreamRequest::encodeUpstreamMetadata(Http::MetadataMapPtr&& metadata_map_ptr) {
  filter_manager_.decodeMetadata(*metadata_map_ptr);
}

void UpstreamRequestFilter::ActiveUpstreamRequest::onResetStream(
    Http::StreamResetReason reason, absl::string_view transport_failure_reason) {
  ScopeTrackerScopeState scope(&parent_.parent_.parent_.callbacks()->scope(),
                               parent_.parent_.parent_.callbacks()->dispatcher());

  ENVOY_LOG_MISC(info, "SEEING RESET STREAM");
  if (parent_.parent_.span_ != nullptr) {
    // Add tags about reset.
    parent_.parent_.span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    parent_.parent_.span_->setTag(Tracing::Tags::get().ErrorReason,
                                  Http::Utility::resetReasonToString(reason));
  }

  if (parent_.conn_pool_->cancelAnyPendingStream()) {
    ENVOY_STREAM_LOG(debug, "canceled pool request", *parent_.parent_.parent_.callbacks());
    ASSERT(!parent_.upstream_);
  }
  parent_.clearRequestEncoder();
  parent_.parent_.awaiting_headers_ = false;
  if (!parent_.calling_encode_headers_) {
    parent_.parent_.filter_manager_.streamInfo().setResponseFlag(
        Filter::streamResetReasonToResponseFlag(reason));
    parent_.parent_.parent_.onUpstreamReset(reason, transport_failure_reason, parent_.parent_);
  } else {
    parent_.deferred_reset_reason_ = reason;
  }
}

void UpstreamRequest::resetStream() {
  // Don't reset the stream if we're already done with it.
  if (encode_complete_ && decode_complete_) {
    return;
  }

  if (span_ != nullptr) {
    // Add tags about the cancellation.
    span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);
  }

  filter_->resetStream();
}

void UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout().per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks()->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout().per_try_timeout_);
  }
}

void UpstreamRequest::onPerTryTimeout() {
  // If we've sent anything downstream, ignore the per try timeout and let the response continue
  // up to the global timeout
  if (!parent_.downstreamResponseStarted()) {
    ENVOY_STREAM_LOG(debug, "upstream per try timeout", *parent_.callbacks());

    filter_manager_.streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout);
    parent_.onPerTryTimeout(*this);
  } else {
    ENVOY_STREAM_LOG(debug,
                     "ignored upstream per try timeout due to already started downstream response",
                     *parent_.callbacks());
  }
}

void UpstreamRequestFilter::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                          absl::string_view transport_failure_reason,
                                          Upstream::HostDescriptionConstSharedPtr host) {
  Http::StreamResetReason reset_reason = Http::StreamResetReason::ConnectionFailure;
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = Http::StreamResetReason::Overflow;
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    FALLTHRU;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
    break;
  case ConnectionPool::PoolFailureReason::Timeout:
    reset_reason = Http::StreamResetReason::LocalReset;
  }

  // Mimic an upstream reset.
  parent_.onUpstreamHostSelected(host);
  active_request_.onResetStream(reset_reason, transport_failure_reason);
}

void UpstreamRequestFilter::onPoolReady(
    std::unique_ptr<GenericUpstream>&& upstream, Upstream::HostDescriptionConstSharedPtr host,
    const Network::Address::InstanceConstSharedPtr& upstream_local_address,
    const StreamInfo::StreamInfo& info) {
  // This may be called under an existing ScopeTrackerScopeState but it will unwind correctly.
  ScopeTrackerScopeState scope(&decoder_callbacks_->scope(), decoder_callbacks_->dispatcher());
  ENVOY_STREAM_LOG(debug, "pool ready", *decoder_callbacks_);
  upstream_ = std::move(upstream);

  if (parent_.parent_.requestVcluster()) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the virtual cluster's upstream_rq_total_ stat
    // here.
    parent_.parent_.requestVcluster()->stats().upstream_rq_total_.inc();
  }

  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  parent_.onUpstreamHostSelected(host);

  decoder_callbacks_->streamInfo().setUpstreamFilterState(
      std::make_shared<StreamInfo::FilterStateImpl>(info.filterState().parent()->parent(),
                                                    StreamInfo::FilterState::LifeSpan::Request));
  decoder_callbacks_->streamInfo().setUpstreamLocalAddress(upstream_local_address);
  parent_.parent_.callbacks()->streamInfo().setUpstreamLocalAddress(upstream_local_address);

  decoder_callbacks_->streamInfo().setUpstreamSslConnection(info.downstreamSslConnection());
  parent_.parent_.callbacks()->streamInfo().setUpstreamSslConnection(
      info.downstreamSslConnection());

  ENVOY_LOG_MISC(info, "IN POOL READY {} {}", parent_.parent_.downstreamEndStream(),
                 parent_.create_per_try_timeout_on_request_complete_);
  if (parent_.parent_.downstreamEndStream()) {
    parent_.setupPerTryTimeout();
  } else {
    parent_.create_per_try_timeout_on_request_complete_ = true;
  }

  // Make sure the connection manager will inform the downstream watermark manager when the
  // downstream buffers are overrun. This may result in immediate watermark callbacks referencing
  // the encoder.
  parent_.parent_.callbacks()->addDownstreamWatermarkCallbacks(downstream_watermark_manager_);

  auto headers = parent_.requestHeaders();
  if (parent_.parent_.routeEntry()->autoHostRewrite() && !host->hostname().empty()) {
    parent_.parent_.downstreamHeaders()->setHost(host->hostname());
  }

  if (parent_.span_ != nullptr) {
    parent_.span_->injectContext(*parent_.requestHeaders());
  }

  parent_.upstream_timing_.onFirstUpstreamTxByteSent(
      parent_.parent_.callbacks()->dispatcher().timeSource());

  // Make sure that when we are forwarding CONNECT payload we do not do so until
  // the upstream has accepted the CONNECT request.
  if (conn_pool_->protocol().has_value() &&
      headers->get().getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    paused_for_connect_ = true;
  }

  if (parent_.upstream_host_->cluster().commonHttpProtocolOptions().has_max_stream_duration()) {
    const auto max_stream_duration = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        parent_.upstream_host_->cluster().commonHttpProtocolOptions().max_stream_duration()));
    if (max_stream_duration.count()) {
      parent_.max_stream_duration_timer_ = parent_.parent_.callbacks()->dispatcher().createTimer(
          [this]() -> void { parent_.onStreamMaxDurationReached(); });
      parent_.max_stream_duration_timer_->enableTimer(max_stream_duration);
    }
  }

  calling_encode_headers_ = true;
  upstream_->encodeHeaders(*parent_.requestHeaders(), encoding_headers_only_);
  calling_encode_headers_ = false;

  if (encoding_headers_only_) {
    parent_.upstream_timing_.onLastUpstreamTxByteSent(
        parent_.parent_.callbacks()->dispatcher().timeSource());
  }

  if (deferred_reset_reason_) {
    active_request_.onResetStream(deferred_reset_reason_.value(), absl::string_view());
    return;
  }

  if (!paused_for_connect_ && !decoding_headers_) {
    paused_for_connect_ = false;
    decoder_callbacks_->continueDecoding();
  }

  await_stream_ = false;
}

const ScopeTrackedObject& UpstreamRequest::scope() { return parent_.callbacks()->scope(); }

void UpstreamRequest::onStreamMaxDurationReached() {
  upstream_host_->cluster().stats().upstream_rq_max_duration_reached_.inc();

  // The upstream had closed then try to retry along with retry policy.
  parent_.onStreamMaxDurationReached(*this);
}

void UpstreamRequestFilter::clearRequestEncoder() {
  // Before clearing the encoder, unsubscribe from callbacks.
  if (upstream_) {
    parent_.parent_.callbacks()->removeDownstreamWatermarkCallbacks(downstream_watermark_manager_);
  }
  upstream_.reset();
}

void UpstreamRequestFilter::DownstreamWatermarkManager::onAboveWriteBufferHighWatermark() {
  ASSERT(parent_.upstream_);

  // There are two states we should get this callback in: 1) the watermark was
  // hit due to writes from a different filter instance over a shared
  // downstream connection, or 2) the watermark was hit due to THIS filter
  // instance writing back the "winning" upstream request. In either case we
  // can disable reads from upstream.
  ASSERT(!parent_.parent_.parent_.finalUpstreamRequest() ||
         &parent_.parent_ == parent_.parent_.parent_.finalUpstreamRequest());
  // The downstream connection is overrun. Pause reads from upstream.
  // If there are multiple calls to readDisable either the codec (H2) or the underlying
  // Network::Connection (H1) will handle reference counting.
  parent_.parent_.parent_.cluster()->stats().upstream_flow_control_paused_reading_total_.inc();
  parent_.upstream_->readDisable(true);
}

void UpstreamRequestFilter::DownstreamWatermarkManager::onBelowWriteBufferLowWatermark() {
  ASSERT(parent_.upstream_);

  // One source of connection blockage has buffer available. Pass this on to the stream, which
  // will resume reads if this was the last remaining high watermark.
  parent_.parent_.parent_.cluster()->stats().upstream_flow_control_resumed_reading_total_.inc();
  parent_.upstream_->readDisable(false);
}

} // namespace Router
} // namespace Envoy
