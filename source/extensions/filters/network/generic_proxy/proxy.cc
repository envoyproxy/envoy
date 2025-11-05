#include "source/extensions/filters/network/generic_proxy/proxy.h"

#include <cstdint>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/route_impl.h"

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

StreamInfo::CoreResponseFlag flagFromDownstreamReasonReason(DownstreamStreamResetReason reason) {
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
    : parent_(parent), request_header_frame_(std::move(request)),
      stream_info_(parent_.time_source_,
                   parent_.callbacks_->connection().connectionInfoProviderSharedPtr(),
                   StreamInfo::FilterState::LifeSpan::FilterChain) {
  // Override the start time if the codec provides one.
  if (start_time.has_value()) {
    stream_info_.start_time_ = start_time.value().start_time;
    stream_info_.start_time_monotonic_ = start_time.value().start_time_monotonic;
  };

  if (!request_header_frame_->frameFlags().endStream()) {
    // If the request is not fully received, register the stream to the frame handler map.
    parent_.registerFrameHandler(requestStreamId(), this);
    waiting_request_frames_ = true;
  } else {
    // The request is fully received.
    stream_info_.downstreamTiming().onLastDownstreamRxByteReceived(parent_.time_source_);
  }

  parent_.stats_helper_.onRequest();

  conn_manager_tracing_config_ = parent_.config_->tracingConfig();

  auto tracer = parent_.config_->tracingProvider();

  if (!conn_manager_tracing_config_.has_value() || !tracer.has_value()) {
    return;
  }

  auto decision = tracingDecision(conn_manager_tracing_config_.value(), parent_.runtime_);
  if (decision.traced) {
    stream_info_.setTraceReason(decision.reason);
  }

  TraceContextBridge trace_context{*request_header_frame_};
  active_span_ = tracer->startSpan(*this, trace_context, stream_info_, decision);
}

Tracing::OperationName ActiveStream::operationName() const {
  ASSERT(conn_manager_tracing_config_.has_value());
  return conn_manager_tracing_config_->operationName();
}

void ActiveStream::modifySpan(Tracing::Span& span) const {
  ASSERT(conn_manager_tracing_config_.has_value());

  const TraceContextBridge trace_context{*request_header_frame_};
  const FormatterContextExtension context_extension(request_header_frame_.get(),
                                                    response_header_frame_.get());
  Formatter::Context context;
  context.setExtension(context_extension);
  const Tracing::CustomTagContext ctx{trace_context, stream_info_, context};

  for (const auto& it : conn_manager_tracing_config_->getCustomTags()) {
    it.second->applySpan(span, ctx);
  }
}

bool ActiveStream::verbose() const {
  ASSERT(conn_manager_tracing_config_.has_value());
  return conn_manager_tracing_config_->verbose();
}

uint32_t ActiveStream::maxPathTagLength() const {
  ASSERT(conn_manager_tracing_config_.has_value());
  return conn_manager_tracing_config_->maxPathTagLength();
}

bool ActiveStream::spawnUpstreamSpan() const {
  ASSERT(conn_manager_tracing_config_.has_value());
  return conn_manager_tracing_config_->spawnUpstreamSpan();
}

Envoy::Event::Dispatcher& ActiveStream::dispatcher() {
  return parent_.downstreamConnection().dispatcher();
}
const CodecFactory& ActiveStream::codecFactory() { return parent_.config_->codecFactory(); }

void ActiveStream::resetStream(DownstreamStreamResetReason reason) { completeStream(reason); }

bool ActiveStream::sendFrameToDownstream(const StreamFrame& frame, bool header_frame) {
  const bool end_stream = frame.frameFlags().endStream();

  const auto result = parent_.server_codec_->encode(frame, *this);
  if (!result.ok()) {
    ENVOY_LOG(error, "Generic proxy: response encoding failure: {}", result.status().message());
    resetStream(DownstreamStreamResetReason::ProtocolError);
    return false;
  }

  ENVOY_LOG(debug, "Generic proxy: send {} bytes to client, complete: {}", result.value(),
            end_stream);

  if (header_frame) {
    stream_info_.downstreamTiming().onFirstDownstreamTxByteSent(parent_.time_source_);
  }

  // If the request is fully sent, record the last downstream tx byte sent time and clean
  // up the stream.
  if (end_stream) {
    stream_info_.downstreamTiming().onLastDownstreamTxByteSent(parent_.time_source_);
    ASSERT(response_common_frames_.empty());
    completeStream();
  }
  return true;
}

void ActiveStream::sendLocalReply(Status status, absl::string_view data,
                                  ResponseUpdateFunction func) {
  // To ensure only one response header frame is sent for local reply.
  // If there is a previous response header but has not been sent to the downstream,
  // we should send the latest local reply to the downstream directly without any
  // filter processing.
  // If the previous response header has been sent to the downstream, it is an invalid
  // situation that we cannot handle. Then, we should reset the stream and return.
  // In other cases, we should continue the filter chain to process the local reply.

  const bool has_prev_response = response_header_frame_ != nullptr;
  const bool has_sent_response =
      has_prev_response && encoder_filter_iter_header_ == encoder_filters_.end();

  if (has_sent_response) {
    ENVOY_LOG(error, "Generic proxy: invalid local reply: response is already sent.");
    resetStream(DownstreamStreamResetReason::ProtocolError);
    return;
  }

  // Clear possible response frames anyway now.
  response_header_frame_ = nullptr;
  response_common_frames_.clear();

  // Create the local response frame.
  auto response = parent_.server_codec_->respond(status, data, *request_header_frame_);

  if (response == nullptr || !response->frameFlags().endStream()) {
    // This will be treated as protocol error.
    ENVOY_LOG(error, "Generic proxy: invalid local reply: null or not end stream.");
    resetStream(DownstreamStreamResetReason::ProtocolError);
    return;
  }

  if (func != nullptr) {
    func(*response);
  }

  response_header_frame_ = std::move(response);
  parent_.stream_drain_decision_ |= response_header_frame_->frameFlags().drainClose();
  local_reply_ = true;
  // status message will be used as response code details.
  stream_info_.setResponseCodeDetails(status.message());
  // Set the response code to the stream info.
  stream_info_.setResponseCode(response_header_frame_->status().code());

  if (has_prev_response) {
    // There was a previous response. Send the new response to the downstream directly.
    sendFrameToDownstream(*response_header_frame_, true);
    return;
  } else {
    continueEncoding();
  }
}

void ActiveStream::processRequestHeaderFrame() {
  ASSERT(request_header_frame_ != nullptr);
  if (decoder_filter_iter_header_ == decoder_filters_.end() || stream_reset_or_complete_) {
    // This header frame is already completed or the stream is reset.
    return;
  }

  while (decoder_filter_iter_header_ != decoder_filters_.end()) {
    const auto status =
        (*decoder_filter_iter_header_)->filter_->decodeHeaderFrame(*request_header_frame_);

    ENVOY_LOG(debug, "Generic proxy: decode header frame (filter: {} status: {} stream: {}).",
              (*decoder_filter_iter_header_)->filterConfigName(), static_cast<size_t>(status),
              stream_reset_or_complete_);

    if (stream_reset_or_complete_) {
      stop_decoder_filter_chain_ = true;
      return;
    }

    decoder_filter_iter_header_++;

    if (status == HeaderFilterStatus::StopIteration) {
      break;
    }
  }

  // Stop the filter chain if the filter returns StopIteration status and the filter is not
  // the terminal filter.
  // Note: the StopIteration status of terminal filter make no sense because it will be
  // treated as complete anyway.
  stop_decoder_filter_chain_ = decoder_filter_iter_header_ != decoder_filters_.end();
  if (stop_decoder_filter_chain_) {
    return;
  }
  ENVOY_LOG(debug, "Generic proxy: complete decoder filters for header frame (end_stream: {})",
            request_header_frame_->frameFlags().endStream());
}

void ActiveStream::processRequestCommonFrame() {
  ASSERT(request_common_frame_ != nullptr);
  if (decoder_filter_iter_common_ == decoder_filters_.end() || stream_reset_or_complete_) {
    // This common frame is already completed or the stream is reset.
    return;
  }

  while (decoder_filter_iter_common_ != decoder_filters_.end()) {
    const auto status =
        (*decoder_filter_iter_common_)->filter_->decodeCommonFrame(*request_common_frame_);

    ENVOY_LOG(debug, "Generic proxy: decode common frame (filter: {} status: {} stream: {}).",
              (*decoder_filter_iter_common_)->filterConfigName(), static_cast<size_t>(status),
              stream_reset_or_complete_);

    if (stream_reset_or_complete_) {
      stop_decoder_filter_chain_ = true;
      return;
    }

    decoder_filter_iter_common_++;

    if (status == CommonFilterStatus::StopIteration) {
      break;
    }
  }

  // Stop the filter chain if the filter returns StopIteration status and the filter is not
  // the terminal filter.
  // Note: the StopIteration status of terminal filter make no sense because it will be
  // treated as complete anyway.
  stop_decoder_filter_chain_ = decoder_filter_iter_common_ != decoder_filters_.end();
  if (stop_decoder_filter_chain_) {
    return;
  }

  ENVOY_LOG(debug, "Generic proxy: complete decoder filters for common frame (end_stream: {})",
            request_common_frame_->frameFlags().endStream());

  RequestCommonFramePtr local_common_frame = std::move(request_common_frame_);
  // Reset the iterator and frame for the next common frame.
  request_common_frame_ = nullptr;
  decoder_filter_iter_common_ = decoder_filters_.begin();

  // Transfer the active common frame to the request stream frame handler.
  if (request_stream_frames_handler_ != nullptr) {
    request_stream_frames_handler_->onRequestCommonFrame(std::move(local_common_frame));
    if (stream_reset_or_complete_) {
      stop_decoder_filter_chain_ = true;
    }
  }
}

void ActiveStream::processResponseHeaderFrame() {
  ASSERT(response_header_frame_ != nullptr);

  auto on_header_complete = [this]() {
    ENVOY_LOG(debug, "Generic proxy: complete encoder filters for header frame (end_stream: {})",
              response_header_frame_->frameFlags().endStream());

    // Send the header frame to downstream.
    if (!sendFrameToDownstream(*response_header_frame_, true)) {
      stop_encoder_filter_chain_ = true;
    }
  };

  // Handle the special case where no filter is added to the encoder filter chain.
  if (encoder_filters_.empty()) {
    // No filter is added to the encoder filter chain. Directly send the response header frame
    // to downstream.
    on_header_complete();
    return;
  }

  if (encoder_filter_iter_header_ == encoder_filters_.end() || stream_reset_or_complete_) {
    // This header frame is already completed or the stream is reset.
    return;
  }

  while (encoder_filter_iter_header_ != encoder_filters_.end()) {
    const auto status =
        (*encoder_filter_iter_header_)->filter_->encodeHeaderFrame(*response_header_frame_);

    ENVOY_LOG(debug, "Generic proxy: encode header frame (filter: {} status: {} stream: {}).",
              (*encoder_filter_iter_header_)->filterConfigName(), static_cast<size_t>(status),
              stream_reset_or_complete_);

    if (stream_reset_or_complete_) {
      stop_encoder_filter_chain_ = true;
      return;
    }

    encoder_filter_iter_header_++;

    if (status == HeaderFilterStatus::StopIteration) {
      break;
    }
  }

  // Stop the filter chain if the filter returns StopIteration status and the filter is not
  // the terminal filter.
  // Note: the StopIteration status of terminal filter make no sense because it will be
  // treated as complete anyway.
  stop_encoder_filter_chain_ = encoder_filter_iter_header_ != encoder_filters_.end();
  if (stop_encoder_filter_chain_) {
    return;
  }

  on_header_complete();
}

void ActiveStream::processResponseCommonFrame() {
  ASSERT(response_common_frame_ != nullptr);

  auto on_common_complete = [this]() {
    ENVOY_LOG(debug, "Generic proxy: complete encoder filters for common frame (end_stream: {})",
              response_common_frame_->frameFlags().endStream());

    auto local_common_frame = std::move(response_common_frame_);
    // Reset the iterator and frame for the next common frame.
    response_common_frame_ = nullptr;
    encoder_filter_iter_common_ = encoder_filters_.begin();

    // Send the common frame to downstream.
    if (!sendFrameToDownstream(*local_common_frame, false)) {
      stop_encoder_filter_chain_ = true;
    }
  };

  // Handle the special case where no filter is added to the encoder filter chain.
  if (encoder_filters_.empty()) {
    // No filter is added to the encoder filter chain. Directly send the response common frame
    // to downstream.
    on_common_complete();
    return;
  }

  if (encoder_filter_iter_common_ == encoder_filters_.end() || stream_reset_or_complete_) {
    // This common frame is already completed or the stream is reset.
    return;
  }

  for (; encoder_filter_iter_common_ < encoder_filters_.end();) {
    const auto status =
        (*encoder_filter_iter_common_)->filter_->encodeCommonFrame(*response_common_frame_);

    ENVOY_LOG(debug, "Generic proxy: encode common frame (filter: {} status: {} stream: {}).",
              (*encoder_filter_iter_common_)->filterConfigName(), static_cast<size_t>(status),
              stream_reset_or_complete_);

    if (stream_reset_or_complete_) {
      stop_encoder_filter_chain_ = true;
      return;
    }

    encoder_filter_iter_common_++;

    if (status == CommonFilterStatus::StopIteration) {
      break;
    }
  }

  // Stop the filter chain if the filter returns StopIteration status and the filter is not
  // the terminal filter.
  // Note: the StopIteration status of terminal filter make no sense because it will be
  // treated as complete anyway.
  stop_encoder_filter_chain_ = encoder_filter_iter_common_ != encoder_filters_.end();
  if (stop_encoder_filter_chain_) {
    return;
  }

  on_common_complete();
}

void ActiveStream::continueDecoding() {
  if (stream_reset_or_complete_) {
    return;
  }

  if (cached_route_entry_ == nullptr) {
    const MatchInput match_input(*request_header_frame_, stream_info_, MatchAction::RouteAction);
    cached_route_entry_ = parent_.config_->routeEntry(match_input);
    if (cached_route_entry_ != nullptr) {
      auto* cluster =
          parent_.cluster_manager_.getThreadLocalCluster(cached_route_entry_->clusterName());
      if (cluster != nullptr) {
        stream_info_.setUpstreamClusterInfo(cluster->info());
      }
    }
  }

  stop_decoder_filter_chain_ = false;

  // Handle the request header frame.
  processRequestHeaderFrame();
  if (stop_decoder_filter_chain_) {
    return;
  }

  // Handle the active request common frame first if exists.
  if (request_common_frame_ != nullptr) {
    processRequestCommonFrame();
  }

  // Handle the other request common frames if exists.
  while (!request_common_frames_.empty()) {
    // Check the stop flag first because the filter chain may be
    // stopped by the previous active request frame.
    if (stop_decoder_filter_chain_) {
      break;
    }

    ASSERT(request_common_frame_ == nullptr);
    ASSERT(decoder_filter_iter_common_ == decoder_filters_.begin());

    // Pop the first frame from the queue.
    auto frame = std::move(request_common_frames_.front());
    request_common_frames_.pop_front();
    request_common_frame_ = std::move(frame);

    processRequestCommonFrame();
  }
}

void ActiveStream::onRequestCommonFrame(RequestCommonFramePtr request_common_frame) {
  if (request_common_frame->frameFlags().endStream()) {
    // The request is fully received. Record the last downstream rx byte received time.
    stream_info_.downstreamTiming().onLastDownstreamRxByteReceived(parent_.time_source_);

    // Unregister the stream from the frame handler map to not expect any more frames.
    parent_.unregisterFrameHandler(requestStreamId());
    waiting_request_frames_ = false;
  }

  if (stop_decoder_filter_chain_) {
    request_common_frames_.emplace_back(std::move(request_common_frame));
  } else {
    // The active stream always try to send all the frames to the upstream. If the
    // stop_decoder_filter_chain_ is false, it means all the previous frames are already
    // sent to the upstream. So, we can process the new frame directly.

    ASSERT(decoder_filter_iter_common_ == decoder_filters_.begin());
    ASSERT(request_common_frame_ == nullptr);
    request_common_frame_ = std::move(request_common_frame);
    processRequestCommonFrame();
  }
}

void ActiveStream::onResponseHeaderFrame(ResponsePtr response) {
  // To ensure only one response header frame is handled for upstream response.
  // If there is a previous response header frame, no matter it is come from
  // the upstream or the local reply, it is an invalid situation that we cannot
  // handle. Then, we should reset the stream and return.
  const bool has_prev_response = response_header_frame_ != nullptr;
  if (has_prev_response) {
    ENVOY_LOG(error, "Generic proxy: invalid response: has previous response.");
    resetStream(DownstreamStreamResetReason::ProtocolError);
    return;
  }

  response_header_frame_ = std::move(response);
  parent_.stream_drain_decision_ |= response_header_frame_->frameFlags().drainClose();

  // The response code details always be "via_upstream" for response from upstream.
  stream_info_.setResponseCodeDetails("via_upstream");
  // Set the response code to the stream info.
  stream_info_.setResponseCode(response_header_frame_->status().code());

  continueEncoding();
}

void ActiveStream::onResponseCommonFrame(ResponseCommonFramePtr response_common_frame) {
  if (stop_encoder_filter_chain_) {
    response_common_frames_.emplace_back(std::move(response_common_frame));
  } else {
    // The active stream always try to send all the frames to the downstream.
    // If the stop_encoder_filter_chain_ is false, it means all the previous
    // frames are already sent to the downstream. So, we can process the new
    // frame directly.

    ASSERT(encoder_filter_iter_common_ == encoder_filters_.begin());
    ASSERT(response_common_frame_ == nullptr);
    response_common_frame_ = std::move(response_common_frame);
    processResponseCommonFrame();
  }
}

const Network::Connection* ActiveStream::ActiveFilterBase::connection() const {
  return &parent_.parent_.downstreamConnection();
}

void ActiveStream::continueEncoding() {
  if (stream_reset_or_complete_ || response_header_frame_ == nullptr) {
    return;
  }

  stop_encoder_filter_chain_ = false;

  // Handle the response header frame.
  processResponseHeaderFrame();
  if (stop_encoder_filter_chain_) {
    return;
  }

  // Handle the active response common frame first if exists.
  if (response_common_frame_ != nullptr) {
    processResponseCommonFrame();
  }

  // Handle the other response common frames if exists.
  while (!response_common_frames_.empty()) {
    // Check the stop flag first because the filter chain may be
    // stopped by the previous active response frame.
    if (stop_encoder_filter_chain_) {
      break;
    }

    ASSERT(response_common_frame_ == nullptr);
    ASSERT(encoder_filter_iter_common_ == encoder_filters_.begin());

    // Pop the first frame from the queue.
    auto frame = std::move(response_common_frames_.front());
    response_common_frames_.pop_front();
    response_common_frame_ = std::move(frame);

    processResponseCommonFrame();
  }
}

bool ActiveStream::initializeFilterChain(FilterChainFactory& factory) {
  factory.createFilterChain(*this);
  // Reverse the encoder filter chain so that the first encoder filter is the last filter in the
  // chain.
  std::reverse(encoder_filters_.begin(), encoder_filters_.end());

  if (decoder_filters_.empty()) {
    ENVOY_LOG(error, "Generic proxy: as least on terminal decoder filter should be added.");
    return false;
  }

  decoder_filter_iter_header_ = decoder_filters_.begin();
  decoder_filter_iter_common_ = decoder_filters_.begin();

  encoder_filter_iter_header_ = encoder_filters_.begin();
  encoder_filter_iter_common_ = encoder_filters_.begin();
  return true;
}

void ActiveStream::deferredDelete() {
  if (inserted()) {
    parent_.callbacks_->connection().dispatcher().deferredDelete(
        removeFromList(parent_.active_streams_));
  }
}

void ActiveStream::completeStream(absl::optional<DownstreamStreamResetReason> reason) {
  if (stream_reset_or_complete_) {
    return;
  }
  stream_reset_or_complete_ = true;

  if (reason.has_value()) {
    parent_.stats_helper_.onRequestReset();
    stream_info_.setResponseFlag(flagFromDownstreamReasonReason(reason.value()));
  }

  deferredDelete();

  if (waiting_request_frames_) {
    parent_.unregisterFrameHandler(requestStreamId());
    waiting_request_frames_ = false;
  }

  stream_info_.onRequestComplete();

  bool error_reply = false;
  // This response frame may be nullptr if the request is one-way or the response is not
  // sent to the downstream.
  if (response_header_frame_ != nullptr) {
    error_reply = !response_header_frame_->status().ok();
  }
  parent_.stats_helper_.onRequestComplete(stream_info_, local_reply_, error_reply);

  if (active_span_) {
    Tracing::TracerUtility::finalizeSpan(*active_span_, stream_info_, *this, false);
  }

  for (const auto& access_log : parent_.config_->accessLogs()) {
    const FormatterContextExtension context_extension(request_header_frame_.get(),
                                                      response_header_frame_.get());
    Formatter::Context context;
    access_log->log(context.setExtension(context_extension), stream_info_);
  }

  // TODO(wbpcode): use ranges to simplify the code.
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
  if (request_header_frame == nullptr) {
    ENVOY_LOG(error, "generic proxy: request header frame from codec is null");
    onDecodingFailure();
    return;
  }

  const uint64_t stream_id = request_header_frame->frameFlags().streamId();

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
  if (request_common_frame == nullptr) {
    ENVOY_LOG(error, "generic proxy: request common frame from codec is null");
    onDecodingFailure();
    return;
  }

  const uint64_t stream_id = request_common_frame->frameFlags().streamId();
  // One existing stream expects this frame.
  if (auto iter = frame_handlers_.find(stream_id); iter != frame_handlers_.end()) {
    iter->second->onRequestCommonFrame(std::move(request_common_frame));
    return;
  }

  // No existing stream expects this non-leading frame. It should not happen.
  // We treat it as request decoding failure.
  ENVOY_LOG(error, "generic proxy: id {} not found for stream frame", stream_id);
  onDecodingFailure("unknown stream id");
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

void Filter::registerFrameHandler(uint64_t stream_id, ActiveStream* raw_stream) {
  // Repeated stream id will be detected in the onDecodingSuccess method.
  ASSERT(frame_handlers_.find(stream_id) == frame_handlers_.end());
  frame_handlers_[stream_id] = raw_stream;
}

void Filter::unregisterFrameHandler(uint64_t stream_id) { frame_handlers_.erase(stream_id); }

void Filter::newDownstreamRequest(StreamRequestPtr request, absl::optional<StartTime> start_time) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request), start_time);
  auto raw_stream = stream.get();
  LinkedList::moveIntoList(std::move(stream), active_streams_);

  // Initialize filter chain.
  if (!raw_stream->initializeFilterChain(*config_)) {
    raw_stream->resetStream(DownstreamStreamResetReason::LocalConnectionTermination);
    return;
  }

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
  if ((drain_decision_.drainClose(Network::DrainDirection::All) || stream_drain_decision_) &&
      active_streams_.empty()) {
    onDrainCloseAndNoActiveStreams();
  }
}

// Default implementation for connection draining.
void Filter::onDrainCloseAndNoActiveStreams() { closeDownstreamConnection(); }

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
