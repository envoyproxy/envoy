#include "contrib/generic_proxy/filters/network/source/router/router.h"

#include <cstdint>

#include "envoy/common/conn_pool.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/tracing/tracer_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/tracing.h"

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

constexpr absl::string_view RouterFilterName = "envoy.filters.generic.router";

} // namespace

GenericUpstream::~GenericUpstream() {
  // In case we doesn't clean up the pending connecting request.
  if (tcp_pool_handle_ != nullptr) {
    // Clear the data first.
    auto local_handle = tcp_pool_handle_;
    tcp_pool_handle_ = nullptr;

    local_handle->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

void GenericUpstream::initialize() {
  if (!initialized_) {
    initialized_ = true;
    tcp_pool_handle_ = tcp_pool_data_.newConnection(*this);
  }
}

void GenericUpstream::cleanUp(bool close_connection) {
  ENVOY_LOG(debug, "generic proxy upstream manager: clean up upstream (close: {})",
            close_connection);

  if (close_connection && owned_conn_data_ != nullptr) {
    ENVOY_LOG(debug, "generic proxy upstream request: close upstream connection");
    ASSERT(tcp_pool_handle_ == nullptr);

    // Clear the data first to avoid re-entering this function in the close callback.
    auto local_data = std::move(owned_conn_data_);
    owned_conn_data_.reset();

    local_data->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  if (tcp_pool_handle_ != nullptr) {
    ENVOY_LOG(debug, "generic proxy upstream manager: cacel upstream connection");
    ASSERT(owned_conn_data_ == nullptr);

    // Clear the data first.
    auto local_handle = tcp_pool_handle_;
    tcp_pool_handle_ = nullptr;

    local_handle->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

void GenericUpstream::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    absl::string_view transport_failure_reason,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection failure (host: {})",
            host != nullptr ? host->address()->asStringView() : absl::string_view{});

  tcp_pool_handle_ = nullptr;
  mayUpdateUpstreamHost(std::move(host));

  onPoolFailureImpl(reason, transport_failure_reason);
}

void GenericUpstream::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  ASSERT(host != nullptr);
  ENVOY_LOG(debug, "generic proxy upstream manager: on upstream connection ready (host: {})",
            host->address()->asStringView());

  tcp_pool_handle_ = nullptr;
  mayUpdateUpstreamHost(std::move(host));

  owned_conn_data_ = std::move(conn_data);
  owned_conn_data_->addUpstreamCallbacks(*this);

  onPoolSuccessImpl();
}

void GenericUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (data.length() == 0) {
    return;
  }

  client_codec_->decode(data, end_stream);
}

void GenericUpstream::writeToConnection(Buffer::Instance& buffer) {
  if (owned_conn_data_ != nullptr &&
      owned_conn_data_->connection().state() == Network::Connection::State::Open) {
    owned_conn_data_->connection().write(buffer, false);
  }
}

OptRef<Network::Connection> GenericUpstream::connection() {
  if (owned_conn_data_ != nullptr &&
      owned_conn_data_->connection().state() == Network::Connection::State::Open) {
    return {owned_conn_data_->connection()};
  }
  return {};
}

BoundGenericUpstream::BoundGenericUpstream(const CodecFactory& codec_factory,
                                           Envoy::Upstream::TcpPoolData&& tcp_pool_data,
                                           Network::Connection& downstream_connection)
    : GenericUpstream(std::move(tcp_pool_data), codec_factory.createClientCodec()),
      downstream_connection_(downstream_connection) {

  connection_event_watcher_ = std::make_unique<EventWatcher>(*this);
  downstream_connection_.addConnectionCallbacks(*connection_event_watcher_);
}

void BoundGenericUpstream::onDownstreamConnectionEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    // The event should be handled first by the generic proxy first. So all pending
    // requests will be cleaned up by the downstream connection close event.
    ASSERT(waiting_upstream_requests_.empty());
    ASSERT(waiting_response_requests_.empty());

    // Close upstream connection and this will trigger the upstream connection close event.
    cleanUp(true);
  }
}

