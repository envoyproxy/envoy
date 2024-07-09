#include "source/extensions/filters/network/generic_proxy/router/router.h"

#include <cstdint>

#include "envoy/common/conn_pool.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/tracing/tracer_impl.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/tracing.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

namespace {

struct ReasonViewAndFlag {
  absl::string_view view{};
  absl::optional<StreamInfo::CoreResponseFlag> flag{};
};

static constexpr ReasonViewAndFlag ReasonViewAndFlags[] = {
    {"local_reset", absl::nullopt},
    {"connection_failure", StreamInfo::CoreResponseFlag::UpstreamConnectionFailure},
    {"connection_termination", StreamInfo::CoreResponseFlag::UpstreamConnectionTermination},
    {"overflow", StreamInfo::CoreResponseFlag::UpstreamOverflow},
    {"protocol_error", StreamInfo::CoreResponseFlag::UpstreamProtocolError},
};

ReasonViewAndFlag resetReasonToViewAndFlag(StreamResetReason reason) {
  return ReasonViewAndFlags[static_cast<uint32_t>(reason)];
}

} // namespace

UpstreamRequest::UpstreamRequest(RouterFilter& parent, FrameFlags header_frame_flags,
                                 GenericUpstreamSharedPtr generic_upstream)
    : parent_(parent), generic_upstream_(std::move(generic_upstream)),
      stream_info_(parent.time_source_, nullptr, StreamInfo::FilterState::LifeSpan::FilterChain),
      upstream_info_(std::make_shared<StreamInfo::UpstreamInfoImpl>()),
      stream_id_(header_frame_flags.streamId()),
      expects_response_(!header_frame_flags.oneWayStream()) {

  // Host is known at this point and set the upstream host.
  onUpstreamHostSelected(generic_upstream_->upstreamHost());

  auto filter_callbacks = parent_.callbacks_;
  ASSERT(filter_callbacks != nullptr);

  // Set the upstream info for the stream info.
  stream_info_.setUpstreamInfo(upstream_info_);
  filter_callbacks->streamInfo().setUpstreamInfo(upstream_info_);
  stream_info_.healthCheck(filter_callbacks->streamInfo().healthCheck());
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);

  // Set tracing config.
  tracing_config_ = filter_callbacks->tracingConfig();
  if (!tracing_config_.has_value() || !tracing_config_->spawnUpstreamSpan()) {
    return;
  }
  span_ = filter_callbacks->activeSpan().spawnChild(
      tracing_config_.value().get(),
      absl::StrCat("router ", parent_.cluster_->observabilityName(), " egress"),
      parent.time_source_.systemTime());
}

void UpstreamRequest::startStream() {
  connecting_start_time_ = parent_.time_source_.monotonicTime();
  generic_upstream_->appendUpstreamRequest(stream_id_, this);
}

void UpstreamRequest::resetStream(StreamResetReason reason, absl::string_view reason_detail) {
  if (reset_or_response_complete_) {
    return;
  }
  reset_or_response_complete_ = true;

  // Remove this stream form the parent's list because this upstream request is reset.
  deferredDelete();

  ENVOY_LOG(debug, "generic proxy upstream request: reset upstream request");

  generic_upstream_->removeUpstreamRequest(stream_id_);
  generic_upstream_->cleanUp(true);

  if (span_ != nullptr) {
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, resetReasonToViewAndFlag(reason).view);
    TraceContextBridge trace_context{*parent_.request_stream_};
    Tracing::TracerUtility::finalizeSpan(*span_, trace_context, stream_info_,
                                         tracing_config_.value().get(), true);
  }

  // Notify the parent filter that the upstream request has been reset.
  parent_.onUpstreamRequestReset(*this, reason, reason_detail);
}

void UpstreamRequest::clearStream(bool close_connection) {
  // Set the upstream response complete flag to true first to ensure the possible
  // connection close event will not be handled.
  reset_or_response_complete_ = true;

  ENVOY_LOG(debug, "generic proxy upstream request: complete upstream request {}",
            close_connection);

  if (span_ != nullptr) {
    TraceContextBridge trace_context{*parent_.request_stream_};
    Tracing::TracerUtility::finalizeSpan(*span_, trace_context, stream_info_,
                                         tracing_config_.value().get(), true);
  }

  generic_upstream_->removeUpstreamRequest(stream_id_);
  generic_upstream_->cleanUp(close_connection);

  // Remove this stream form the parent's list because this upstream request is complete.
  deferredDelete();
}

