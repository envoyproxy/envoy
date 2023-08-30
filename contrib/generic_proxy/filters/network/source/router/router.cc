#include "contrib/generic_proxy/filters/network/source/router/router.h"

#include "envoy/common/conn_pool.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/tracing/tracer_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/filter.h"

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
} // namespace

UpstreamManagerImpl::UpstreamManagerImpl(UpstreamRequest& parent, Upstream::TcpPoolData&& pool)
    : UpstreamConnection(std::move(pool),
                         parent.decoder_callbacks_.downstreamCodec().responseDecoder()),
      parent_(parent) {}

void UpstreamManagerImpl::onEventImpl(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  parent_.onConnectionClose(event);
}

void UpstreamManagerImpl::onPoolSuccessImpl() {
  parent_.onBindSuccess(owned_conn_data_->connection(), upstream_host_);
}

void UpstreamManagerImpl::onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                            absl::string_view transport_failure_reason) {
  parent_.onBindFailure(reason, transport_failure_reason, upstream_host_);
}

void UpstreamManagerImpl::setResponseCallback() { response_decoder_->setDecoderCallback(parent_); }

UpstreamRequest::UpstreamRequest(RouterFilter& parent,
                                 absl::optional<Upstream::TcpPoolData> tcp_pool_data)
    : parent_(parent), decoder_callbacks_(*parent_.callbacks_),
      tcp_pool_data_(std::move(tcp_pool_data)),
      stream_info_(parent.context_.mainThreadDispatcher().timeSource(), nullptr) {

  // Set the upstream info for the stream info.
  stream_info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  decoder_callbacks_.streamInfo().setUpstreamInfo(stream_info_.upstreamInfo());
  stream_info_.healthCheck(decoder_callbacks_.streamInfo().healthCheck());
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);

  // Set request options.
  auto options = decoder_callbacks_.requestOptions();
  ASSERT(options.has_value());
  stream_id_ = options->streamId().value_or(0);
  wait_response_ = options->waitResponse();

  // Set tracing config.
  tracing_config_ = decoder_callbacks_.tracingConfig();
  if (tracing_config_.has_value()) {
    span_ = decoder_callbacks_.activeSpan().spawnChild(
        tracing_config_.value().get(),
        absl::StrCat("router ", parent_.cluster_->observabilityName(), " egress"),
        parent.context_.mainThreadDispatcher().timeSource().systemTime());
  }
}

void UpstreamRequest::startStream() {
  if (!tcp_pool_data_.has_value()) {
    // Iff the upstream connection binding is enabled, the upstream connection should be
    // managed by the generic proxy directly. Then register the upstream callbacks to the
    // generic proxy and wait for the bound upstream connection.
    ASSERT(decoder_callbacks_.boundUpstreamConn().has_value());
    decoder_callbacks_.boundUpstreamConn()->registerUpstreamCallback(stream_id_, *this);
    return;
  }

  // If the tcp_pool_data_ has value, it means we should get or create an upstream connection
  // for the request.
  upstream_manager_ =
      std::make_unique<UpstreamManagerImpl>(*this, std::move(tcp_pool_data_.value()));
  upstream_manager_->newConnection();
}

void UpstreamRequest::resetStream(StreamResetReason reason) {
  if (stream_reset_) {
    return;
  }
  stream_reset_ = true;

  ENVOY_LOG(debug, "generic proxy upstream request: reset upstream request");

  if (upstream_manager_ != nullptr) {
    // If the upstream connection is managed by the upstream request self, we should clean
    // up the upstream connection.
    upstream_manager_->cleanUp(true);
    decoder_callbacks_.dispatcher().deferredDelete(std::move(upstream_manager_));
    upstream_manager_ = nullptr;
  } else {
    // If the upstream connection is not managed by the generic proxy, we should unregister
    // the related callbacks from the generic proxy.
    ASSERT(decoder_callbacks_.boundUpstreamConn().has_value());
    decoder_callbacks_.boundUpstreamConn()->unregisterUpstreamCallback(stream_id_);
    decoder_callbacks_.boundUpstreamConn()->unregisterResponseCallback(stream_id_);
  }

  if (span_ != nullptr) {
    span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    span_->setTag(Tracing::Tags::get().ErrorReason, resetReasonToStringView(reason));
    Tracing::TracerUtility::finalizeSpan(*span_, *parent_.request_, stream_info_,
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
    Tracing::TracerUtility::finalizeSpan(*span_, *parent_.request_, stream_info_,
                                         tracing_config_.value().get(), true);
  }

  if (upstream_manager_ != nullptr) {
    upstream_manager_->cleanUp(close_connection);
    decoder_callbacks_.dispatcher().deferredDelete(std::move(upstream_manager_));
    upstream_manager_ = nullptr;
  }

  // Remove this stream form the parent's list because this upstream request is complete.
  deferredDelete();
}

void UpstreamRequest::deferredDelete() {
  if (inserted()) {
    // Remove this stream from the parent's list of upstream requests and delete it at
    // next event loop iteration.
    decoder_callbacks_.dispatcher().deferredDelete(removeFromList(parent_.upstream_requests_));
  }
}

void UpstreamRequest::onEncodingSuccess(Buffer::Instance& buffer) {
  ENVOY_LOG(debug, "upstream request encoding success");
  encodeBufferToUpstream(buffer);

  // Need not to wait for the upstream response and complete directly.
  if (!wait_response_) {
    clearStream(false);
    parent_.completeDirectly();
    return;
  }

  // If the upstream connection manager is null, it means the upstream
  // connection is managed by the generic proxy directly. Register the
  // response callback to the generic proxy and wait for the upstream
  // response.
  if (upstream_manager_ == nullptr) {
    ASSERT(decoder_callbacks_.boundUpstreamConn().has_value());
    decoder_callbacks_.boundUpstreamConn()->registerResponseCallback(stream_id_, *this);
  } else {
    upstream_manager_->setResponseCallback();
  }
}

void UpstreamRequest::onBindFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
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

void UpstreamRequest::onBindSuccess(Network::ClientConnection& conn,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "upstream request: {} tcp connection has ready",
            upstream_manager_ != nullptr ? "owned" : "bound");

  onUpstreamHostSelected(std::move(host));
  upstream_conn_ = &conn;

  if (span_ != nullptr) {
    span_->injectContext(*parent_.request_, upstream_host_);
  }

  parent_.request_encoder_->encode(*parent_.request_, *this);
}