void BoundGenericUpstream::insertUpstreamRequest(uint64_t stream_id,
                                                 UpstreamRequest* pending_request) {
  if (waiting_response_requests_.contains(stream_id) ||
      waiting_upstream_requests_.contains(stream_id)) {
    ENVOY_LOG(error, "generic proxy: stream_id {} already registered", stream_id);
    // Close downstream connection because we treat this as request decoding failure.
    // The downstream closing will trigger the upstream connection closing.
    downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  if (upstream_connection_ready_.has_value()) {
    // Upstream connection is already ready. If the upstream connection is failed then
    // all pending requests will be reset and no new upstream request will be created.
    if (!upstream_connection_ready_.value()) {
      return;
    }

    waiting_response_requests_[stream_id] = pending_request;
    pending_request->onUpstreamSuccess();
  } else {
    waiting_upstream_requests_[stream_id] = pending_request;

    // Try to initialize the upstream connection after there is at least one pending request.
    // If the upstream connection is already initialized, this is a no-op.
    initialize();
  }
}

void BoundGenericUpstream::removeUpstreamRequest(uint64_t stream_id) {
  waiting_upstream_requests_.erase(stream_id);
  waiting_response_requests_.erase(stream_id);
}

void BoundGenericUpstream::onEventImpl(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }

  while (!waiting_response_requests_.empty()) {
    auto it = waiting_response_requests_.begin();
    auto cb = it->second;
    waiting_response_requests_.erase(it);

    cb->onConnectionClose(event);
  }

  // If the downstream connection is not closed, close it.
  if (downstream_connection_.state() == Network::Connection::State::Open) {
    downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
  }
}

void BoundGenericUpstream::cleanUp(bool close_connection) {
  // Shared upstream manager never release the connection back to the pool
  // because the connection is bound to the downstream connection.
  if (!close_connection || prevent_clean_up_) {
    return;
  }
  // Only actually do the cleanup when we want to close the connection.
  GenericUpstream::cleanUp(true);
}

void BoundGenericUpstream::onPoolSuccessImpl() {
  // This should be called only once and all pending requests should be notified.
  // After this is called, the upstream connection is ready and new upstream requests
  // should be notified directly.

  ASSERT(!upstream_connection_ready_.has_value());
  upstream_connection_ready_ = true;

  ASSERT(waiting_response_requests_.empty());

  while (!waiting_upstream_requests_.empty()) {
    auto it = waiting_upstream_requests_.begin();
    auto cb = it->second;

    // Insert it to the waiting response list and remove it from the waiting upstream list.
    waiting_response_requests_[it->first] = cb;
    waiting_upstream_requests_.erase(it);

    // Now, notify the upstream request that the upstream connection is ready.
    cb->onUpstreamSuccess();
  }
}

void BoundGenericUpstream::onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_failure_reason) {
  // This should be called only once and all pending requests should be notified.
  // Then the downstream connection will be closed.

  ASSERT(!upstream_connection_ready_.has_value());
  upstream_connection_ready_ = false;

  ASSERT(waiting_response_requests_.empty());

  while (!waiting_upstream_requests_.empty()) {
    auto it = waiting_upstream_requests_.begin();
    auto cb = it->second;

    // Remove it from the waiting upstream list.
    waiting_upstream_requests_.erase(it);

    // Now, notify the upstream request that the upstream connection is failed.
    cb->onUpstreamFailure(reason, transport_failure_reason);
  }

  // If the downstream connection is not closed, close it.
  downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
}

void BoundGenericUpstream::onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                                             absl::optional<StartTime> start_time) {
  const uint64_t stream_id = response_header_frame->frameFlags().streamFlags().streamId();
  const bool end_stream = response_header_frame->frameFlags().endStream();

  auto it = waiting_response_requests_.find(stream_id);
  if (it == waiting_response_requests_.end()) {
    ENVOY_LOG(error, "generic proxy: id {} not found for frame", stream_id);
    return;
  }

  auto cb = it->second;

  // If the response is end, remove the callback from the map.
  if (end_stream) {
    waiting_response_requests_.erase(it);
  }

  return cb->onDecodingSuccess(std::move(response_header_frame), std::move(start_time));
}

void BoundGenericUpstream::onDecodingSuccess(ResponseCommonFramePtr response_common_frame) {
  const uint64_t stream_id = response_common_frame->frameFlags().streamFlags().streamId();
  const bool end_stream = response_common_frame->frameFlags().endStream();

  auto it = waiting_response_requests_.find(stream_id);
  if (it == waiting_response_requests_.end()) {
    ENVOY_LOG(error, "generic proxy: id {} not found for frame", stream_id);
    return;
  }

  auto cb = it->second;

  // If the response is end, remove the callback from the map.
  if (end_stream) {
    waiting_response_requests_.erase(it);
  }

  return cb->onDecodingSuccess(std::move(response_common_frame));
}