void UpstreamRequest::deferredDelete() {
  if (parent_.upstream_request_.get() == this) {
    // Remove this stream from the parent and delete it at next event loop iteration.
    parent_.callbacks_->dispatcher().deferredDelete(std::move(parent_.upstream_request_));
    parent_.upstream_request_.reset();
  }
}

void UpstreamRequest::sendHeaderFrameToUpstream() {
  request_stream_header_sent_ = true;
  sendFrameToUpstream(*parent_.request_stream_, true);
}

void UpstreamRequest::sendCommonFrameToUpstream() {
  if (!request_stream_header_sent_) {
    // Do not send request frame to upstream until the request header is sent. It may be blocked
    // by the upstream connecting.
    return;
  }

  while (!parent_.request_stream_frames_.empty()) {
    auto frame = std::move(parent_.request_stream_frames_.front());
    parent_.request_stream_frames_.pop_front();
    if (!sendFrameToUpstream(*frame, false)) {
      break;
    }
  }
}

bool UpstreamRequest::sendFrameToUpstream(const StreamFrame& frame, bool header_frame) {
  ASSERT(generic_upstream_ != nullptr);
  const bool end_stream = frame.frameFlags().endStream();

  const auto result = generic_upstream_->clientCodec().encode(frame, *this);
  if (!result.ok()) {
    ENVOY_LOG(error, "Generic proxy: request encoding failure: {}", result.status().message());
    // The request encoding failure is treated as a protocol error.
    resetStream(StreamResetReason::ProtocolError, result.status().message());
    return false;
  }

  ENVOY_LOG(debug, "Generic proxy: send {} bytes to server, complete: {}", result.value(),
            end_stream);

  if (header_frame) {
    upstream_info_->upstreamTiming().onFirstUpstreamTxByteSent(parent_.time_source_);
  }

  // If the request is fully sent, record the last downstream tx byte sent time and clean
  // up the stream.
  if (end_stream) {
    upstream_info_->upstreamTiming().onLastUpstreamTxByteSent(parent_.time_source_);

    // Oneway requests need not to wait for the upstream response and complete directly.
    if (!expects_response_) {
      clearStream(false);
      parent_.completeDirectly();
    }
  }
  return true;
}

OptRef<const RouteEntry> UpstreamRequest::routeEntry() const {
  return makeOptRefFromPtr(parent_.route_entry_);
}

void UpstreamRequest::onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                        absl::string_view transport_failure_reason) {
  ENVOY_LOG(debug, "upstream request: tcp connection (bound or owned) failure");
  onUpstreamConnectionReady();

  if (reason == ConnectionPool::PoolFailureReason::Overflow) {
    resetStream(StreamResetReason::Overflow, transport_failure_reason);
    return;
  }
  resetStream(StreamResetReason::ConnectionFailure, transport_failure_reason);
}

void UpstreamRequest::onUpstreamSuccess() {
  ENVOY_LOG(debug, "upstream request: {} tcp connection has ready",
            parent_.config_->bindUpstreamConnection() ? "bound" : "owned");
  onUpstreamConnectionReady();

  const auto upstream_host = upstream_info_->upstream_host_.get();
  const Tracing::UpstreamContext upstream_context(
      upstream_host, upstream_host ? &upstream_host->cluster() : nullptr,
      Tracing::ServiceType::Unknown, false);

  TraceContextBridge trace_context{*parent_.request_stream_};

  if (span_ != nullptr) {
    span_->injectContext(trace_context, upstream_context);
  } else {
    parent_.callbacks_->activeSpan().injectContext(trace_context, upstream_context);
  }

  sendHeaderFrameToUpstream();
  sendCommonFrameToUpstream();
}

void UpstreamRequest::onUpstreamResponseComplete(bool drain_close) {
  clearStream(drain_close);
  upstream_info_->upstreamTiming().onLastUpstreamRxByteReceived(parent_.time_source_);
}

void UpstreamRequest::onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                                        absl::optional<StartTime> start_time) {
  if (response_stream_header_received_) {
    ENVOY_LOG(error, "upstream request: multiple StreamResponse received");
    resetStream(StreamResetReason::ProtocolError, {});
    return;
  }
  response_stream_header_received_ = true;

  if (start_time.has_value()) {
    // Set the start time from the upstream codec.
    upstream_info_->upstreamTiming().first_upstream_rx_byte_received_ =
        start_time.value().start_time_monotonic;
  } else {
    // Codec does not provide the start time and use the current time as the start time.
    upstream_info_->upstreamTiming().onFirstUpstreamRxByteReceived(parent_.time_source_);
  }

  if (response_header_frame->frameFlags().endStream()) {
    onUpstreamResponseComplete(response_header_frame->frameFlags().drainClose());
  }

  parent_.onResponseHeaderFrame(std::move(response_header_frame));
}

