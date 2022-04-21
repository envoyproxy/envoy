#include "source/common/router/upstream_request.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/debug_config.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

namespace Envoy {
namespace Router {

UpstreamRequest::UpstreamRequest(RouterFilterInterface& parent,
                                 std::unique_ptr<GenericConnPool>&& conn_pool,
                                 bool can_send_early_data, bool can_use_http3)
    : parent_(parent), conn_pool_(std::move(conn_pool)), grpc_rq_success_deferred_(false),
      stream_info_(parent_.callbacks()->dispatcher().timeSource(), nullptr),
      start_time_(parent_.callbacks()->dispatcher().timeSource().monotonicTime()),
      calling_encode_headers_(false), upstream_canary_(false), decode_complete_(false),
      encode_complete_(false), encode_trailers_(false), retried_(false), awaiting_headers_(true),
      outlier_detection_timeout_recorded_(false),
      create_per_try_timeout_on_request_complete_(false), paused_for_connect_(false),
      record_timeout_budget_(parent_.cluster()->timeoutBudgetStats().has_value()),
      cleaned_up_(false), had_upstream_(false),
      stream_options_({can_send_early_data, can_use_http3}) {
  if (parent_.config().start_child_span_) {
    span_ = parent_.callbacks()->activeSpan().spawnChild(
        parent_.callbacks()->tracingConfig(), "router " + parent.cluster()->name() + " egress",
        parent.timeSource().systemTime());
    if (parent.attemptCount() != 1) {
      // This is a retry request, add this metadata to span.
      span_->setTag(Tracing::Tags::get().RetryCount, std::to_string(parent.attemptCount() - 1));
    }
  }
  stream_info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  parent_.callbacks()->streamInfo().setUpstreamInfo(stream_info_.upstreamInfo());

  stream_info_.healthCheck(parent_.callbacks()->streamInfo().healthCheck());
  absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info =
      parent_.callbacks()->streamInfo().upstreamClusterInfo();
  if (cluster_info.has_value()) {
    stream_info_.setUpstreamClusterInfo(*cluster_info);
  }
}

UpstreamRequest::~UpstreamRequest() { cleanUp(); }

void UpstreamRequest::cleanUp() {
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;
  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::finalizeUpstreamSpan(*span_, upstream_headers_.get(),
                                                     upstream_trailers_.get(), stream_info_,
                                                     Tracing::EgressConfig::get());
  }

  if (per_try_timeout_ != nullptr) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
  if (per_try_idle_timeout_ != nullptr) {
    // Allows for testing.
    per_try_idle_timeout_->disableTimer();
  }
  if (max_stream_duration_timer_ != nullptr) {
    max_stream_duration_timer_->disableTimer();
  }
  clearRequestEncoder();

  // If desired, fire the per-try histogram when the UpstreamRequest
  // completes.
  if (record_timeout_budget_) {
    Event::Dispatcher& dispatcher = parent_.callbacks()->dispatcher();
    const MonotonicTime end_time = dispatcher.timeSource().monotonicTime();
    const std::chrono::milliseconds response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
    Upstream::ClusterTimeoutBudgetStatsOptRef tb_stats = parent_.cluster()->timeoutBudgetStats();
    tb_stats->get().upstream_rq_timeout_budget_per_try_percent_used_.recordValue(
        FilterUtility::percentageOfTimeout(response_time, parent_.timeout().per_try_timeout_));
  }

  // Ditto for request/response size histograms.
  Upstream::ClusterRequestResponseSizeStatsOptRef req_resp_stats_opt =
      parent_.cluster()->requestResponseSizeStats();
  if (req_resp_stats_opt.has_value()) {
    auto& req_resp_stats = req_resp_stats_opt->get();
    req_resp_stats.upstream_rq_headers_size_.recordValue(parent_.downstreamHeaders()->byteSize());
    req_resp_stats.upstream_rq_body_size_.recordValue(stream_info_.bytesSent());

    if (response_headers_size_.has_value()) {
      req_resp_stats.upstream_rs_headers_size_.recordValue(response_headers_size_.value());
      req_resp_stats.upstream_rs_body_size_.recordValue(stream_info_.bytesReceived());
    }
  }

  stream_info_.onRequestComplete();
  for (const auto& upstream_log : parent_.config().upstream_logs_) {
    upstream_log->log(parent_.downstreamHeaders(), upstream_headers_.get(),
                      upstream_trailers_.get(), stream_info_);
  }