void BoundGenericUpstream::onDecodingFailure(absl::string_view reason) {
  ENVOY_LOG(error, "generic proxy bound upstream manager: decoding failure ({})", reason);

  // Prevent the clean up to ensure the connection will not be closed by the following
  // upstream request reset.
  prevent_clean_up_ = true;

  while (!waiting_response_requests_.empty()) {
    auto it = waiting_response_requests_.begin();
    auto upstream_request = it->second;
    waiting_response_requests_.erase(it);
    upstream_request->onDecodingFailure(reason);
  }

  // Now to clean up the connection after all upstream requests are reset.
  prevent_clean_up_ = false;
  cleanUp(true);
}

OwnedGenericUpstream::OwnedGenericUpstream(const CodecFactory& codec_factory,
                                           Envoy::Upstream::TcpPoolData&& tcp_pool_data)
    : GenericUpstream(std::move(tcp_pool_data), codec_factory.createClientCodec()) {}

void OwnedGenericUpstream::insertUpstreamRequest(uint64_t, UpstreamRequest* pending_request) {
  upstream_request_ = pending_request;
  initialize();
}

void OwnedGenericUpstream::onEventImpl(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onConnectionClose(event);
}

void OwnedGenericUpstream::onPoolSuccessImpl() {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onUpstreamSuccess();
}

void OwnedGenericUpstream::onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_failure_reason) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onUpstreamFailure(reason, transport_failure_reason);
}

void OwnedGenericUpstream::onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                                             absl::optional<StartTime> start_time) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onDecodingSuccess(std::move(response_header_frame), std::move(start_time));
}
void OwnedGenericUpstream::onDecodingSuccess(ResponseCommonFramePtr response_common_frame) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onDecodingSuccess(std::move(response_common_frame));
}
void OwnedGenericUpstream::onDecodingFailure(absl::string_view reason) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onDecodingFailure(reason);
}

UpstreamRequest::UpstreamRequest(RouterFilter& parent, GenericUpstreamSharedPtr generic_upstream)
    : parent_(parent), generic_upstream_(std::move(generic_upstream)),
      stream_info_(parent.time_source_, nullptr, StreamInfo::FilterState::LifeSpan::FilterChain),
      upstream_info_(std::make_shared<StreamInfo::UpstreamInfoImpl>()) {

  // Host is known at this point and set the initial upstream host.
  onUpstreamHostSelected(generic_upstream_->upstreamHost());

  // Set the upstream info for the stream info.
  stream_info_.setUpstreamInfo(upstream_info_);
  parent_.callbacks_->streamInfo().setUpstreamInfo(upstream_info_);
  stream_info_.healthCheck(parent_.callbacks_->streamInfo().healthCheck());
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);

  // Set request options.
  auto options = parent_.request_stream_->frameFlags().streamFlags();
  stream_id_ = options.streamId();
  expects_response_ = !options.oneWayStream();

  // Set tracing config.
  tracing_config_ = parent_.callbacks_->tracingConfig();
  if (tracing_config_.has_value() && tracing_config_->spawnUpstreamSpan()) {
    span_ = parent_.callbacks_->activeSpan().spawnChild(
        tracing_config_.value().get(),
        absl::StrCat("router ", parent_.cluster_->observabilityName(), " egress"),
        parent.time_source_.systemTime());
  }
}

void UpstreamRequest::startStream() {
  connecting_start_time_ = parent_.time_source_.monotonicTime();
  generic_upstream_->insertUpstreamRequest(stream_id_, this);
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

  ENVOY_LOG(debug, "generic proxy upstream request: complete upstream request ()",
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
  if (inserted()) {
    // Remove this stream from the parent's list of upstream requests and delete it at
    // next event loop iteration.
    parent_.callbacks_->dispatcher().deferredDelete(removeFromList(parent_.upstream_requests_));
  }
}

void UpstreamRequest::sendRequestStartToUpstream() {
  request_stream_header_sent_ = true;
  ASSERT(generic_upstream_ != nullptr);

  // The first frame of request is sent.
  upstream_info_->upstreamTiming().onFirstUpstreamTxByteSent(parent_.time_source_);
  generic_upstream_->clientCodec().encode(*parent_.request_stream_, *this);
}

void UpstreamRequest::sendRequestFrameToUpstream() {
  if (!request_stream_header_sent_) {
    // Do not send request frame to upstream until the request header is sent. It may be blocked
    // by the upstream connecting.
    return;
  }

  while (!parent_.request_stream_frames_.empty()) {
    auto frame = std::move(parent_.request_stream_frames_.front());
    parent_.request_stream_frames_.pop_front();

    ASSERT(generic_upstream_ != nullptr);
    generic_upstream_->clientCodec().encode(*frame, *this);
  }
}

void UpstreamRequest::onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) {
  encodeBufferToUpstream(buffer);

  if (!end_stream) {
    return;
  }

  // The request is fully sent.
  upstream_info_->upstreamTiming().onLastUpstreamTxByteSent(parent_.time_source_);

  // Request is complete.
  ENVOY_LOG(debug, "upstream request encoding success");

  // Need not to wait for the upstream response and complete directly.
  if (!expects_response_) {
    clearStream(false);
    parent_.completeDirectly();
    return;
  }
}

