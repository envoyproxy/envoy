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

} // namespace

ActiveStream::ActiveStream(Filter& parent, StreamRequestPtr request)
    : parent_(parent), request_stream_(std::move(request)),
      request_stream_end_(request_stream_->frameFlags().endStream()),
      stream_info_(parent_.time_source_,
                   parent_.callbacks_->connection().connectionInfoProviderSharedPtr()),
      request_timer_(new Stats::HistogramCompletableTimespanImpl(parent_.stats_.request_time_ms_,
                                                                 parent_.time_source_)) {
  if (!request_stream_end_) {
    // If the request is not fully received, register the stream to the frame handler map.
    parent_.registerFrameHandler(requestStreamId(), this);
    registered_in_frame_handlers_ = true;
  }

  parent_.stats_.request_.inc();
  parent_.stats_.request_active_.inc();

  connection_manager_tracing_config_ = parent_.config_->tracingConfig();

  auto tracer = parent_.config_->tracingProvider();

  if (!connection_manager_tracing_config_.has_value() || !tracer.has_value()) {
    return;
  }

  auto decision = tracingDecision(connection_manager_tracing_config_.value(), parent_.runtime_);
  if (decision.traced) {
    stream_info_.setTraceReason(decision.reason);
  }
  active_span_ = tracer->startSpan(*this, *request_stream_, stream_info_, decision);
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
const CodecFactory& ActiveStream::downstreamCodec() { return parent_.config_->codecFactory(); }
void ActiveStream::resetStream() {
  if (active_stream_reset_) {
    return;
  }
  active_stream_reset_ = true;
  parent_.deferredStream(*this);
}

void ActiveStream::sendResponseStartToDownstream() {
  ASSERT(response_stream_ != nullptr);
  response_filter_chain_complete_ = true;

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

  if (request_stream_frame_handler_ == nullptr) {
    // The request stream frame handler is not ready yet.
    return;
  }

  while (!request_stream_frames_.empty()) {
    // Pop the first frame from the queue.
    auto frame = std::move(request_stream_frames_.front());
    request_stream_frames_.pop_front();

    // Send the frame to upstream.
    request_stream_frame_handler_->onStreamFrame(std::move(frame));
  }
}

void ActiveStream::sendLocalReply(Status status, ResponseUpdateFunction&& func) {
  response_stream_ = parent_.message_creator_->response(status, *request_stream_);
  response_stream_frames_.clear();
  // Only one frame is allowed in the local reply.
  response_stream_end_ = true;

  ASSERT(response_stream_ != nullptr);

  if (func != nullptr) {
    func(*response_stream_);
  }

  sendResponseStartToDownstream();
}

void ActiveStream::continueDecoding() {
  if (active_stream_reset_ || request_stream_ == nullptr) {
    return;
  }

  if (cached_route_entry_ == nullptr) {
    cached_route_entry_ = parent_.config_->routeEntry(*request_stream_);
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

void ActiveStream::onRequestFrame(StreamFramePtr frame) {
  request_stream_end_ = frame->frameFlags().endStream();
  request_stream_frames_.emplace_back(std::move(frame));

  ASSERT(registered_in_frame_handlers_);
  if (request_stream_end_) {
    // If the request is fully received, remove the stream from the
    // frame handler map.
    parent_.unregisterFrameHandler(requestStreamId());
    registered_in_frame_handlers_ = false;
  }

  // Try to send the frame to upstream immediately.
  sendRequestFrameToUpstream();
}

void ActiveStream::onResponseStart(ResponsePtr response) {
  response_stream_ = std::move(response);
  response_stream_end_ = response_stream_->frameFlags().endStream();
  parent_.stream_drain_decision_ = response_stream_->frameFlags().streamFlags().drainClose();
  continueEncoding();
}

void ActiveStream::onResponseFrame(StreamFramePtr frame) {
  response_stream_end_ = frame->frameFlags().endStream();
  response_stream_frames_.emplace_back(std::move(frame));
  // Try to send the frame to downstream immediately.
  sendResponseFrameToDownstream();
}

void ActiveStream::completeDirectly() {
  response_stream_end_ = true;
  parent_.deferredStream(*this);
};

void ActiveStream::ActiveDecoderFilter::bindUpstreamConn(Upstream::TcpPoolData&& pool_data) {
  parent_.parent_.bindUpstreamConn(std::move(pool_data));
}
OptRef<UpstreamManager> ActiveStream::ActiveDecoderFilter::boundUpstreamConn() {
  return parent_.parent_.boundUpstreamConn();
}

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

  ENVOY_LOG(debug, "Generic proxy: downstream response complete");

  ASSERT(response_stream_end_);
  ASSERT(response_stream_frames_.empty());

  parent_.stats_.response_.inc();
  parent_.deferredStream(*this);
}

void ActiveStream::initializeFilterChain(FilterChainFactory& factory) {
  factory.createFilterChain(*this);
  // Reverse the encoder filter chain so that the first encoder filter is the last filter in the
  // chain.
  std::reverse(encoder_filters_.begin(), encoder_filters_.end());
}

void ActiveStream::completeRequest() {
  if (registered_in_frame_handlers_) {
    parent_.unregisterFrameHandler(requestStreamId());
    registered_in_frame_handlers_ = false;
  }

  stream_info_.onRequestComplete();

  request_timer_->complete();
  parent_.stats_.request_active_.dec();

  if (active_span_) {
    Tracing::TracerUtility::finalizeSpan(*active_span_, *request_stream_, stream_info_, *this,
                                         false);
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
}

UpstreamManagerImpl::UpstreamManagerImpl(Filter& parent, Upstream::TcpPoolData&& tcp_pool_data)
    : UpstreamConnection(std::move(tcp_pool_data),
                         parent.config_->codecFactory().responseDecoder()),
      parent_(parent) {
  response_decoder_->setDecoderCallback(*this);
}

void UpstreamManagerImpl::registerResponseCallback(uint64_t stream_id,
                                                   PendingResponseCallback& cb) {
  // The stream id is already registered and it should not happen. We treat it as
  // request decoding failure.
  // All pending streams will be reset and downstream connection will be closed and
  // bound upstream connection will be cleaned up.
  if (registered_response_callbacks_.find(stream_id) != registered_response_callbacks_.end()) {
    ENVOY_LOG(error, "generic proxy: stream_id {} already registered", stream_id);
    parent_.onDecodingFailure();
    return;
  }

  registered_response_callbacks_[stream_id] = &cb;
}

void UpstreamManagerImpl::registerUpstreamCallback(uint64_t stream_id,
                                                   UpstreamBindingCallback& cb) {
  // Connection is already bound to a downstream connection and use it directly.
  if (owned_conn_data_ != nullptr) {
    cb.onBindSuccess(owned_conn_data_->connection(), upstream_host_);
    return;
  }

  // The stream id is already registered and it should not happen. We treat it as
  // request decoding failure.
  // All pending streams will be reset and downstream connection will be closed and
  // bound upstream connection will be cleaned up.
  if (registered_upstream_callbacks_.find(stream_id) != registered_upstream_callbacks_.end()) {
    ENVOY_LOG(error, "generic proxy: stream_id {} already registered", stream_id);
    parent_.onDecodingFailure();
    return;
  }

  registered_upstream_callbacks_[stream_id] = &cb;
}

void UpstreamManagerImpl::unregisterResponseCallback(uint64_t stream_id) {
  registered_response_callbacks_.erase(stream_id);
}

void UpstreamManagerImpl::unregisterUpstreamCallback(uint64_t stream_id) {
  registered_upstream_callbacks_.erase(stream_id);
}

void UpstreamManagerImpl::onEventImpl(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }

  // If the connection event is consumed by this upstream manager, it means that
  // the upstream connection is ready and onPoolReady()/onPoolSuccessImpl() have
  // been called. So the registered upstream callbacks should be empty.
  ASSERT(registered_upstream_callbacks_.empty());

  while (!registered_response_callbacks_.empty()) {
    auto it = registered_response_callbacks_.begin();
    auto cb = it->second;
    registered_response_callbacks_.erase(it);

    cb->onConnectionClose(event);
  }

  parent_.onBoundUpstreamConnectionEvent(event);
}

void UpstreamManagerImpl::onPoolSuccessImpl() {
  ASSERT(registered_response_callbacks_.empty());

  while (!registered_upstream_callbacks_.empty()) {
    auto iter = registered_upstream_callbacks_.begin();
    auto* cb = iter->second;
    registered_upstream_callbacks_.erase(iter);

    cb->onBindSuccess(owned_conn_data_->connection(), upstream_host_);
  }
}

void UpstreamManagerImpl::onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                            absl::string_view transport_failure_reason) {
  ASSERT(registered_response_callbacks_.empty());

  while (!registered_upstream_callbacks_.empty()) {
    auto iter = registered_upstream_callbacks_.begin();
    auto* cb = iter->second;
    registered_upstream_callbacks_.erase(iter);

    cb->onBindFailure(reason, transport_failure_reason, upstream_host_);
  }

  parent_.onBoundUpstreamConnectionEvent(Network::ConnectionEvent::RemoteClose);
}

void UpstreamManagerImpl::onDecodingSuccess(StreamFramePtr response) {
  // registered_upstream_callbacks_ should be empty because after upstream connection is ready.
  ASSERT(registered_upstream_callbacks_.empty());

  const uint64_t stream_id = response->frameFlags().streamFlags().streamId();
  const bool end_stream = response->frameFlags().endStream();

  auto it = registered_response_callbacks_.find(stream_id);
  if (it == registered_response_callbacks_.end()) {
    ENVOY_LOG(error, "generic proxy: id {} not found for frame", stream_id);
    return;
  }

  auto cb = it->second;

  // If the response is end, remove the callback from the map.
  if (end_stream) {
    registered_response_callbacks_.erase(it);
  }

  return cb->onDecodingSuccess(std::move(response));
}

void UpstreamManagerImpl::onDecodingFailure() {
  // registered_upstream_callbacks_ should be empty because after upstream connection is ready.
  ASSERT(registered_upstream_callbacks_.empty());

  ENVOY_LOG(error, "generic proxy bound upstream manager: decoding failure");

  parent_.stats_.response_decoding_error_.inc();

  while (!registered_response_callbacks_.empty()) {
    auto it = registered_response_callbacks_.begin();
    auto cb = it->second;
    registered_response_callbacks_.erase(it);

    cb->onDecodingFailure();
  }

  // Close both the upstream and downstream connections by this call.
  parent_.onBoundUpstreamConnectionEvent(Network::ConnectionEvent::RemoteClose);
}

void UpstreamManagerImpl::writeToConnection(Buffer::Instance& buffer) {
  if (is_cleaned_up_) {
    return;
  }

  if (owned_conn_data_ != nullptr) {
    ASSERT(owned_conn_data_->connection().state() == Network::Connection::State::Open);
    owned_conn_data_->connection().write(buffer, false);
  }
}

OptRef<Network::Connection> UpstreamManagerImpl::connection() {
  if (is_cleaned_up_) {
    return {};
  }
  if (owned_conn_data_ != nullptr) {
    return {owned_conn_data_->connection()};
  }
  return {};
}

Envoy::Network::FilterStatus Filter::onData(Envoy::Buffer::Instance& data, bool) {
  if (downstream_connection_closed_) {
    return Envoy::Network::FilterStatus::StopIteration;
  }

  request_decoder_->decode(data);
  return Envoy::Network::FilterStatus::StopIteration;
}

void Filter::onDecodingSuccess(StreamFramePtr request) {
  const uint64_t stream_id = request->frameFlags().streamFlags().streamId();
  // One existing stream expects this frame.
  if (auto iter = frame_handlers_.find(stream_id); iter != frame_handlers_.end()) {
    iter->second->onRequestFrame(std::move(request));
    return;
  }

  StreamFramePtrHelper<StreamRequest> helper(std::move(request));

  // Create a new active stream for the leading StreamRequest frame.
  if (helper.typed_frame_ != nullptr) {
    newDownstreamRequest(std::move(helper.typed_frame_));
    return;
  }

  ASSERT(helper.frame_ != nullptr);
  // No existing stream expects this non-leading frame. It should not happen.
  // We treat it as request decoding failure.
  ENVOY_LOG(error, "generic proxy: id {} not found for stream frame",
            helper.frame_->frameFlags().streamFlags().streamId());
  onDecodingFailure();
}

void Filter::onDecodingFailure() {
  stats_.request_decoding_error_.inc();

  resetStreamsForUnexpectedError();
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

void Filter::sendFrameToDownstream(StreamFrame& frame, ResponseEncoderCallback& callback) {
  response_encoder_->encode(frame, callback);
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

void Filter::newDownstreamRequest(StreamRequestPtr request) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request));
  auto raw_stream = stream.get();
  LinkedList::moveIntoList(std::move(stream), active_streams_);

  // Initialize filter chian.
  raw_stream->initializeFilterChain(*config_);
  // Start request.
  raw_stream->continueDecoding();
}