  while (downstream_data_disabled_ != 0) {
    parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
    parent_.cluster()->stats().upstream_flow_control_drained_total_.inc();
    --downstream_data_disabled_;
  }
}

void UpstreamRequest::decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  ASSERT(Http::HeaderUtility::isSpecial1xx(*headers));
  addResponseHeadersSize(headers->byteSize());
  parent_.onUpstream1xxHeaders(std::move(headers), *this);
}

void UpstreamRequest::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  resetPerTryIdleTimer();
  addResponseHeadersSize(headers->byteSize());

  // We drop unsupported 1xx on the floor here. 101 upgrade headers need to be passed to the client
  // as part of the final response. Most 1xx headers are handled in onUpstream1xxHeaders.
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

  // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
  // byte.
  upstreamTiming().onFirstUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
  maybeEndDecode(end_stream);

  awaiting_headers_ = false;
  if (!parent_.config().upstream_logs_.empty()) {
    upstream_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*headers);
  }
  stream_info_.response_code_ = static_cast<uint32_t>(response_code);

  if (paused_for_connect_ && response_code == 200) {
    encodeBodyAndTrailers();
    paused_for_connect_ = false;
  }

  parent_.onUpstreamHeaders(response_code, std::move(headers), *this, end_stream);
}

void UpstreamRequest::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  resetPerTryIdleTimer();
  maybeEndDecode(end_stream);
  stream_info_.addBytesReceived(data.length());
  parent_.onUpstreamData(data, *this, end_stream);
}

void UpstreamRequest::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  maybeEndDecode(true);
  if (!parent_.config().upstream_logs_.empty()) {
    upstream_trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers);
  }
  parent_.onUpstreamTrailers(std::move(trailers), *this);
}

void UpstreamRequest::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "UpstreamRequest " << this << "\n";
  const auto addressProvider = connection().connectionInfoProviderSharedPtr();
  const Http::RequestHeaderMap* request_headers = parent_.downstreamHeaders();
  DUMP_DETAILS(addressProvider);
  DUMP_DETAILS(request_headers);
}

const RouteEntry& UpstreamRequest::routeEntry() const { return *parent_.routeEntry(); }

const Network::Connection& UpstreamRequest::connection() const {
  return *parent_.callbacks()->connection();
}

void UpstreamRequest::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  parent_.onUpstreamMetadata(std::move(metadata_map));
}

void UpstreamRequest::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    upstreamTiming().onLastUpstreamRxByteReceived(parent_.callbacks()->dispatcher().timeSource());
    decode_complete_ = true;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  StreamInfo::UpstreamInfo& upstream_info = *streamInfo().upstreamInfo();
  upstream_info.setUpstreamHost(host);
  upstream_host_ = host;
  parent_.onUpstreamHostSelected(host);
}

void UpstreamRequest::encodeHeaders(bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  conn_pool_->newStream(this);
}

void UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  if (!upstream_ || paused_for_connect_) {
    ENVOY_STREAM_LOG(trace, "buffering {} bytes", *parent_.callbacks(), data.length());
    if (!buffered_request_body_) {
      buffered_request_body_ = parent_.callbacks()->dispatcher().getWatermarkFactory().createBuffer(
          [this]() -> void { this->enableDataFromDownstreamForFlowControl(); },
          [this]() -> void { this->disableDataFromDownstreamForFlowControl(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
      buffered_request_body_->setWatermarks(parent_.callbacks()->decoderBufferLimit());
    }

    buffered_request_body_->move(data);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks(), data.length());
    stream_info_.addBytesSent(data.length());
    upstream_->encodeData(data, end_stream);
    if (end_stream) {
      upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
    }
  }
}

void UpstreamRequest::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ASSERT(!encode_complete_);
  encode_complete_ = true;
  encode_trailers_ = true;

  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "buffering trailers", *parent_.callbacks());
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying trailers", *parent_.callbacks());
    upstream_->encodeTrailers(trailers);
    upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
  }
}

void UpstreamRequest::encodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) {
  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "upstream_ not ready. Store metadata_map to encode later: {}",
                     *parent_.callbacks(), *metadata_map_ptr);
    downstream_metadata_map_vector_.emplace_back(std::move(metadata_map_ptr));
  } else {
    ENVOY_STREAM_LOG(trace, "Encode metadata: {}", *parent_.callbacks(), *metadata_map_ptr);
    Http::MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    upstream_->encodeMetadata(metadata_map_vector);
  }
}

