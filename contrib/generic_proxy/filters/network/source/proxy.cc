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

ActiveStream::ActiveStream(Filter& parent, RequestPtr request, ExtendedOptions options)
    : parent_(parent), downstream_request_stream_(std::move(request)),
      downstream_request_options_(options),
      stream_info_(parent_.time_source_,
                   parent_.callbacks_->connection().connectionInfoProviderSharedPtr()),
      request_timer_(new Stats::HistogramCompletableTimespanImpl(parent_.stats_.request_time_ms_,
                                                                 parent_.time_source_)) {
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
  active_span_ = tracer->startSpan(*this, *downstream_request_stream_, stream_info_, decision);
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

void ActiveStream::sendLocalReply(Status status, ResponseUpdateFunction&& func) {
  ASSERT(parent_.message_creator_ != nullptr);
  local_or_upstream_response_stream_ =
      parent_.message_creator_->response(status, *downstream_request_stream_);

  ASSERT(local_or_upstream_response_stream_ != nullptr);

  if (func != nullptr) {
    func(*local_or_upstream_response_stream_);
  }

  parent_.sendReplyDownstream(*local_or_upstream_response_stream_, *this);
}

void ActiveStream::continueDecoding() {
  if (active_stream_reset_ || downstream_request_stream_ == nullptr) {
    return;
  }

  if (cached_route_entry_ == nullptr) {
    cached_route_entry_ = parent_.config_->routeEntry(*downstream_request_stream_);
  }

  ASSERT(downstream_request_stream_ != nullptr);
  for (; next_decoder_filter_index_ < decoder_filters_.size();) {
    auto status = decoder_filters_[next_decoder_filter_index_]->filter_->onStreamDecoded(
        *downstream_request_stream_);
    next_decoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }
  if (next_decoder_filter_index_ == decoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete decoder filters");
  }
}

void ActiveStream::upstreamResponse(ResponsePtr response, ExtendedOptions options) {
  local_or_upstream_response_stream_ = std::move(response);
  local_or_upstream_response_options_ = options;
  parent_.stream_drain_decision_ = options.drainClose();
  continueEncoding();
}

void ActiveStream::completeDirectly() { parent_.deferredStream(*this); };

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
  if (active_stream_reset_ || local_or_upstream_response_stream_ == nullptr) {
    return;
  }

  ASSERT(local_or_upstream_response_stream_ != nullptr);
  for (; next_encoder_filter_index_ < encoder_filters_.size();) {
    auto status = encoder_filters_[next_encoder_filter_index_]->filter_->onStreamEncoded(
        *local_or_upstream_response_stream_);
    next_encoder_filter_index_++;
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }

  if (next_encoder_filter_index_ == encoder_filters_.size()) {
    ENVOY_LOG(debug, "Complete encoder filters");
    parent_.sendReplyDownstream(*local_or_upstream_response_stream_, *this);
  }
}

void ActiveStream::onEncodingSuccess(Buffer::Instance& buffer) {
  ASSERT(parent_.downstreamConnection().state() == Network::Connection::State::Open);
  parent_.downstreamConnection().write(buffer, false);
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
  stream_info_.onRequestComplete();

  request_timer_->complete();
  parent_.stats_.request_active_.dec();

  if (active_span_) {
    Tracing::TracerUtility::finalizeSpan(*active_span_, *downstream_request_stream_, stream_info_,
                                         *this, false);
  }

  for (const auto& access_log : parent_.config_->accessLogs()) {
    access_log->log({downstream_request_stream_.get(), local_or_upstream_response_stream_.get()},
                    stream_info_);
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

void UpstreamManagerImpl::onDecodingSuccess(ResponsePtr response, ExtendedOptions options) {
  // registered_upstream_callbacks_ should be empty because after upstream connection is ready.
  ASSERT(registered_upstream_callbacks_.empty());

  uint64_t stream_id = options.streamId().value_or(0);

  auto it = registered_response_callbacks_.find(stream_id);
  if (it == registered_response_callbacks_.end()) {
    ENVOY_LOG(error, "generic proxy: stream_id {} not found", stream_id);
    return;
  }

  auto cb = it->second;
  registered_response_callbacks_.erase(it);

  cb->onDecodingSuccess(std::move(response), options);
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

void Filter::onDecodingSuccess(RequestPtr request, ExtendedOptions options) {
  newDownstreamRequest(std::move(request), options);
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

void Filter::sendReplyDownstream(Response& response, ResponseEncoderCallback& callback) {
  response_encoder_->encode(response, callback);
}

void Filter::newDownstreamRequest(RequestPtr request, ExtendedOptions options) {
  auto stream = std::make_unique<ActiveStream>(*this, std::move(request), options);
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
