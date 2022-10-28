#include "source/extensions/filters/network/dubbo_proxy/router/router_impl.h"

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

void Router::onDestroy() {
  if (upstream_request_) {
    upstream_request_->resetStream();
  }
  cleanup();
}

void Router::setDecoderFilterCallbacks(DubboFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

FilterStatus Router::onMessageDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) {
  ASSERT(metadata->hasInvocationInfo());
  const auto& invocation = metadata->invocationInfo();

  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "dubbo router: no cluster match for interface '{}'", *callbacks_,
                     invocation.serviceName());
    callbacks_->sendLocalReply(AppException(ResponseStatus::ServiceNotFound,
                                            fmt::format("dubbo router: no route for interface '{}'",
                                                        invocation.serviceName())),
                               false);
    return FilterStatus::AbortIteration;
  }

  route_entry_ = route_->routeEntry();

  Upstream::ThreadLocalCluster* cluster =
      cluster_manager_.getThreadLocalCluster(route_entry_->clusterName());
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "dubbo router: unknown cluster '{}'", *callbacks_,
                     route_entry_->clusterName());
    callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError, fmt::format("dubbo router: unknown cluster '{}'",
                                                              route_entry_->clusterName())),
        false);
    return FilterStatus::AbortIteration;
  }

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "dubbo router: cluster '{}' match for interface '{}'", *callbacks_,
                   route_entry_->clusterName(), invocation.serviceName());

  if (cluster_->maintenanceMode()) {
    callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError,
                     fmt::format("dubbo router: maintenance mode for cluster '{}'",
                                 route_entry_->clusterName())),
        false);
    return FilterStatus::AbortIteration;
  }

  auto conn_pool_data = cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!conn_pool_data) {
    callbacks_->sendLocalReply(
        AppException(
            ResponseStatus::ServerError,
            fmt::format("dubbo router: no healthy upstream for '{}'", route_entry_->clusterName())),
        false);
    return FilterStatus::AbortIteration;
  }

  ENVOY_STREAM_LOG(debug, "dubbo router: decoding request", *callbacks_);

  const auto* invocation_impl = dynamic_cast<const RpcInvocationImpl*>(&invocation);
  ASSERT(invocation_impl);

  if (invocation_impl->hasAttachment() && invocation_impl->attachment().attachmentUpdated()) {
    constexpr size_t body_length_size = sizeof(uint32_t);

    const size_t attachment_offset = invocation_impl->attachment().attachmentOffset();
    const size_t request_header_size = ctx->headerSize();

    ASSERT(attachment_offset <= ctx->originMessage().length());

    // Move the other parts of the request headers except the body size to the upstream request
    // buffer.
    upstream_request_buffer_.move(ctx->originMessage(), request_header_size - body_length_size);
    // Discard the old body size.
    ctx->originMessage().drain(body_length_size);

    // Re-serialize the updated attachment.
    Buffer::OwnedImpl attachment_buffer;
    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(attachment_buffer));
    encoder.encode(invocation_impl->attachment().attachment());

    size_t new_body_size = attachment_offset - request_header_size + attachment_buffer.length();

    upstream_request_buffer_.writeBEInt<uint32_t>(new_body_size);
    upstream_request_buffer_.move(ctx->originMessage(), attachment_offset - request_header_size);
    upstream_request_buffer_.move(attachment_buffer);

    // Discard the old attachment.
    ctx->originMessage().drain(ctx->messageSize() - attachment_offset);
  } else {
    upstream_request_buffer_.move(ctx->originMessage(), ctx->messageSize());
  }

  upstream_request_ = std::make_unique<UpstreamRequest>(*this, *conn_pool_data, metadata,
                                                        callbacks_->serializationType(),
                                                        callbacks_->protocolType());
  return upstream_request_->start();
}