void UpstreamRequest::onEncodingFailure(absl::string_view reason) {
  // The request encoding failure is treated as a protocol error.
  resetStream(StreamResetReason::ProtocolError, reason);
}

OptRef<const RouteEntry> UpstreamRequest::routeEntry() const {
  return makeOptRefFromPtr(parent_.route_entry_);
}

void UpstreamRequest::onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                                        absl::string_view transport_failure_reason) {
  ENVOY_LOG(debug, "upstream request: tcp connection (bound or owned) failure");
  onUpstreamHostSelected(generic_upstream_->upstreamHost());
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
  onUpstreamHostSelected(generic_upstream_->upstreamHost());
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

  sendRequestStartToUpstream();
  sendRequestFrameToUpstream();
}

void UpstreamRequest::onUpstreamResponseComplete(bool drain_close) {
  clearStream(drain_close);
  upstream_info_->upstreamTiming().onLastUpstreamRxByteReceived(parent_.time_source_);
}

void UpstreamRequest::onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                                        absl::optional<StartTime> start_time) {
  if (response_stream_header_received_) {
    ENVOY_LOG(error, "upstream request: multiple StreamResponse received");
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
    onUpstreamResponseComplete(response_header_frame->frameFlags().streamFlags().drainClose());
  }

  parent_.onResponseStart(std::move(response_header_frame));
}

void UpstreamRequest::onDecodingSuccess(ResponseCommonFramePtr response_common_frame) {
  if (!response_stream_header_received_) {
    ENVOY_LOG(error, "upstream request: first frame is not StreamResponse");
    resetStream(StreamResetReason::ProtocolError, {});
    return;
  }

  if (response_common_frame->frameFlags().endStream()) {
    onUpstreamResponseComplete(response_common_frame->frameFlags().streamFlags().drainClose());
  }

  parent_.onResponseFrame(std::move(response_common_frame));
}

void UpstreamRequest::onDecodingFailure(absl::string_view reason) {
  // Decoding failure after the response is complete, close the connection.
  // This should only happen when some special cases, for example:
  // The HTTP response is complete but the request is not fully sent.
  // The codec will throw an error after the response is complete.
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

  switch (event) {
  case Network::ConnectionEvent::LocalClose:
    resetStream(StreamResetReason::LocalReset, {});
    break;
  case Network::ConnectionEvent::RemoteClose:
    resetStream(StreamResetReason::ConnectionTermination, {});
    break;
  default:
    break;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  if (host == nullptr || host == upstream_info_->upstream_host_) {
    return;
  }

  ENVOY_LOG(debug, "upstream request: selected host {}", host->address()->asStringView());
  upstream_info_->upstream_host_ = std::move(host);
}

void UpstreamRequest::onUpstreamConnectionReady() {
  ASSERT(connecting_start_time_.has_value());
  upstream_info_->upstreamTiming().recordConnectionPoolCallbackLatency(
      connecting_start_time_.value(), parent_.time_source_);
}

void UpstreamRequest::encodeBufferToUpstream(Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "proxying {} bytes", buffer.length());

  ASSERT(generic_upstream_ != nullptr);
  generic_upstream_->writeToConnection(buffer);
}

void RouterFilter::onResponseStart(ResponseHeaderFramePtr response_header_frame) {
  if (response_header_frame->frameFlags().endStream()) {
    onFilterComplete();
  }
  callbacks_->onResponseStart(std::move(response_header_frame));
}

void RouterFilter::onResponseFrame(ResponseCommonFramePtr response_common_frame) {
  if (response_common_frame->frameFlags().endStream()) {
    onFilterComplete();
  }
  callbacks_->onResponseFrame(std::move(response_common_frame));
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

  // Retry is the upstream request is reset because of the connection failure or the
  // protocol error.
  if (couldRetry(reason)) {
    kickOffNewUpstreamRequest();
    return;
  }

  resetStream(reason, reason_detail);
}

