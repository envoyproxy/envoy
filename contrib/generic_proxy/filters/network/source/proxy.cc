#include "contrib/generic_proxy/filters/network/source/proxy.h"

#include <cstdint>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

namespace {

Tracing::Decision tracingDecision(const Tracing::ConnectionManagerTracingConfig& tracing_config,
                                  Runtime::Loader& runtime) {
  bool traced = runtime.snapshot().featureEnabled("tracing.random_sampling",
                                                  tracing_config.getRandomSampling());

  if (traced) {
    return {Tracing::Reason::Sampling, true};
  }
  return {Tracing::Reason::NotTraceable, false};
}

StreamInfo::CoreResponseFlag
responseFlagFromDownstreamReasonReason(DownstreamStreamResetReason reason) {
  switch (reason) {
  case DownstreamStreamResetReason::ConnectionTermination:
    return StreamInfo::CoreResponseFlag::DownstreamConnectionTermination;
  case DownstreamStreamResetReason::LocalConnectionTermination:
    return StreamInfo::CoreResponseFlag::LocalReset;
  case DownstreamStreamResetReason::ProtocolError:
    return StreamInfo::CoreResponseFlag::DownstreamProtocolError;
  }
  PANIC("Unknown reset reason");
}

} // namespace

ActiveStream::ActiveStream(Filter& parent, RequestHeaderFramePtr request,
                           absl::optional<StartTime> start_time)
    : parent_(parent), request_stream_(std::move(request)),
      request_stream_end_(request_stream_->frameFlags().endStream()),
      stream_info_(parent_.time_source_,
                   parent_.callbacks_->connection().connectionInfoProviderSharedPtr(),
                   StreamInfo::FilterState::LifeSpan::FilterChain) {
  // Override the start time if the codec provides one.
  if (start_time.has_value()) {
    stream_info_.start_time_ = start_time.value().start_time;
    stream_info_.start_time_monotonic_ = start_time.value().start_time_monotonic;
  };

  if (!request_stream_end_) {
    // If the request is not fully received, register the stream to the frame handler map.
    parent_.registerFrameHandler(requestStreamId(), this);
    registered_in_frame_handlers_ = true;
  } else {
    // The request is fully received.
    stream_info_.downstreamTiming().onLastDownstreamRxByteReceived(parent_.time_source_);
  }

  parent_.stats_helper_.onRequest();

  connection_manager_tracing_config_ = parent_.config_->tracingConfig();

  auto tracer = parent_.config_->tracingProvider();

  if (!connection_manager_tracing_config_.has_value() || !tracer.has_value()) {
    return;
  }

  auto decision = tracingDecision(connection_manager_tracing_config_.value(), parent_.runtime_);
  if (decision.traced) {
    stream_info_.setTraceReason(decision.reason);
  }

  TraceContextBridge trace_context{*request_stream_};
  active_span_ = tracer->startSpan(*this, trace_context, stream_info_, decision);
}

Tracing::OperationName ActiveStream::operationName() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->operationName();
}

const Tracing::CustomTagMap* ActiveStream::customTags() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return &connection_manager_tracing_config_->getCustomTags();
}

bool ActiveStream::verbose() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->verbose();
}

uint32_t ActiveStream::maxPathTagLength() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->maxPathTagLength();
}

bool ActiveStream::spawnUpstreamSpan() const {
  ASSERT(connection_manager_tracing_config_.has_value());
  return connection_manager_tracing_config_->spawnUpstreamSpan();
}

Envoy::Event::Dispatcher& ActiveStream::dispatcher() {
  return parent_.downstreamConnection().dispatcher();
}
const CodecFactory& ActiveStream::codecFactory() { return parent_.config_->codecFactory(); }
void ActiveStream::resetStream(DownstreamStreamResetReason reason) {
  if (active_stream_reset_) {
    return;
  }
  active_stream_reset_ = true;

  parent_.stats_helper_.onRequestReset();
  stream_info_.setResponseFlag(responseFlagFromDownstreamReasonReason(reason));

  completeRequest();
}