void Router::setEncoderFilterCallbacks(DubboFilters::EncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

FilterStatus Router::onMessageEncoded(MessageMetadataSharedPtr metadata, ContextSharedPtr) {
  if (!metadata->hasResponseStatus() || upstream_request_ == nullptr) {
    return FilterStatus::Continue;
  }

  ENVOY_STREAM_LOG(trace, "dubbo router: response status: {}", *encoder_callbacks_,
                   static_cast<int>(metadata->responseStatus()));

  switch (metadata->responseStatus()) {
  case ResponseStatus::Ok:
    if (metadata->messageType() == MessageType::Exception) {
      upstream_request_->upstream_host_->outlierDetector().putResult(
          Upstream::Outlier::Result::ExtOriginRequestFailed);
    } else {
      upstream_request_->upstream_host_->outlierDetector().putResult(
          Upstream::Outlier::Result::ExtOriginRequestSuccess);
    }
    break;
  case ResponseStatus::ServerTimeout:
    upstream_request_->upstream_host_->outlierDetector().putResult(
        Upstream::Outlier::Result::LocalOriginTimeout);
    break;
  case ResponseStatus::ServiceError:
    FALLTHRU;
  case ResponseStatus::ServerError:
    FALLTHRU;
  case ResponseStatus::ServerThreadpoolExhaustedError:
    upstream_request_->upstream_host_->outlierDetector().putResult(
        Upstream::Outlier::Result::ExtOriginRequestFailed);
    break;
  default:
    break;
  }

  return FilterStatus::Continue;
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!upstream_request_->response_complete_);

  ENVOY_STREAM_LOG(trace, "dubbo router: reading response: {} bytes", *callbacks_, data.length());

  // Handle normal response.
  if (!upstream_request_->response_started_) {
    callbacks_->startUpstreamResponse();
    upstream_request_->response_started_ = true;
  }

  DubboFilters::UpstreamResponseStatus status = callbacks_->upstreamData(data);
  if (status == DubboFilters::UpstreamResponseStatus::Complete) {
    ENVOY_STREAM_LOG(debug, "dubbo router: response complete", *callbacks_);
    upstream_request_->onResponseComplete();
    cleanup();
    return;
  } else if (status == DubboFilters::UpstreamResponseStatus::Reset) {
    ENVOY_STREAM_LOG(debug, "dubbo router: upstream reset", *callbacks_);
    // When the upstreamData function returns Reset,
    // the current stream is already released from the upper layer,
    // so there is no need to call callbacks_->resetStream() to notify
    // the upper layer to release the stream.
    upstream_request_->resetStream();
    return;
  }

  if (end_stream) {
    // Response is incomplete, but no more data is coming.
    ENVOY_STREAM_LOG(debug, "dubbo router: response underflow", *callbacks_);
    upstream_request_->onResetStream(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    upstream_request_->onResponseComplete();
    cleanup();
  }
}

void Router::onEvent(Network::ConnectionEvent event) {
  if (!upstream_request_ || upstream_request_->response_complete_) {
    ENVOY_BUG(event == Network::ConnectionEvent::RemoteClose ||
                  event == Network::ConnectionEvent::LocalClose,
              "Unexpected event");
    // Client closed connection after completing response.
    ENVOY_LOG(debug, "dubbo upstream request: the upstream request had completed");
    return;
  }

  if (upstream_request_->stream_reset_ && event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "dubbo upstream request: the stream reset");
    return;
  }

  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    upstream_request_->onResetStream(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    upstream_request_->upstream_host_->outlierDetector().putResult(
        Upstream::Outlier::Result::LocalOriginConnectFailed);
    break;
  case Network::ConnectionEvent::LocalClose:
    upstream_request_->onResetStream(ConnectionPool::PoolFailureReason::LocalConnectionFailure);
    break;
  case Network::ConnectionEvent::Connected:
  case Network::ConnectionEvent::ConnectedZeroRtt:
    // Connected is consumed by the connection pool.
    IS_ENVOY_BUG("unexpected");
  }
}

const Envoy::Router::MetadataMatchCriteria* Router::metadataMatchCriteria() {
  // Have we been called before? If so, there's no need to recompute because
  // by the time this method is called for the first time, route_entry_ should
  // not change anymore
  if (metadata_match_ != nullptr) {
    return metadata_match_.get();
  }

  const Envoy::Router::MetadataMatchCriteria* route_criteria =
      (route_entry_ != nullptr) ? route_entry_->metadataMatchCriteria() : nullptr;

  // The request's metadata, if present, takes precedence over the route's.
  const auto& request_metadata = callbacks_->streamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = request_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

  if (filter_it == request_metadata.end()) {
    return route_criteria;
  }

  if (route_criteria != nullptr) {
    metadata_match_ = route_criteria->mergeMatchCriteria(filter_it->second);
  } else {
    metadata_match_ = std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
  }
  return metadata_match_.get();
}

const Network::Connection* Router::downstreamConnection() const {
  return callbacks_ != nullptr ? callbacks_->connection() : nullptr;
}

void Router::cleanup() {
  if (upstream_request_) {
    upstream_request_.reset();
  }
}

Router::UpstreamRequest::UpstreamRequest(Router& parent, Upstream::TcpPoolData& pool_data,
                                         MessageMetadataSharedPtr& metadata,
                                         SerializationType serialization_type,
                                         ProtocolType protocol_type)
    : parent_(parent), conn_pool_data_(pool_data), metadata_(metadata),
      protocol_(
          NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol(serialization_type)),
      request_complete_(false), response_started_(false), response_complete_(false),
      stream_reset_(false) {}

