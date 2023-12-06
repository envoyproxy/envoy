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
absl::string_view resetReasonToStringView(StreamResetReason reason) {
  static std::string Reasons[] = {"local_reset", "connection_failure", "connection_termination",
                                  "overflow", "protocol_error"};
  return Reasons[static_cast<uint32_t>(reason)];
}

constexpr absl::string_view RouterFilterName = "envoy.filters.generic.router";

} // namespace

void GenericUpstream::writeToConnection(Buffer::Instance& buffer) {
  if (is_cleaned_up_) {
    return;
  }

  if (owned_conn_data_ != nullptr) {
    ASSERT(owned_conn_data_->connection().state() == Network::Connection::State::Open);
    owned_conn_data_->connection().write(buffer, false);
  }
}

OptRef<Network::Connection> GenericUpstream::connection() {
  if (is_cleaned_up_) {
    return {};
  }
  if (owned_conn_data_ != nullptr) {
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
  if (upstream_connection_ready_.has_value()) {
    // Upstream connection is already ready. If the upstream connection is failed then
    // all pending requests will be reset and no new upstream request will be created.
    ASSERT(upstream_connection_ready_.value());
    if (!upstream_connection_ready_.value()) {
      return;
    }

    ASSERT(waiting_upstream_requests_.empty());

    if (waiting_response_requests_.contains(stream_id)) {
      ENVOY_LOG(error, "generic proxy: stream_id {} already registered for response", stream_id);
      // Close downstream connection because we treat this as request decoding failure.
      // The downstream closing will trigger the upstream connection closing.
      downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

    waiting_response_requests_[stream_id] = pending_request;
    pending_request->onUpstreamSuccess(upstream_host_);
  } else {
    // Waiting for the upstream connection to be ready.
    if (waiting_upstream_requests_.contains(stream_id)) {
      ENVOY_LOG(error, "generic proxy: stream_id {} already registered for upstream", stream_id);
      // Close downstream connection because we treat this as request decoding failure.
      // The downstream closing will trigger the upstream connection closing.
      downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

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

  ASSERT(waiting_upstream_requests_.empty());

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
  if (!close_connection) {
    return;
  }
  // Only actually do the cleanup when we want to close the connection.
  UpstreamConnection::cleanUp(true);
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
    cb->onUpstreamSuccess(upstream_host_);
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
    cb->onUpstreamFailure(reason, transport_failure_reason, upstream_host_);
  }

  // If the downstream connection is not closed, close it.
  downstream_connection_.close(Network::ConnectionCloseType::FlushWrite);
}

void BoundGenericUpstream::onDecodingSuccess(StreamFramePtr response) {
  const uint64_t stream_id = response->frameFlags().streamFlags().streamId();
  const bool end_stream = response->frameFlags().endStream();

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

  return cb->onDecodingSuccess(std::move(response));
}

void BoundGenericUpstream::onDecodingFailure() {
  ENVOY_LOG(error, "generic proxy bound upstream manager: decoding failure");

  // This will trigger the upstream connection close event and all pending requests will be reset
  // by the upstream connection close event.
  cleanUp(true);

  // All pending streams will be reset by the upstream connection close event.
  ASSERT(waiting_response_requests_.empty());
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
  upstream_request_->onUpstreamSuccess(upstream_host_);
}

void OwnedGenericUpstream::onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_failure_reason) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onUpstreamFailure(reason, transport_failure_reason, upstream_host_);
}

// ResponseDecoderCallback
void OwnedGenericUpstream::onDecodingSuccess(StreamFramePtr response) {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onDecodingSuccess(std::move(response));
}
void OwnedGenericUpstream::onDecodingFailure() {
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->onDecodingFailure();
}

UpstreamRequest::UpstreamRequest(RouterFilter& parent, GenericUpstreamSharedPtr generic_upstream)
    : parent_(parent), generic_upstream_(std::move(generic_upstream)),
      stream_info_(parent.context_.mainThreadDispatcher().timeSource(), nullptr) {

  // Set the upstream info for the stream info.
  stream_info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  parent_.callbacks_->streamInfo().setUpstreamInfo(stream_info_.upstreamInfo());
  stream_info_.healthCheck(parent_.callbacks_->streamInfo().healthCheck());
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);

  // Set request options.
  auto options = parent_.request_stream_->frameFlags().streamFlags();
  stream_id_ = options.streamId();
  expects_response_ = !options.oneWayStream();

  // Set tracing config.
  if (tracing_config_ = parent_.callbacks_->tracingConfig(); tracing_config_.has_value()) {
    span_ = parent_.callbacks_->activeSpan().spawnChild(
        tracing_config_.value().get(),
        absl::StrCat("router ", parent_.cluster_->observabilityName(), " egress"),
        parent.context_.mainThreadDispatcher().timeSource().systemTime());
  }
}