void UpstreamRequest::onDecodingSuccess(ResponseCommonFramePtr response_common_frame) {
  if (!response_stream_header_received_) {
    ENVOY_LOG(error, "upstream request: first frame is not StreamResponse");
    resetStream(StreamResetReason::ProtocolError, {});
    return;
  }

  if (response_common_frame->frameFlags().endStream()) {
    onUpstreamResponseComplete(response_common_frame->frameFlags().drainClose());
  }

  parent_.onResponseCommonFrame(std::move(response_common_frame));
}

void UpstreamRequest::onDecodingFailure(absl::string_view reason) {
  // Decoding failure after the response is complete, close the connection manually.
  // Note the removeUpstreamRequest() has already been called and the upstream was
  // already cleaned up. But it is still safe to call cleanUp() again to force the
  // upstream connection to be closed.
  //
  // This should only happen when some special cases, for example: The HTTP response
  // is complete but the request is not fully sent. The codec will throw an error
  // after the response is complete.
  if (reset_or_response_complete_) {
    generic_upstream_->cleanUp(true);
    return;
  }
  resetStream(StreamResetReason::ProtocolError, reason);
}

void UpstreamRequest::onConnectionClose(Network::ConnectionEvent event) {
  // If the upstream response is complete or the upstream request is reset then
  // ignore the connection close event.
  if (reset_or_response_complete_) {
    return;
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    resetStream(StreamResetReason::ConnectionTermination, {});
  } else if (event == Network::ConnectionEvent::LocalClose) {
    resetStream(StreamResetReason::LocalReset, {});
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "upstream request: selected host {}", host->address()->asStringView());
  upstream_info_->upstream_host_ = std::move(host);
}

void UpstreamRequest::onUpstreamConnectionReady() {
  ASSERT(connecting_start_time_.has_value());
  upstream_info_->upstreamTiming().recordConnectionPoolCallbackLatency(
      connecting_start_time_.value(), parent_.time_source_);
}

void RouterFilter::onResponseHeaderFrame(ResponseHeaderFramePtr response_header_frame) {
  if (response_header_frame->frameFlags().endStream()) {
    onFilterComplete();
  }
  callbacks_->onResponseHeaderFrame(std::move(response_header_frame));
}

void RouterFilter::onResponseCommonFrame(ResponseCommonFramePtr response_common_frame) {
  if (response_common_frame->frameFlags().endStream()) {
    onFilterComplete();
  }
  callbacks_->onResponseCommonFrame(std::move(response_common_frame));
}

void RouterFilter::completeDirectly() {
  onFilterComplete();
  callbacks_->completeDirectly();
}

void RouterFilter::onUpstreamRequestReset(UpstreamRequest&, StreamResetReason reason,
                                          absl::string_view reason_detail) {
  // If the RouterFilter is already completed (request is canceled or reset), ignore
  // the upstream request reset.
  if (filter_complete_) {
    return;
  }

  ASSERT(upstream_request_ == nullptr);

  // Retry is the upstream request is reset because of the connection failure or the
  // protocol error.
  if (couldRetry(reason)) {
    kickOffNewUpstreamRequest();
    return;
  }

  // Complete the filter/request and send the local reply to the downstream if no retry.
  const auto [view, flag] = resetReasonToViewAndFlag(reason);
  completeAndSendLocalReply(Status(StatusCode::kUnavailable, view), reason_detail, flag);
}

void RouterFilter::onFilterComplete() {
  // Ensure this method is called only once strictly.
  if (filter_complete_) {
    return;
  }
  filter_complete_ = true;

  // Clean up all pending upstream requests.
  if (upstream_request_ != nullptr) {
    auto* upstream_request = upstream_request_.get();
    // Remove the upstream request from the upstream request list first. The resetStream() will
    // also do this. But in some corner cases, the upstream request is already reset and triggers
    // the router filter to call onFilterComplete(). But note because the upstream request is
    // already reset, the resetStream() callings will be no-op and the router filter may fall
    // into the infinite loop.
    upstream_request->deferredDelete();
    upstream_request->resetStream(StreamResetReason::LocalReset, {});
  }

  // Clean up the timer to avoid the timeout event.
  if (timeout_timer_ != nullptr) {
    timeout_timer_->disableTimer();
    timeout_timer_.reset();
  }
}

