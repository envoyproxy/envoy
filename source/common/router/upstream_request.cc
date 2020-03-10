#include "common/router/upstream_request.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
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
#include "common/router/retry_state_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {

UpstreamRequest::UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool)
    : parent_(parent), conn_pool_(pool), grpc_rq_success_deferred_(false),
      stream_info_(pool.protocol(), parent_.callbacks_->dispatcher().timeSource()),
      start_time_(parent_.callbacks_->dispatcher().timeSource().monotonicTime()),
      calling_encode_headers_(false), upstream_canary_(false), decode_complete_(false),
      encode_complete_(false), encode_trailers_(false), retried_(false), awaiting_headers_(true),
      outlier_detection_timeout_recorded_(false),
      create_per_try_timeout_on_request_complete_(false),
      record_timeout_budget_(parent_.cluster_->timeoutBudgetStats().has_value()) {
  if (parent_.config_.start_child_span_) {
    span_ = parent_.callbacks_->activeSpan().spawnChild(
        parent_.callbacks_->tracingConfig(), "router " + parent.cluster_->name() + " egress",
        parent.timeSource().systemTime());
    if (parent.attempt_count_ != 1) {
      // This is a retry request, add this metadata to span.
      span_->setTag(Tracing::Tags::get().RetryCount, std::to_string(parent.attempt_count_ - 1));
    }
  }

  stream_info_.healthCheck(parent_.callbacks_->streamInfo().healthCheck());
}

UpstreamRequest::~UpstreamRequest() {
  if (span_ != nullptr) {
    Tracing::HttpTracerUtility::finalizeUpstreamSpan(*span_, upstream_headers_.get(),
                                                     upstream_trailers_.get(), stream_info_,
                                                     Tracing::EgressConfig::get());
  }

  if (per_try_timeout_ != nullptr) {
    // Allows for testing.
    per_try_timeout_->disableTimer();
  }
  clearRequestEncoder();

  // If desired, fire the per-try histogram when the UpstreamRequest
  // completes.
  if (record_timeout_budget_) {
    Event::Dispatcher& dispatcher = parent_.callbacks_->dispatcher();
    const MonotonicTime end_time = dispatcher.timeSource().monotonicTime();
    const std::chrono::milliseconds response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
    parent_.cluster_->timeoutBudgetStats()
        ->upstream_rq_timeout_budget_per_try_percent_used_.recordValue(
            FilterUtility::percentageOfTimeout(response_time, parent_.timeout_.per_try_timeout_));
  }

  stream_info_.setUpstreamTiming(upstream_timing_);
  stream_info_.onRequestComplete();
  for (const auto& upstream_log : parent_.config_.upstream_logs_) {
    upstream_log->log(parent_.downstream_headers_, upstream_headers_.get(),
                      upstream_trailers_.get(), stream_info_);
  }
}

void UpstreamRequest::decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  ASSERT(100 == Http::Utility::getResponseStatus(*headers));
  parent_.onUpstream100ContinueHeaders(std::move(headers), *this);
}

void UpstreamRequest::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
  // byte.
  upstream_timing_.onFirstUpstreamRxByteReceived(parent_.callbacks_->dispatcher().timeSource());
  maybeEndDecode(end_stream);

  awaiting_headers_ = false;
  if (!parent_.config_.upstream_logs_.empty()) {
    upstream_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*headers);
  }
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  stream_info_.response_code_ = static_cast<uint32_t>(response_code);
  parent_.onUpstreamHeaders(response_code, std::move(headers), *this, end_stream);
}

void UpstreamRequest::decodeData(Buffer::Instance& data, bool end_stream) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  maybeEndDecode(end_stream);
  stream_info_.addBytesReceived(data.length());
  parent_.onUpstreamData(data, *this, end_stream);
}

void UpstreamRequest::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  maybeEndDecode(true);
  if (!parent_.config_.upstream_logs_.empty()) {
    upstream_trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*trailers);
  }
  parent_.onUpstreamTrailers(std::move(trailers), *this);
}

void UpstreamRequest::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  parent_.onUpstreamMetadata(std::move(metadata_map));
}

void UpstreamRequest::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    upstream_timing_.onLastUpstreamRxByteReceived(parent_.callbacks_->dispatcher().timeSource());
    decode_complete_ = true;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  stream_info_.onUpstreamHostSelected(host);
  upstream_host_ = host;
  parent_.callbacks_->streamInfo().onUpstreamHostSelected(host);
  if (parent_.retry_state_ && host) {
    parent_.retry_state_->onHostAttempted(host);
  }
}

void UpstreamRequest::encodeHeaders(bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  // It's possible for a reset to happen inline within the newStream() call. In this case, we
  // might get deleted inline as well. Only write the returned handle out if it is not nullptr to
  // deal with this case.
  Http::ConnectionPool::Cancellable* handle = conn_pool_.newStream(*this, *this);
  if (handle) {
    conn_pool_stream_handle_ = handle;
  }
}

void UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!encode_complete_);
  encode_complete_ = end_stream;

  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "buffering {} bytes", *parent_.callbacks_, data.length());
    if (!buffered_request_body_) {
      buffered_request_body_ = std::make_unique<Buffer::WatermarkBuffer>(
          [this]() -> void { this->enableDataFromDownstreamForFlowControl(); },
          [this]() -> void { this->disableDataFromDownstreamForFlowControl(); });
      buffered_request_body_->setWatermarks(parent_.callbacks_->decoderBufferLimit());
    }

    buffered_request_body_->move(data);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks_, data.length());
    stream_info_.addBytesSent(data.length());
    upstream_->encodeData(data, end_stream);
    if (end_stream) {
      upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
    }
  }
}

void UpstreamRequest::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ASSERT(!encode_complete_);
  encode_complete_ = true;
  encode_trailers_ = true;

  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "buffering trailers", *parent_.callbacks_);
  } else {
    ASSERT(downstream_metadata_map_vector_.empty());

    ENVOY_STREAM_LOG(trace, "proxying trailers", *parent_.callbacks_);
    upstream_->encodeTrailers(trailers);
    upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
  }
}

void UpstreamRequest::encodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) {
  if (!upstream_) {
    ENVOY_STREAM_LOG(trace, "upstream_ not ready. Store metadata_map to encode later: {}",
                     *parent_.callbacks_, *metadata_map_ptr);
    downstream_metadata_map_vector_.emplace_back(std::move(metadata_map_ptr));
  } else {
    ENVOY_STREAM_LOG(trace, "Encode metadata: {}", *parent_.callbacks_, *metadata_map_ptr);
    Http::MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    upstream_->encodeMetadata(metadata_map_vector);
  }
}

void UpstreamRequest::onResetStream(Http::StreamResetReason reason,
                                    absl::string_view transport_failure_reason) {
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());

  if (span_ != nullptr) {
    // Add tags about reset.
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, Http::Utility::resetReasonToString(reason));
  }

  clearRequestEncoder();
  awaiting_headers_ = false;
  if (!calling_encode_headers_) {
    stream_info_.setResponseFlag(parent_.streamResetReasonToResponseFlag(reason));
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

  if (conn_pool_stream_handle_) {
    ENVOY_STREAM_LOG(debug, "cancelling pool request", *parent_.callbacks_);
    ASSERT(!upstream_);
    conn_pool_stream_handle_->cancel();
    conn_pool_stream_handle_ = nullptr;
  }

  if (upstream_) {
    ENVOY_STREAM_LOG(debug, "resetting pool request", *parent_.callbacks_);
    upstream_->resetStream();
    clearRequestEncoder();
  }
}

void UpstreamRequest::setupPerTryTimeout() {
  ASSERT(!per_try_timeout_);
  if (parent_.timeout_.per_try_timeout_.count() > 0) {
    per_try_timeout_ =
        parent_.callbacks_->dispatcher().createTimer([this]() -> void { onPerTryTimeout(); });
    per_try_timeout_->enableTimer(parent_.timeout_.per_try_timeout_);
  }
}

void UpstreamRequest::onPerTryTimeout() {
  // If we've sent anything downstream, ignore the per try timeout and let the response continue
  // up to the global timeout
  if (!parent_.downstream_response_started_) {
    ENVOY_STREAM_LOG(debug, "upstream per try timeout", *parent_.callbacks_);

    stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout);
    parent_.onPerTryTimeout(*this);
  } else {
    ENVOY_STREAM_LOG(debug,
                     "ignored upstream per try timeout due to already started downstream response",
                     *parent_.callbacks_);
  }
}

void UpstreamRequest::onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                                    absl::string_view transport_failure_reason,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  Http::StreamResetReason reset_reason = Http::StreamResetReason::ConnectionFailure;
  switch (reason) {
  case Http::ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = Http::StreamResetReason::Overflow;
    break;
  case Http::ConnectionPool::PoolFailureReason::ConnectionFailure:
    reset_reason = Http::StreamResetReason::ConnectionFailure;
    break;
  }

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reset_reason, transport_failure_reason);
}