void UpstreamRequest::onResetStream(Http::StreamResetReason reason,
                                    absl::string_view transport_failure_reason) {
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());

  if (span_ != nullptr) {
    // Add tags about reset.
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, Http::Utility::resetReasonToString(reason));
  }
  clearRequestEncoder();
  awaiting_headers_ = false;
  if (!calling_encode_headers_) {
    stream_info_.setResponseFlag(Filter::streamResetReasonToResponseFlag(reason));
    parent_.onUpstreamReset(reason, transport_failure_reason, *this);
  } else {
    deferred_reset_reason_ = reason;
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

  if (conn_pool_->cancelAnyPendingStream()) {
    ENVOY_STREAM_LOG(debug, "canceled pool request", *parent_.callbacks());
    ASSERT(!upstream_);
  }

  if (upstream_) {
    ENVOY_STREAM_LOG(debug, "resetting pool request", *parent_.callbacks());
    upstream_->resetStream();
    clearRequestEncoder();
  }
}

void UpstreamRequest::resetPerTryIdleTimer() {
  if (per_try_idle_timeout_ != nullptr) {
    per_try_idle_timeout_->enableTimer(parent_.timeout().per_try_idle_timeout_);
  }
}

void UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout().per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks()->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout().per_try_timeout_);
  }

  ASSERT(!per_try_idle_timeout_);
  if (parent_.timeout().per_try_idle_timeout_.count() > 0) {
    per_try_idle_timeout_ =
        parent_.callbacks()->dispatcher().createTimer([this]() -> void { onPerTryIdleTimeout(); });
    resetPerTryIdleTimer();
  }
}

void UpstreamRequest::onPerTryIdleTimeout() {
  ENVOY_STREAM_LOG(debug, "upstream per try idle timeout", *parent_.callbacks());
  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout);
  parent_.onPerTryIdleTimeout(*this);
}

void UpstreamRequest::onPerTryTimeout() {
  // If we've sent anything downstream, ignore the per try timeout and let the response continue
  // up to the global timeout
  if (!parent_.downstreamResponseStarted()) {
    ENVOY_STREAM_LOG(debug, "upstream per try timeout", *parent_.callbacks());

    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout);
    parent_.onPerTryTimeout(*this);
  } else {
    ENVOY_STREAM_LOG(debug,
                     "ignored upstream per try timeout due to already started downstream response",
                     *parent_.callbacks());
  }
}

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason,
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
    reset_reason = Http::StreamResetReason::ConnectionFailure;
  }

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reset_reason, transport_failure_reason);
}