void RouterFilter::mayRequestStreamEnd(bool stream_end_stream) {
  if (!stream_end_stream) {
    return;
  }

  request_stream_end_ = stream_end_stream;

  const auto timeout = route_entry_->timeout();
  if (timeout.count() > 0) {
    timeout_timer_ = callbacks_->dispatcher().createTimer([this] { onTimeout(); });
    timeout_timer_->enableTimer(timeout);
  }
}

void RouterFilter::onTimeout() {
  completeAndSendLocalReply(Status(StatusCode::kDeadlineExceeded, "timeout"), {},
                            StreamInfo::CoreResponseFlag::UpstreamRequestTimeout);
}

void RouterFilter::onDestroy() { onFilterComplete(); }

void RouterFilter::completeAndSendLocalReply(absl::Status status, absl::string_view details,
                                             absl::optional<StreamInfo::CoreResponseFlag> flag) {
  if (flag.has_value()) {
    callbacks_->streamInfo().setResponseFlag(flag.value());
  }
  onFilterComplete();
  callbacks_->sendLocalReply(std::move(status), details);
}

void RouterFilter::kickOffNewUpstreamRequest() {
  ASSERT(upstream_request_ == nullptr);

  num_retries_++;

  const auto& cluster_name = route_entry_->clusterName();
  ENVOY_LOG(debug, "generic proxy: route to cluster: {}", cluster_name);

  auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    completeAndSendLocalReply(Status(StatusCode::kNotFound, "cluster_not_found"), {},
                              StreamInfo::CoreResponseFlag::NoClusterFound);
    return;
  }

  cluster_ = thread_local_cluster->info();
  callbacks_->streamInfo().setUpstreamClusterInfo(cluster_);

  if (cluster_->maintenanceMode()) {
    // No response flag for maintenance mode for now.
    completeAndSendLocalReply(Status(StatusCode::kUnavailable, "cluster_maintain_mode"), {});
    return;
  }

  GenericUpstreamSharedPtr generic_upstream = generic_upstream_factory_->createGenericUpstream(
      *thread_local_cluster, this, const_cast<Network::Connection&>(*callbacks_->connection()),
      callbacks_->codecFactory(), config_->bindUpstreamConnection());
  if (generic_upstream == nullptr) {
    completeAndSendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"), {},
                              StreamInfo::CoreResponseFlag::NoHealthyUpstream);
    return;
  }

  upstream_request_ = std::make_unique<UpstreamRequest>(*this, request_stream_->frameFlags(),
                                                        std::move(generic_upstream));
  upstream_request_->startStream();
}

void RouterFilter::onRequestCommonFrame(RequestCommonFramePtr frame) {
  mayRequestStreamEnd(frame->frameFlags().endStream());

  request_stream_frames_.emplace_back(std::move(frame));
  if (upstream_request_ != nullptr) {
    upstream_request_->sendCommonFrameToUpstream();
  }
}

HeaderFilterStatus RouterFilter::decodeHeaderFrame(StreamRequest& request) {
  ENVOY_LOG(debug, "Try route request to the upstream based on the route entry");

  setRouteEntry(callbacks_->routeEntry());
  request_stream_ = &request;

  if (route_entry_ == nullptr) {
    ENVOY_LOG(debug, "No route for current request and send local reply");
    completeAndSendLocalReply(Status(StatusCode::kNotFound, "route_not_found"), {},
                              StreamInfo::CoreResponseFlag::NoRouteFound);
    return HeaderFilterStatus::StopIteration;
  }

  mayRequestStreamEnd(request.frameFlags().endStream());
  kickOffNewUpstreamRequest();
  return HeaderFilterStatus::StopIteration;
}

const Envoy::Router::MetadataMatchCriteria* RouterFilter::metadataMatchCriteria() {
  // Have we been called before? If so, there's no need to recompute.
  if (metadata_match_ != nullptr) {
    return metadata_match_.get();
  }

  const auto& request_metadata = callbacks_->streamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = request_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

  if (filter_it == request_metadata.end()) {
    return nullptr;
  }

  metadata_match_ = std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
  return metadata_match_.get();
}

const Network::Connection* RouterFilter::downstreamConnection() const {
  return callbacks_ != nullptr ? callbacks_->connection() : nullptr;
}

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
