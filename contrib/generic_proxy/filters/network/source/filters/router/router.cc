#include "contrib/generic_proxy/filters/network/source/filters/router/router.h"

#include "envoy/common/conn_pool.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_filter.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

namespace {
absl::string_view resetReasonToStringView(StreamResetReason reason) {
  static std::string Reasons[] = {"local_reset", "connection_failure", "connection_termination",
                                  "overflow", "ProtocolError"};
  return Reasons[static_cast<uint32_t>(reason)];
}
} // namespace

UpstreamRequest::UpstreamRequest(RouterFilter& parent, Upstream::TcpPoolData tcp_data)
    : parent_(parent), tcp_data_(std::move(tcp_data)) {}

void UpstreamRequest::startStream() {
  Tcp::ConnectionPool::Cancellable* handle = tcp_data_.newConnection(*this);
  conn_pool_handle_ = handle;
}

// TODO(wbpcode): To support stream reset reason.
void UpstreamRequest::resetStream(StreamResetReason reason) {
  ENVOY_LOG(debug, "generic proxy upstream request: reset upstream request");
  stream_reset_ = true;

  if (conn_pool_handle_) {
    ASSERT(!conn_data_);
    ENVOY_LOG(debug, "generic proxy upstream request: cacel upstream request");
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    conn_pool_handle_ = nullptr;
  }

  if (conn_data_) {
    ASSERT(!conn_pool_handle_);
    ENVOY_LOG(debug, "generic proxy upstream request: close upstream connection");
    conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    conn_data_.reset();
  }

  parent_.onUpstreamRequestReset(*this, reason);
}

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);

  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    resetStream(StreamResetReason::Overflow);
  default:
    // Treat pool timeout as connection failure.
    resetStream(StreamResetReason::ConnectionFailure);
  }

  resetStream(StreamResetReason::ConnectionFailure);
}

void UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "dubbo upstream request: tcp connection has ready");
  onUpstreamHostSelected(host);

  conn_data_ = std::move(conn);
  conn_data_->addUpstreamCallbacks(*this);
  conn_pool_handle_ = nullptr;

  encodeBufferToUpstream(parent_.upstream_request_buffer_);
}

void UpstreamRequest::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (!response_started_) {
    response_started_ = true;
    response_decoder_ = parent_.callbacks_->downstreamCodec().responseDecoder();
    response_decoder_->setDecoderCallback(*this);
  }
  response_decoder_->decode(data);

  if (end_stream && !response_complete_) {
    resetStream(StreamResetReason::ProtocolError);
  }
}

void UpstreamRequest::onGenericResponse(GenericResponsePtr response) {
  response_complete_ = true;
  ASSERT(conn_pool_handle_ == nullptr);
  ASSERT(conn_data_ != nullptr);
  conn_data_.reset();
  parent_.onUpstreamResponse(std::move(response));
}

void UpstreamRequest::onDecodingError() {
  response_complete_ = true;
  resetStream(StreamResetReason::ProtocolError);
}

void UpstreamRequest::onEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::LocalClose:
    if (!stream_reset_) {
      resetStream(StreamResetReason::LocalReset);
    }
    break;
  case Network::ConnectionEvent::RemoteClose:
    if (!stream_reset_) {
      resetStream(StreamResetReason::ConnectionTermination);
    }
    break;
  default:
    break;
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "dubbo upstream request: selected upstream {}", host->address()->asString());
  upstream_host_ = host;
}

void UpstreamRequest::encodeBufferToUpstream(Buffer::Instance& buffer) {
  ASSERT(conn_data_);
  ASSERT(!conn_pool_handle_);

  ENVOY_LOG(trace, "proxying {} bytes", buffer.length());

  conn_data_->connection().write(buffer, false);
}

void RouterFilter::onUpstreamResponse(GenericResponsePtr response) {
  // TODO(wbpcode): To support retry policy.
  callbacks_->upstreamResponse(std::move(response));
  filter_complete_ = true;
}

void RouterFilter::onUpstreamRequestReset(UpstreamRequest& upstream_request,
                                          StreamResetReason reason) {
  // Remove upstream request from router filter and move it to the defered-delete list.
  callbacks_->dispatcher().deferredDelete(upstream_request.removeFromList(upstream_requests_));

  if (filter_complete_) {
    return;
  }

  // TODO(wbpcode): To support retry policy.
  resetStream(reason);
}

void RouterFilter::cleanUpstreamRequests(bool filter_complete) {
  // If filter_complete is true then the resetStream() of RouterFilter will not be called on the
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
  ASSERT(upstream_requests_.empty());
  switch (reason) {
  case StreamResetReason::LocalReset:
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, resetReasonToStringView(reason));
    break;
  case StreamResetReason::ProtocolError:
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, resetReasonToStringView(reason));
    break;
  case StreamResetReason::ConnectionFailure:
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, resetReasonToStringView(reason));
    break;
  case StreamResetReason::ConnectionTermination:
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, resetReasonToStringView(reason));
  case StreamResetReason::Overflow:
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, resetReasonToStringView(reason));
  }

  filter_complete_ = true;
}

GenericFilterStatus RouterFilter::onStreamDecoded(GenericRequest& request) {
  const auto route_entry = callbacks_->routeEntry();
  if (route_entry == nullptr) {
    callbacks_->sendLocalReply(GenericState::LocalExpectedError, "route_not_found");
    return GenericFilterStatus::StopIteration;
  }

  const auto& cluster_name = route_entry->clusterName();

  auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(cluster_name);
  if (thread_local_cluster == nullptr) {
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, "cluster_not_found");
    return GenericFilterStatus::StopIteration;
  }

  auto cluster_info = thread_local_cluster->info();
  if (cluster_info->maintenanceMode()) {
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, "cluster_maintain_mode");
    return GenericFilterStatus::StopIteration;
  }

  auto tcp_data = thread_local_cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!tcp_data.has_value()) {
    callbacks_->sendLocalReply(GenericState::LocalUnknowedError, "no_healthy_upstream");
    return GenericFilterStatus::StopIteration;
  }

  request_encoder_ = callbacks_->downstreamCodec().requestEncoder();
  request_encoder_->encode(request, upstream_request_buffer_);

  auto upstream_request = std::make_unique<UpstreamRequest>(*this, std::move(tcp_data.value()));
  auto raw_upstream_request = upstream_request.get();
  LinkedList::moveIntoList(std::move(upstream_request), upstream_requests_);

  raw_upstream_request->startStream();
  return GenericFilterStatus::StopIteration;
}

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