void ActiveStream::sendResponseStartToDownstream() {
  ASSERT(response_stream_ != nullptr);
  response_filter_chain_complete_ = true;
  // The first frame of response is sent.
  stream_info_.downstreamTiming().onFirstDownstreamTxByteSent(parent_.time_source_);
  parent_.sendFrameToDownstream(*response_stream_, *this);
}

void ActiveStream::sendResponseFrameToDownstream() {
  if (!response_filter_chain_complete_) {
    // Wait for the response header frame to be sent first. It may be blocked by
    // the filter chain.
    return;
  }

  while (!response_stream_frames_.empty()) {
    // Pop the first frame from the queue.
    auto frame = std::move(response_stream_frames_.front());
    response_stream_frames_.pop_front();

    // Send the frame to downstream.
    parent_.sendFrameToDownstream(*frame, *this);
  }
}

void ActiveStream::sendRequestFrameToUpstream() {
  if (!request_filter_chain_complete_) {
    // Wait for the request header frame to be sent first. It may be blocked by
    // the filter chain.
    return;
  }

  if (request_stream_frames_handler_ == nullptr) {
    // The request stream frame handler is not ready yet.
    return;
  }

  while (!request_stream_frames_.empty()) {
    // Pop the first frame from the queue.
    auto frame = std::move(request_stream_frames_.front());
    request_stream_frames_.pop_front();

    // Send the frame to upstream.
    request_stream_frames_handler_->onRequestCommonFrame(std::move(frame));
  }
}

// TODO(wbpcode): add the short_response_flags support to the sendLocalReply
// method.
void ActiveStream::sendLocalReply(Status status, absl::string_view data,
                                  ResponseUpdateFunction func) {
  response_stream_ = parent_.server_codec_->respond(status, data, *request_stream_);
  response_stream_frames_.clear();
  // Only one frame is allowed in the local reply.
  response_stream_end_ = true;

  ASSERT(response_stream_ != nullptr);

  if (func != nullptr) {
    func(*response_stream_);
  }

  local_reply_ = true;
  // status message will be used as response code details.
  stream_info_.setResponseCodeDetails(status.message());
  // Set the response code to the stream info.
  stream_info_.setResponseCode(response_stream_->status().code());

  sendResponseStartToDownstream();
}

void ActiveStream::continueDecoding() {
  if (active_stream_reset_ || request_stream_ == nullptr) {
    return;
  }

  if (cached_route_entry_ == nullptr) {
    const MatchInput match_input(*request_stream_, stream_info_, MatchAction::RouteAction);
    cached_route_entry_ = parent_.config_->routeEntry(match_input);
    if (cached_route_entry_ != nullptr) {
      auto* cluster =
          parent_.cluster_manager_.getThreadLocalCluster(cached_route_entry_->clusterName());
      if (cluster != nullptr) {
        stream_info_.setUpstreamClusterInfo(cluster->info());
      }
    }
  }

  ASSERT(request_stream_ != nullptr);
  for (; next_decoder_filter_index_ < decoder_filters_.size();) {
    auto status =
        decoder_filters_[next_decoder_filter_index_]->filter_->onStreamDecoded(*request_stream_);
    next_decoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }
  if (next_decoder_filter_index_ == decoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
    request_filter_chain_complete_ = true;
    sendRequestFrameToUpstream();
  }
}

void ActiveStream::onRequestFrame(RequestCommonFramePtr request_common_frame) {
  request_stream_end_ = request_common_frame->frameFlags().endStream();
  request_stream_frames_.emplace_back(std::move(request_common_frame));

  ASSERT(registered_in_frame_handlers_);
  if (request_stream_end_) {
    // The request is fully received.
    stream_info_.downstreamTiming().onLastDownstreamRxByteReceived(parent_.time_source_);

    // If the request is fully received, remove the stream from the
    // frame handler map.
    parent_.unregisterFrameHandler(requestStreamId());
    registered_in_frame_handlers_ = false;
  }

  // Try to send the frame to upstream immediately.
  sendRequestFrameToUpstream();
}