Router::UpstreamRequest::~UpstreamRequest() = default;

FilterStatus Router::UpstreamRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_data_.newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}

void Router::UpstreamRequest::resetStream() {
  stream_reset_ = true;

  if (conn_pool_handle_) {
    ASSERT(!conn_data_);
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    conn_pool_handle_ = nullptr;
    ENVOY_LOG(debug, "dubbo upstream request: reset connection pool handler");
  }

  if (conn_data_) {
    ASSERT(!conn_pool_handle_);
    conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    conn_data_.reset();
    ENVOY_LOG(debug, "dubbo upstream request: reset connection data");
  }
}

void Router::UpstreamRequest::encodeData(Buffer::Instance& data) {
  ASSERT(conn_data_);
  ASSERT(!conn_pool_handle_);

  ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks_, data.length());
  conn_data_->connection().write(data, false);
}

void Router::UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                            absl::string_view,
                                            Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reason);

  parent_.upstream_request_buffer_.drain(parent_.upstream_request_buffer_.length());

  // If it is a connection error, it means that the connection pool returned
  // the error asynchronously and the upper layer needs to be notified to continue decoding.
  // If it is a non-connection error, it is returned synchronously from the connection pool
  // and is still in the callback at the current Filter, nothing to do.
  if (reason == ConnectionPool::PoolFailureReason::Timeout ||
      reason == ConnectionPool::PoolFailureReason::LocalConnectionFailure ||
      reason == ConnectionPool::PoolFailureReason::RemoteConnectionFailure) {
    if (reason == ConnectionPool::PoolFailureReason::Timeout) {
      host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginTimeout);
    } else {
      host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectFailed);
    }
    parent_.callbacks_->continueDecoding();
  }
}

void Router::UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                          Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "dubbo upstream request: tcp connection has ready");

  // Only invoke continueDecoding if we'd previously stopped the filter chain.
  bool continue_decoding = conn_pool_handle_ != nullptr;

  onUpstreamHostSelected(host);
  host->outlierDetector().putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess);

  conn_data_ = std::move(conn_data);
  conn_data_->addUpstreamCallbacks(parent_);
  conn_pool_handle_ = nullptr;

  onRequestStart(continue_decoding);
  encodeData(parent_.upstream_request_buffer_);
}

void Router::UpstreamRequest::onRequestStart(bool continue_decoding) {
  ENVOY_LOG(debug, "dubbo upstream request: start sending data to the server {}",
            upstream_host_->address()->asString());

  if (continue_decoding) {
    parent_.callbacks_->continueDecoding();
  }
  onRequestComplete();
}

void Router::UpstreamRequest::onRequestComplete() { request_complete_ = true; }

void Router::UpstreamRequest::onResponseComplete() {
  response_complete_ = true;
  conn_data_.reset();
}

void Router::UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "dubbo upstream request: selected upstream {}", host->address()->asString());
  upstream_host_ = host;
}

void Router::UpstreamRequest::onResetStream(ConnectionPool::PoolFailureReason reason) {
  if (metadata_->messageType() == MessageType::Oneway) {
    // For oneway requests, we should not attempt a response. Reset the downstream to signal
    // an error.
    ENVOY_LOG(debug, "dubbo upstream request: the request is oneway, reset downstream stream");
    parent_.callbacks_->resetStream();
    return;
  }

  // When the filter's callback does not end, the sendLocalReply function call
  // triggers the release of the current stream at the end of the filter's callback.
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    parent_.callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError,
                     fmt::format("dubbo upstream request: too many connections")),
        false);
    break;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    parent_.callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError,
                     fmt::format("dubbo upstream request: local connection failure '{}'",
                                 upstream_host_->address()->asString())),
        false);
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    parent_.callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError,
                     fmt::format("dubbo upstream request: remote connection failure '{}'",
                                 upstream_host_->address()->asString())),
        false);
    break;
  case ConnectionPool::PoolFailureReason::Timeout:
    parent_.callbacks_->sendLocalReply(
        AppException(ResponseStatus::ServerError,
                     fmt::format("dubbo upstream request: connection failure '{}' due to timeout",
                                 upstream_host_->address()->asString())),
        false);
    break;
  }

  if (parent_.filter_complete_ && !response_complete_) {
    // When the filter's callback has ended and the reply message has not been processed,
    // call resetStream to release the current stream.
    // the resetStream eventually triggers the onDestroy function call.
    parent_.callbacks_->resetStream();
  }
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