void RouterFilter::onFilterComplete() {
  // Ensure this method is called only once strictly.
  if (filter_complete_) {
    return;
  }
  filter_complete_ = true;

  // Clean up all pending upstream requests.
  while (!upstream_requests_.empty()) {
    auto* upstream_request = upstream_requests_.back().get();
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

void RouterFilter::resetStream(StreamResetReason reason, absl::string_view reason_detail) {
  // Ensure this method is called only once strictly and never called after
  // onFilterComplete().
  if (filter_complete_) {
    return;
  }
  const auto [view, flag] = resetReasonToViewAndFlag(reason);
  completeAndSendLocalReply(Status(StatusCode::kUnavailable, view), reason_detail, flag);
}

void RouterFilter::completeAndSendLocalReply(absl::Status status, absl::string_view details,
                                             absl::optional<StreamInfo::CoreResponseFlag> flag) {
  if (flag.has_value()) {
    callbacks_->streamInfo().setResponseFlag(flag.value());
  }
  onFilterComplete();
  callbacks_->sendLocalReply(std::move(status), details);
}

void RouterFilter::kickOffNewUpstreamRequest() {
  num_retries_++;

  const auto& cluster_name = route_entry_->clusterName();

  auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    completeAndSendLocalReply(Status(StatusCode::kNotFound, "cluster_not_found"), {});
    return;
  }

  cluster_ = thread_local_cluster->info();
  callbacks_->streamInfo().setUpstreamClusterInfo(cluster_);

  if (cluster_->maintenanceMode()) {
    // No response flag for maintenance mode for now.
    completeAndSendLocalReply(Status(StatusCode::kUnavailable, "cluster_maintain_mode"), {});
    return;
  }

  GenericUpstreamSharedPtr generic_upstream;

  if (config_->bindUpstreamConnection()) {
    // If the upstream connection binding is enabled.

    const auto* const_downstream_connection = callbacks_->connection();
    ASSERT(const_downstream_connection != nullptr);
    auto downstream_connection = const_cast<Network::Connection*>(const_downstream_connection);

    auto* bound_upstream =
        downstream_connection->streamInfo().filterState()->getDataMutable<BoundGenericUpstream>(
            RouterFilterName);
    if (bound_upstream == nullptr) {
      // The upstream connection is not bound yet and create a new bound upstream connection.
      auto pool_data = thread_local_cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
      if (!pool_data.has_value()) {
        completeAndSendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"), {},
                                  StreamInfo::CoreResponseFlag::NoHealthyUpstream);
        return;
      }
      auto new_bound_upstream = std::make_shared<BoundGenericUpstream>(
          callbacks_->codecFactory(), std::move(pool_data.value()), *downstream_connection);
      bound_upstream = new_bound_upstream.get();
      downstream_connection->streamInfo().filterState()->setData(
          RouterFilterName, std::move(new_bound_upstream),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    }

    ASSERT(bound_upstream != nullptr);
    generic_upstream = bound_upstream->shared_from_this();
  } else {
    // Upstream connection binding is disabled and create a new upstream connection.
    auto pool_data = thread_local_cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
    if (!pool_data.has_value()) {
      completeAndSendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"), {},
                                StreamInfo::CoreResponseFlag::NoHealthyUpstream);
      return;
    }
    generic_upstream = std::make_shared<OwnedGenericUpstream>(callbacks_->codecFactory(),
                                                              std::move(pool_data.value()));
  }

  auto upstream_request = std::make_unique<UpstreamRequest>(*this, std::move(generic_upstream));
  auto raw_upstream_request = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  raw_upstream_request->startStream();
}

void RouterFilter::onRequestCommonFrame(RequestCommonFramePtr frame) {
  mayRequestStreamEnd(frame->frameFlags().endStream());

  request_stream_frames_.emplace_back(std::move(frame));

  if (upstream_requests_.empty()) {
    return;
  }

  upstream_requests_.front()->sendRequestFrameToUpstream();
}

FilterStatus RouterFilter::onStreamDecoded(StreamRequest& request) {
  ENVOY_LOG(debug, "Try route request to the upstream based on the route entry");

  setRouteEntry(callbacks_->routeEntry());
  request_stream_ = &request;

  if (route_entry_ == nullptr) {
    ENVOY_LOG(debug, "No route for current request and send local reply");
    completeAndSendLocalReply(Status(StatusCode::kNotFound, "route_not_found"), {},
                              StreamInfo::CoreResponseFlag::NoRouteFound);
    return FilterStatus::StopIteration;
  }

  mayRequestStreamEnd(request.frameFlags().endStream());
  kickOffNewUpstreamRequest();
  return FilterStatus::StopIteration;
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