void UpstreamRequest::onDecodingSuccess(ResponsePtr response, ExtendedOptions options) {
  clearStream(options.drainClose());
  parent_.onUpstreamResponse(std::move(response), options);
}

void UpstreamRequest::onDecodingFailure() { resetStream(StreamResetReason::ProtocolError); }

void UpstreamRequest::writeToConnection(Buffer::Instance& buffer) {
  // If the upstream response is complete or the upstream request is reset then
  // ignore the write.
  if (stream_reset_ || response_complete_) {
    return;
  }

  if (upstream_conn_ != nullptr) {
    ASSERT(upstream_conn_->state() == Network::Connection::State::Open);
    upstream_conn_->write(buffer, false);
  }
}

OptRef<Network::Connection> UpstreamRequest::connection() {
  if (stream_reset_ || response_complete_) {
    return {};
  }

  return upstream_conn_ != nullptr ? OptRef<Network::Connection>(*upstream_conn_)
                                   : OptRef<Network::Connection>();
}

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
  ASSERT(upstream_conn_ != nullptr);

  ENVOY_LOG(trace, "proxying {} bytes", buffer.length());

  upstream_conn_->write(buffer, false);
}

void RouterFilter::onUpstreamResponse(ResponsePtr response, ExtendedOptions options) {
  filter_complete_ = true;
  callbacks_->upstreamResponse(std::move(response), std::move(options));
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

  if (callbacks_->boundUpstreamConn().has_value()) {
    // Upstream connection binding is enabled and the upstream connection is already bound.
    // Create a new upstream request without a connection pool and start the request.
    auto upstream_request = std::make_unique<UpstreamRequest>(*this, absl::nullopt);
    auto raw_upstream_request = upstream_request.get();
    LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
    raw_upstream_request->startStream();
    return;
  }

  auto pool_data = thread_local_cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!pool_data.has_value()) {
    filter_complete_ = true;
    callbacks_->sendLocalReply(Status(StatusCode::kUnavailable, "no_healthy_upstream"));
    return;
  }

  if (protocol_options_.bindUpstreamConnection()) {
    // Upstream connection binding is enabled and the upstream connection is not bound yet.
    // Bind the upstream connection and start the request.
    callbacks_->bindUpstreamConn(std::move(pool_data.value()));
    auto upstream_request = std::make_unique<UpstreamRequest>(*this, absl::nullopt);
    auto raw_upstream_request = upstream_request.get();
    LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
    raw_upstream_request->startStream();
    return;
  }

  // Normal upstream request.
  auto upstream_request = std::make_unique<UpstreamRequest>(*this, std::move(pool_data.value()));
  auto raw_upstream_request = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);
  raw_upstream_request->startStream();
}

FilterStatus RouterFilter::onStreamDecoded(Request& request) {
  ENVOY_LOG(debug, "Try route request to the upstream based on the route entry");

  setRouteEntry(callbacks_->routeEntry());
  request_ = &request;

  if (route_entry_ == nullptr) {
    ENVOY_LOG(debug, "No route for current request and send local reply");
    callbacks_->sendLocalReply(Status(StatusCode::kNotFound, "route_not_found"));
    return FilterStatus::StopIteration;
  }

  request_encoder_ = callbacks_->downstreamCodec().requestEncoder();
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