void UpstreamRequest::onPoolReady(Http::RequestEncoder& request_encoder,
                                  Upstream::HostDescriptionConstSharedPtr host,
                                  const StreamInfo::StreamInfo& info) {
  // This may be called under an existing ScopeTrackerScopeState but it will unwind correctly.
  ScopeTrackerScopeState scope(&parent_.callbacks_->scope(), parent_.callbacks_->dispatcher());
  ENVOY_STREAM_LOG(debug, "pool ready", *parent_.callbacks_);

  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  onUpstreamHostSelected(host);

  stream_info_.setUpstreamLocalAddress(request_encoder.getStream().connectionLocalAddress());
  parent_.callbacks_->streamInfo().setUpstreamLocalAddress(
      request_encoder.getStream().connectionLocalAddress());

  stream_info_.setUpstreamSslConnection(info.downstreamSslConnection());
  parent_.callbacks_->streamInfo().setUpstreamSslConnection(info.downstreamSslConnection());

  if (parent_.downstream_end_stream_) {
    setupPerTryTimeout();
  } else {
    create_per_try_timeout_on_request_complete_ = true;
  }

  conn_pool_stream_handle_ = nullptr;
  setRequestEncoder(request_encoder);
  calling_encode_headers_ = true;
  if (parent_.route_entry_->autoHostRewrite() && !host->hostname().empty()) {
    parent_.downstream_headers_->setHost(host->hostname());
  }

  if (span_ != nullptr) {
    span_->injectContext(*parent_.downstream_headers_);
  }

  upstream_timing_.onFirstUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());

  const bool end_stream = !buffered_request_body_ && encode_complete_ && !encode_trailers_;
  // If end_stream is set in headers, and there are metadata to send, delays end_stream. The case
  // only happens when decoding headers filters return ContinueAndEndStream.
  const bool delay_headers_end_stream = end_stream && !downstream_metadata_map_vector_.empty();
  request_encoder.encodeHeaders(*parent_.downstream_headers_,
                                end_stream && !delay_headers_end_stream);
  calling_encode_headers_ = false;

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
      ENVOY_STREAM_LOG(debug, "Send metadata onPoolReady. {}", *parent_.callbacks_,
                       downstream_metadata_map_vector_);
      request_encoder.encodeMetadata(downstream_metadata_map_vector_);
      downstream_metadata_map_vector_.clear();
      if (delay_headers_end_stream) {
        Buffer::OwnedImpl empty_data("");
        request_encoder.encodeData(empty_data, true);
      }
    }

    if (buffered_request_body_) {
      stream_info_.addBytesSent(buffered_request_body_->length());
      request_encoder.encodeData(*buffered_request_body_, encode_complete_ && !encode_trailers_);
    }

    if (encode_trailers_) {
      request_encoder.encodeTrailers(*parent_.downstream_trailers_);
    }

    if (encode_complete_) {
      upstream_timing_.onLastUpstreamTxByteSent(parent_.callbacks_->dispatcher().timeSource());
    }
  }
}

void UpstreamRequest::setRequestEncoder(Http::RequestEncoder& request_encoder) {
  upstream_.reset(new HttpUpstream(*this, &request_encoder));
  // Now that there is an encoder, have the connection manager inform the manager when the
  // downstream buffers are overrun. This may result in immediate watermark callbacks referencing
  // the encoder.
  parent_.callbacks_->addDownstreamWatermarkCallbacks(downstream_watermark_manager_);
}

void UpstreamRequest::clearRequestEncoder() {
  // Before clearing the encoder, unsubscribe from callbacks.
  if (upstream_) {
    parent_.callbacks_->removeDownstreamWatermarkCallbacks(downstream_watermark_manager_);
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
  ASSERT(!parent_.parent_.final_upstream_request_ ||
         &parent_ == parent_.parent_.final_upstream_request_);

  // The downstream connection is overrun. Pause reads from upstream.
  // If there are multiple calls to readDisable either the codec (H2) or the underlying
  // Network::Connection (H1) will handle reference counting.
  parent_.parent_.cluster_->stats().upstream_flow_control_paused_reading_total_.inc();
  parent_.upstream_->readDisable(true);
}

void UpstreamRequest::DownstreamWatermarkManager::onBelowWriteBufferLowWatermark() {
  ASSERT(parent_.upstream_);

  // One source of connection blockage has buffer available. Pass this on to the stream, which
  // will resume reads if this was the last remaining high watermark.
  parent_.parent_.cluster_->stats().upstream_flow_control_resumed_reading_total_.inc();
  parent_.upstream_->readDisable(false);
}

void UpstreamRequest::disableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not slow down other upstream requests. If we've
  // already seen the full downstream request (downstream_end_stream_) then
  // disabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstream_requests_.size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstream_requests_.size() == 1 || parent_.downstream_end_stream_);
  parent_.cluster_->stats().upstream_flow_control_backed_up_total_.inc();
  parent_.callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
}

void UpstreamRequest::enableDataFromDownstreamForFlowControl() {
  // If there is only one upstream request, we can be assured that
  // disabling reads will not overflow any write buffers in other upstream
  // requests. If we've already seen the full downstream request
  // (downstream_end_stream_) then enabling reads is a noop.
  // This assert condition must be true because
  // parent_.upstream_requests_.size() can only be greater than 1 in the
  // case of a per-try-timeout with hedge_on_per_try_timeout enabled, and
  // the per try timeout timer is started only after downstream_end_stream_
  // is true.
  ASSERT(parent_.upstream_requests_.size() == 1 || parent_.downstream_end_stream_);
  parent_.cluster_->stats().upstream_flow_control_drained_total_.inc();
  parent_.callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
}

} // namespace Router
} // namespace Envoy