void ActiveStream::onResponseStart(ResponsePtr response) {
  ASSERT(response_stream_ == nullptr);
  response_stream_ = std::move(response);
  ASSERT(response_stream_ != nullptr);
  response_stream_end_ = response_stream_->frameFlags().endStream();
  parent_.stream_drain_decision_ = response_stream_->frameFlags().streamFlags().drainClose();

  // The response code details always be "via_upstream" for response from upstream.
  stream_info_.setResponseCodeDetails("via_upstream");
  // Set the response code to the stream info.
  stream_info_.setResponseCode(response_stream_->status().code());
  continueEncoding();
}

void ActiveStream::onResponseFrame(ResponseCommonFramePtr response_common_frame) {
  response_stream_end_ = response_common_frame->frameFlags().endStream();
  response_stream_frames_.emplace_back(std::move(response_common_frame));
  // Try to send the frame to downstream immediately.
  sendResponseFrameToDownstream();
}

void ActiveStream::completeDirectly() {
  response_stream_end_ = true;
  completeRequest();
};

const Network::Connection* ActiveStream::ActiveFilterBase::connection() const {
  return &parent_.parent_.downstreamConnection();
}

void ActiveStream::continueEncoding() {
  if (active_stream_reset_ || response_stream_ == nullptr) {
    return;
  }

  ASSERT(response_stream_ != nullptr);
  for (; next_encoder_filter_index_ < encoder_filters_.size();) {
    auto status =
        encoder_filters_[next_encoder_filter_index_]->filter_->onStreamEncoded(*response_stream_);
    next_encoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }

  if (next_encoder_filter_index_ == encoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete encoder filters");
    sendResponseStartToDownstream();
    sendResponseFrameToDownstream();
  }
}

void ActiveStream::onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) {
  ASSERT(parent_.downstreamConnection().state() == Network::Connection::State::Open);
  parent_.downstreamConnection().write(buffer, false);

  if (!end_stream) {
    return;
  }

  // The response is fully sent.
  stream_info_.downstreamTiming().onLastDownstreamTxByteSent(parent_.time_source_);

  ENVOY_LOG(debug, "Generic proxy: downstream response complete");

  ASSERT(response_stream_end_);
  ASSERT(response_stream_frames_.empty());

  completeRequest();
}

void ActiveStream::onEncodingFailure(absl::string_view reason) {
  ENVOY_LOG(error, "Generic proxy: response encoding failure: {}", reason);
  resetStream(DownstreamStreamResetReason::ProtocolError);
}

void ActiveStream::initializeFilterChain(FilterChainFactory& factory) {
  factory.createFilterChain(*this);
  // Reverse the encoder filter chain so that the first encoder filter is the last filter in the
  // chain.
  std::reverse(encoder_filters_.begin(), encoder_filters_.end());
}

void ActiveStream::deferredDelete() {
  if (inserted()) {
    parent_.callbacks_->connection().dispatcher().deferredDelete(
        removeFromList(parent_.active_streams_));
  }
}

void ActiveStream::completeRequest() {
  deferredDelete();

  if (registered_in_frame_handlers_) {
    parent_.unregisterFrameHandler(requestStreamId());
    registered_in_frame_handlers_ = false;
  }

  stream_info_.onRequestComplete();

  bool error_reply = false;
  // This response frame may be nullptr if the request is one-way.
  if (response_stream_ != nullptr) {
    error_reply = !response_stream_->status().ok();
  }
  parent_.stats_helper_.onRequestComplete(stream_info_, local_reply_, error_reply);

  if (active_span_) {
    TraceContextBridge trace_context{*request_stream_};
    Tracing::TracerUtility::finalizeSpan(*active_span_, trace_context, stream_info_, *this, false);
  }

  for (const auto& access_log : parent_.config_->accessLogs()) {
    access_log->log({request_stream_.get(), response_stream_.get()}, stream_info_);
  }

  for (auto& filter : decoder_filters_) {
    filter->filter_->onDestroy();
  }
  for (auto& filter : encoder_filters_) {
    if (filter->isDualFilter()) {
      continue;
    }
    filter->filter_->onDestroy();
  }

  parent_.mayBeDrainClose();
}

Envoy::Network::FilterStatus Filter::onData(Envoy::Buffer::Instance& data, bool end_stream) {
  if (downstream_connection_closed_) {
    return Envoy::Network::FilterStatus::StopIteration;
  }

  // Basically this end_stream should always be false because we don't support half-close.
  server_codec_->decode(data, end_stream);
  return Envoy::Network::FilterStatus::StopIteration;
}