void UpstreamRequest::onPoolReady(
    std::unique_ptr<GenericUpstream>&& upstream, Upstream::HostDescriptionConstSharedPtr host,
    const Network::Address::InstanceConstSharedPtr& upstream_local_address,
    const StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) {
  // This may be called under an existing ScopeTrackerScopeState but it will unwind correctly.
  ScopeTrackerScopeState scope(&parent_.callbacks()->scope(), parent_.callbacks()->dispatcher());
  ENVOY_STREAM_LOG(debug, "pool ready", *parent_.callbacks());
  upstream_ = std::move(upstream);
  had_upstream_ = true;
  // Have the upstream use the account of the downstream.
  upstream_->setAccount(parent_.callbacks()->account());

  if (parent_.requestVcluster()) {
    // The cluster increases its upstream_rq_total_ counter right before firing this onPoolReady
    // callback. Hence, the upstream request increases the virtual cluster's upstream_rq_total_ stat
    // here.
    parent_.requestVcluster()->stats().upstream_rq_total_.inc();
  }

  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  onUpstreamHostSelected(host);

  if (protocol) {
    stream_info_.protocol(protocol.value());
  }

  StreamInfo::UpstreamInfo& upstream_info = *stream_info_.upstreamInfo();
  parent_.callbacks()->streamInfo().setUpstreamInfo(stream_info_.upstreamInfo());
  if (info.upstreamInfo().has_value()) {
    auto& upstream_timing = info.upstreamInfo().value().get().upstreamTiming();
    upstreamTiming().upstream_connect_start_ = upstream_timing.upstream_connect_start_;
    upstreamTiming().upstream_connect_complete_ = upstream_timing.upstream_connect_complete_;
    upstreamTiming().upstream_handshake_complete_ = upstream_timing.upstream_handshake_complete_;
    upstream_info.setUpstreamNumStreams(info.upstreamInfo().value().get().upstreamNumStreams());
  }

  upstream_info.setUpstreamFilterState(std::make_shared<StreamInfo::FilterStateImpl>(
      info.filterState().parent()->parent(), StreamInfo::FilterState::LifeSpan::Request));
  upstream_info.setUpstreamLocalAddress(upstream_local_address);
  upstream_info.setUpstreamSslConnection(info.downstreamAddressProvider().sslConnection());

  if (info.downstreamAddressProvider().connectionID().has_value()) {
    upstream_info.setUpstreamConnectionId(info.downstreamAddressProvider().connectionID().value());
  }

  if (info.downstreamAddressProvider().interfaceName().has_value()) {
    upstream_info.setUpstreamInterfaceName(
        info.downstreamAddressProvider().interfaceName().value());
  }

  stream_info_.setUpstreamBytesMeter(upstream_->bytesMeter());
  StreamInfo::StreamInfo::syncUpstreamAndDownstreamBytesMeter(parent_.callbacks()->streamInfo(),
                                                              stream_info_);

  if (parent_.downstreamEndStream()) {
    setupPerTryTimeout();
  } else {
    create_per_try_timeout_on_request_complete_ = true;
  }

  // Make sure the connection manager will inform the downstream watermark manager when the
  // downstream buffers are overrun. This may result in immediate watermark callbacks referencing
  // the encoder.
  parent_.callbacks()->addDownstreamWatermarkCallbacks(downstream_watermark_manager_);

  calling_encode_headers_ = true;
  auto* headers = parent_.downstreamHeaders();
  if (parent_.routeEntry()->autoHostRewrite() && !host->hostname().empty()) {
    Http::Utility::updateAuthority(*parent_.downstreamHeaders(), host->hostname(),
                                   parent_.routeEntry()->appendXfh());
  }

  if (span_ != nullptr) {
    span_->injectContext(*parent_.downstreamHeaders());
  } else {
    // No independent child span for current upstream request then inject the parent span's tracing
    // context into the request headers.
    // The injectContext() of the parent span may be called repeatedly when the request is retried.
    parent_.callbacks()->activeSpan().injectContext(*parent_.downstreamHeaders());
  }

  upstreamTiming().onFirstUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());

  // Make sure that when we are forwarding CONNECT payload we do not do so until
  // the upstream has accepted the CONNECT request.
  if (protocol.has_value() &&
      headers->getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    paused_for_connect_ = true;
  }

  absl::optional<std::chrono::milliseconds> max_stream_duration;
  if (parent_.dynamicMaxStreamDuration().has_value()) {
    max_stream_duration = parent_.dynamicMaxStreamDuration().value();
  } else if (upstream_host_->cluster().commonHttpProtocolOptions().has_max_stream_duration()) {
    max_stream_duration = std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
        upstream_host_->cluster().commonHttpProtocolOptions().max_stream_duration()));
  }

  if (max_stream_duration.has_value() && max_stream_duration->count()) {
    max_stream_duration_timer_ = parent_.callbacks()->dispatcher().createTimer(
        [this]() -> void { onStreamMaxDurationReached(); });
    max_stream_duration_timer_->enableTimer(*max_stream_duration);
  }

  const Http::Status status =
      upstream_->encodeHeaders(*parent_.downstreamHeaders(), shouldSendEndStream());
  calling_encode_headers_ = false;

  if (!status.ok()) {
    // It is possible that encodeHeaders() fails. This can happen if filters or other extensions
    // erroneously remove required headers.
    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError);
    const std::string details =
        absl::StrCat(StreamInfo::ResponseCodeDetails::get().FilterRemovedRequiredRequestHeaders,
                     "{", StringUtil::replaceAllEmptySpace(status.message()), "}");
    parent_.callbacks()->sendLocalReply(Http::Code::ServiceUnavailable, status.message(), nullptr,
                                        absl::nullopt, details);
    return;
  }

  if (!paused_for_connect_) {
    encodeBodyAndTrailers();
  }
}