void UpstreamRequest::startStream() { generic_upstream_->insertUpstreamRequest(stream_id_, this); }

void UpstreamRequest::resetStream(StreamResetReason reason) {
  if (stream_reset_) {
    return;
  }
  stream_reset_ = true;

  ENVOY_LOG(debug, "generic proxy upstream request: reset upstream request");

  generic_upstream_->removeUpstreamRequest(stream_id_);
  generic_upstream_->cleanUp(true);

  if (span_ != nullptr) {
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, resetReasonToStringView(reason));
    TraceContextBridge trace_context{*parent_.request_stream_};
    Tracing::TracerUtility::finalizeSpan(*span_, trace_context, stream_info_,
                                         tracing_config_.value().get(), true);
  }

  // Remove this stream form the parent's list because this upstream request is reset.
  deferredDelete();

  // Notify the parent filter that the upstream request has been reset.
  parent_.onUpstreamRequestReset(*this, reason);
}

void UpstreamRequest::clearStream(bool close_connection) {
  // Set the upstream response complete flag to true first to ensure the possible
  // connection close event will not be handled.
  response_complete_ = true;

  ENVOY_LOG(debug, "generic proxy upstream request: complete upstream request");

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
  generic_upstream_->clientCodec()->encode(*parent_.request_stream_, *this);
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
    generic_upstream_->clientCodec()->encode(*frame, *this);
  }
}

void UpstreamRequest::onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) {
  encodeBufferToUpstream(buffer);

  if (!end_stream) {
    return;
  }

  // Request is complete.
  ENVOY_LOG(debug, "upstream request encoding success");

  // Need not to wait for the upstream response and complete directly.
  if (!expects_response_) {
    clearStream(false);
    parent_.completeDirectly();
    return;
  }
}

void UpstreamRequest::onUpstreamFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "upstream request: tcp connection (bound or owned) failure");

  // Mimic an upstream reset.
  onUpstreamHostSelected(std::move(host));

  if (reason == ConnectionPool::PoolFailureReason::Overflow) {
    resetStream(StreamResetReason::Overflow);
    return;
  }

  resetStream(StreamResetReason::ConnectionFailure);
}

void UpstreamRequest::onUpstreamSuccess(Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "upstream request: {} tcp connection has ready",
            parent_.config_->bindUpstreamConnection() ? "bound" : "owned");

  onUpstreamHostSelected(std::move(host));

  if (span_ != nullptr) {
    TraceContextBridge trace_context{*parent_.request_stream_};
    span_->injectContext(trace_context, upstream_host_);
  }

  sendRequestStartToUpstream();
  sendRequestFrameToUpstream();
}

void UpstreamRequest::onDecodingSuccess(StreamFramePtr response) {
  const bool end_stream = response->frameFlags().endStream();
  if (end_stream) {
    clearStream(response->frameFlags().streamFlags().drainClose());
  }

  if (response_stream_header_received_) {
    parent_.onResponseFrame(std::move(response));
    return;
  }

  StreamFramePtrHelper<StreamResponse> helper(std::move(response));
  if (helper.typed_frame_ == nullptr) {
    ENVOY_LOG(error, "upstream request: first frame is not StreamResponse");
    resetStream(StreamResetReason::ProtocolError);
    return;
  }
  response_stream_header_received_ = true;
  parent_.onResponseStart(std::move(helper.typed_frame_));
}

void UpstreamRequest::onDecodingFailure() { resetStream(StreamResetReason::ProtocolError); }

void UpstreamRequest::onConnectionClose(Network::ConnectionEvent event) {
  // If the upstream response is complete or the upstream request is reset then
  // ignore the connection close event.
  if (response_complete_ || stream_reset_) {
    return;
  }

  switch (event) {
  case Network::ConnectionEvent::LocalClose:
    resetStream(StreamResetReason::LocalReset);
    break;
  case Network::ConnectionEvent::RemoteClose:
    resetStream(StreamResetReason::ConnectionTermination);
    break;
  default:
    break;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "upstream request: selected upstream {}", host->address()->asString());
  upstream_host_ = std::move(host);
}