void Filter::onDecodingSuccess(RequestHeaderFramePtr request_header_frame,
                               absl::optional<StartTime> start_time) {
  const uint64_t stream_id = request_header_frame->frameFlags().streamFlags().streamId();

  if (!frame_handlers_.empty()) { // Quick empty check to avoid the map lookup.
    if (auto iter = frame_handlers_.find(stream_id); iter != frame_handlers_.end()) {
      ENVOY_LOG(error, "generic proxy: repetitive stream id: {} at same time", stream_id);
      onDecodingFailure();
      return;
    }
  }

  newDownstreamRequest(std::move(request_header_frame), std::move(start_time));
}

void Filter::onDecodingSuccess(RequestCommonFramePtr request_common_frame) {
  const uint64_t stream_id = request_common_frame->frameFlags().streamFlags().streamId();
  // One existing stream expects this frame.
  if (auto iter = frame_handlers_.find(stream_id); iter != frame_handlers_.end()) {
    iter->second->onRequestFrame(std::move(request_common_frame));
    return;
  }

  // No existing stream expects this non-leading frame. It should not happen.
  // We treat it as request decoding failure.
  ENVOY_LOG(error, "generic proxy: id {} not found for stream frame", stream_id);
  onDecodingFailure();
}

void Filter::onDecodingFailure(absl::string_view reason) {
  ENVOY_LOG(error, "generic proxy: request decoding failure: {}", reason);
  stats_helper_.onRequestDecodingError();
  resetDownstreamAllStreams(DownstreamStreamResetReason::ProtocolError);
  closeDownstreamConnection();
}

void Filter::writeToConnection(Buffer::Instance& buffer) {
  if (downstream_connection_closed_) {
    return;
  }
  downstreamConnection().write(buffer, false);
}

OptRef<Network::Connection> Filter::connection() {
  if (downstream_connection_closed_) {
    return {};
  }
  return {downstreamConnection()};
}

void Filter::sendFrameToDownstream(StreamFrame& frame, EncodingCallbacks& callbacks) {
  server_codec_->encode(frame, callbacks);
}

void Filter::registerFrameHandler(uint64_t stream_id, ActiveStream* raw_stream) {
  // If the stream expects variable length frames, then add it to the frame
  // handler map.
  // This map entry will be removed when the request or response end frame is
  // received.
  if (frame_handlers_.contains(stream_id)) {
    ENVOY_LOG(error, "generic proxy: repetitive stream id: {} at same time", stream_id);
    onDecodingFailure();
    return;
  }
  frame_handlers_[stream_id] = raw_stream;
}

void Filter::unregisterFrameHandler(uint64_t stream_id) { frame_handlers_.erase(stream_id); }

void Filter::newDownstreamRequest(StreamRequestPtr request, absl::optional<StartTime> start_time) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request), start_time);
  auto raw_stream = stream.get();
  LinkedList::moveIntoList(std::move(stream), active_streams_);

  // Initialize filter chain.
  raw_stream->initializeFilterChain(*config_);
  // Start request.
  raw_stream->continueDecoding();
}

void Filter::resetDownstreamAllStreams(DownstreamStreamResetReason reason) {
  while (!active_streams_.empty()) {
    auto* stream = active_streams_.front().get();
    // Remove the stream from the active stream list by the filter self. Although the
    // resetStream() method will do the same thing. But doing it here could ensure the
    // stream is removed even if the resetStream() method is not working as expected.
    // This could ensure this never fall into infinite loop.
    stream->deferredDelete();
    stream->resetStream(reason);
  }
}

void Filter::closeDownstreamConnection() {
  if (downstream_connection_closed_) {
    return;
  }
  downstream_connection_closed_ = true;
  downstreamConnection().close(Network::ConnectionCloseType::FlushWrite);
}

void Filter::mayBeDrainClose() {
  if ((drain_decision_.drainClose() || stream_drain_decision_) && active_streams_.empty()) {
    onDrainCloseAndNoActiveStreams();
  }
}

// Default implementation for connection draining.
void Filter::onDrainCloseAndNoActiveStreams() { closeDownstreamConnection(); }

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