void UpstreamRequest::encodeBodyAndTrailers() {
  // It is possible to get reset in the middle of an encodeHeaders() call. This happens for
  // example in the HTTP/2 codec if the frame cannot be encoded for some reason. This should never
  // happen but it's unclear if we have covered all cases so protect against it and test for it.
  // One specific example of a case where this happens is if we try to encode a total header size
  // that is too big in HTTP/2 (64K currently).
  if (deferred_reset_reason_) {
    onResetStream(deferred_reset_reason_.value(), absl::string_view());
  } else {
    // Encode metadata after headers and before any other frame type.
    if (!downstream_metadata_map_vector_.empty()) {
      ENVOY_STREAM_LOG(debug, "Send metadata onPoolReady. {}", *parent_.callbacks(),
                       downstream_metadata_map_vector_);
      upstream_->encodeMetadata(downstream_metadata_map_vector_);
      downstream_metadata_map_vector_.clear();
    }

    if (buffered_request_body_) {
      stream_info_.addBytesSent(buffered_request_body_->length());
      upstream_->encodeData(*buffered_request_body_, encode_complete_ && !encode_trailers_);
    }

    if (encode_trailers_) {
      upstream_->encodeTrailers(*parent_.downstreamTrailers());
    }

    if (encode_complete_) {
      upstreamTiming().onLastUpstreamTxByteSent(parent_.callbacks()->dispatcher().timeSource());
    }
  }
}

void UpstreamRequest::onStreamMaxDurationReached() {
  upstream_host_->cluster().stats().upstream_rq_max_duration_reached_.inc();

  // The upstream had closed then try to retry along with retry policy.
  parent_.onStreamMaxDurationReached(*this);
}

void UpstreamRequest::clearRequestEncoder() {
  // Before clearing the encoder, unsubscribe from callbacks.
  if (upstream_) {
    parent_.callbacks()->removeDownstreamWatermarkCallbacks(downstream_watermark_manager_);
  }
  upstream_.reset();
}

void UpstreamRequest::DownstreamWatermarkManager::onAboveWriteBufferHighWatermark() {
  ASSERT(parent_.upstream_);

  // There are two states we should get this callback in: 1) the watermark was
  // hit due to writes from a different filter instance over a shared
  // downstream connection, or 2) the watermark was hit due to THIS filter
  // instance writing back the "winning" upstream request. In either case we
  // can disable reads from upstream.
  ASSERT(!parent_.parent_.finalUpstreamRequest() ||
         &parent_ == parent_.parent_.finalUpstreamRequest());
  // The downstream connection is overrun. Pause reads from upstream.
  // If there are multiple calls to readDisable either the codec (H2) or the underlying
  // Network::Connection (H1) will handle reference counting.
  parent_.parent_.cluster()->stats().upstream_flow_control_paused_reading_total_.inc();
  parent_.upstream_->readDisable(true);
}

void UpstreamRequest::DownstreamWatermarkManager::onBelowWriteBufferLowWatermark() {
  ASSERT(parent_.upstream_);

  // One source of connection blockage has buffer available. Pass this on to the stream, which
  // will resume reads if this was the last remaining high watermark.
  parent_.parent_.cluster()->stats().upstream_flow_control_resumed_reading_total_.inc();
  parent_.upstream_->readDisable(false);
}

void UpstreamRequest::disableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not slow down other upstream requests. If we've
  // already seen the full downstream request (downstream_end_stream_) then
  // disabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstreamRequests().size() == 1 || parent_.downstreamEndStream());
  parent_.cluster()->stats().upstream_flow_control_backed_up_total_.inc();
  parent_.callbacks()->onDecoderFilterAboveWriteBufferHighWatermark();
  ++downstream_data_disabled_;
}

void UpstreamRequest::enableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not overflow any write buffers in other upstream
  // requests. If we've already seen the full downstream request
  // (downstream_end_stream_) then enabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstreamRequests().size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstreamRequests().size() == 1 || parent_.downstreamEndStream());
  parent_.cluster()->stats().upstream_flow_control_drained_total_.inc();
  parent_.callbacks()->onDecoderFilterBelowWriteBufferLowWatermark();
  ASSERT(downstream_data_disabled_ != 0);
  if (downstream_data_disabled_ > 0) {
    --downstream_data_disabled_;
  }
}

} // namespace Router
} // namespace Envoy