void Filter::deferredStream(ActiveStream& stream) {
  stream.completeRequest();

  if (!stream.inserted()) {
    return;
  }
  callbacks_->connection().dispatcher().deferredDelete(stream.removeFromList(active_streams_));
  mayBeDrainClose();
}

void Filter::resetStreamsForUnexpectedError() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
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

void Filter::bindUpstreamConn(Upstream::TcpPoolData&& tcp_pool_data) {
  ASSERT(config_->codecFactory().protocolOptions().bindUpstreamConnection());
  ASSERT(upstream_manager_ == nullptr);
  upstream_manager_ = std::make_unique<UpstreamManagerImpl>(*this, std::move(tcp_pool_data));
  upstream_manager_->newConnection();
}

void Filter::onBoundUpstreamConnectionEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "generic proxy: bound upstream connection closed.");
    // All pending streams should be reset by the upstream connection manager.
    // In case there are still pending streams, we reset them here again.
    resetStreamsForUnexpectedError();

    if (upstream_manager_ != nullptr) {
      // Clean up upstream connection manager. Always set the close_connection
      // flag to true to ensure the upstream connection is closed in case of
      // the onBoundUpstreamConnectionEvent() is called for other reasons.
      upstream_manager_->cleanUp(true);
      downstreamConnection().dispatcher().deferredDelete(std::move(upstream_manager_));
      upstream_manager_ = nullptr;
    }

    // Close downstream connection.
    closeDownstreamConnection();
  }
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