void UpstreamRequest::encodeBufferToUpstream(Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "proxying {} bytes", buffer.length());

  ASSERT(generic_upstream_ != nullptr);
  generic_upstream_->writeToConnection(buffer);
}

void RouterFilter::onResponseStart(ResponsePtr response) {
  filter_complete_ = response->frameFlags().endStream();
  callbacks_->onResponseStart(std::move(response));
}

void RouterFilter::onResponseFrame(StreamFramePtr frame) {
  ASSERT(!filter_complete_, "response frame received after response complete");
  filter_complete_ = frame->frameFlags().endStream();
  callbacks_->onResponseFrame(std::move(frame));
}

void RouterFilter::completeDirectly() {
  filter_complete_ = true;
  callbacks_->completeDirectly();
}

void RouterFilter::onUpstreamRequestReset(UpstreamRequest&, StreamResetReason reason) {
  if (filter_complete_) {
    return;
  }

  // TODO(wbpcode): To support retry policy.
  resetStream(reason);
}

void RouterFilter::cleanUpstreamRequests(bool filter_complete) {
  // If filter_complete_ is true then the resetStream() of RouterFilter will not be called on the
  // onUpstreamRequestReset() of RouterFilter.
  filter_complete_ = filter_complete;

  while (!upstream_requests_.empty()) {
    (*upstream_requests_.back()).resetStream(StreamResetReason::LocalReset);
  }
}

void RouterFilter::onDestroy() {
  if (filter_complete_) {
    return;
  }
  cleanUpstreamRequests(true);
}

void RouterFilter::resetStream(StreamResetReason reason) {
  if (filter_complete_) {
    return;
  }
  filter_complete_ = true;

  ASSERT(upstream_requests_.empty());
  switch (reason) {
  case StreamResetReason::LocalReset:
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, resetReasonToStringView(reason)));
    break;
  case StreamResetReason::ProtocolError:
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, resetReasonToStringView(reason)));
    break;
  case StreamResetReason::ConnectionFailure:
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, resetReasonToStringView(reason)));
    break;
  case StreamResetReason::ConnectionTermination:
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, resetReasonToStringView(reason)));
    break;
  case StreamResetReason::Overflow:
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, resetReasonToStringView(reason)));
    break;
  }
}

void RouterFilter::kickOffNewUpstreamRequest() {
  const auto& cluster_name = route_entry_->clusterName();

  auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    filter_complete_ = true;
    callbacks_->sendLocalReply(Status(StatusCode::kNotFound, "cluster_not_found"));
    return;
  }

  cluster_ = thread_local_cluster->info();
  callbacks_->streamInfo().setUpstreamClusterInfo(cluster_);

  if (cluster_->maintenanceMode()) {
    filter_complete_ = true;
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, "cluster_maintain_mode"));
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
        filter_complete_ = true;
        callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"));
        return;
      }
      auto new_bound_upstream = std::make_shared<BoundGenericUpstream>(
          callbacks_->downstreamCodec(), std::move(pool_data.value()), *downstream_connection);
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
      filter_complete_ = true;
      callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"));
      return;
    }
    generic_upstream = std::make_shared<OwnedGenericUpstream>(callbacks_->downstreamCodec(),
                                                              std::move(pool_data.value()));
  }

  auto upstream_request = std::make_unique<UpstreamRequest>(*this, std::move(generic_upstream));
  auto raw_upstream_request = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  raw_upstream_request->startStream();
}

void RouterFilter::onStreamFrame(StreamFramePtr frame) {
  request_stream_end_ = frame->frameFlags().endStream();
  request_stream_frames_.emplace_back(std::move(frame));

  if (upstream_requests_.empty()) {
    return;
  }

  upstream_requests_.front()->sendRequestFrameToUpstream();
}

FilterStatus RouterFilter::onStreamDecoded(StreamRequest& request) {
  ENVOY_LOG(debug, "Try route request to the upstream based on the route entry");

  setRouteEntry(callbacks_->routeEntry());
  request_stream_end_ = request.frameFlags().endStream();
  request_stream_ = &request;

  if (route_entry_ != nullptr) {
    kickOffNewUpstreamRequest();
    return FilterStatus::StopIteration;
  }

  ENVOY_LOG(debug, "No route for current request and send local reply");
  callbacks_->sendLocalReply(Status(StatusCode::kNotFound, "route_not_found"));
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
