#include "extensions/filters/network/dubbo_proxy/router/router_impl.h"

#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "extensions/filters/network/dubbo_proxy/app_exception.h"

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

Network::FilterStatus Router::transportBegin() {
  upstream_request_buffer_.drain(upstream_request_buffer_.length());
  ProtocolDataPassthroughConverter::initProtocolConverter(upstream_request_buffer_);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Router::transportEnd() {
  ASSERT(upstream_request_);

  upstream_request_->encodeData(upstream_request_buffer_, true);

  if (upstream_request_->metadata_->message_type() == MessageType::Oneway) {
    // No response expected
    upstream_request_->onResponseComplete();
    cleanup();
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus Router::messageBegin(MessageType, int64_t, SerializationType) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Router::messageEnd(MessageMetadataSharedPtr metadata) {
  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "dubbo router: no cluster match for interface '{}'", *callbacks_,
                     metadata->service_name());
    callbacks_->sendLocalReply(AppException(AppExceptionType::ServiceNotFound,
                                            fmt::format("dubbo router: no route for interface '{}'",
                                                        metadata->service_name())),
                               true);
    return Network::FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(route_entry_->clusterName());
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "dubbo router: unknown cluster '{}'", *callbacks_,
                     route_entry_->clusterName());
    callbacks_->sendLocalReply(AppException(AppExceptionType::ServerError,
                                            fmt::format("dubbo router: unknown cluster '{}'",
                                                        route_entry_->clusterName())),
                               true);
    return Network::FilterStatus::StopIteration;
  }

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "dubbo router: cluster '{}' match for interface '{}'", *callbacks_,
                   route_entry_->clusterName(), metadata->service_name());

  if (cluster_->maintenanceMode()) {
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::ServerError,
                     fmt::format("dubbo router: maintenance mode for cluster '{}'",
                                 route_entry_->clusterName())),
        true);
    return Network::FilterStatus::StopIteration;
  }

  Tcp::ConnectionPool::Instance* conn_pool = cluster_manager_.tcpConnPoolForCluster(
      route_entry_->clusterName(), Upstream::ResourcePriority::Default, this, nullptr);
  if (!conn_pool) {
    callbacks_->sendLocalReply(
        AppException(
            AppExceptionType::ServerError,
            fmt::format("dubbo router: no healthy upstream for '{}'", route_entry_->clusterName())),
        true);
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_STREAM_LOG(debug, "dubbo router: decoding request", *callbacks_);

  upstream_request_ = std::make_unique<UpstreamRequest>(*this, *conn_pool, metadata,
                                                        callbacks_->downstreamSerializationType(),
                                                        callbacks_->downstreamProtocolType());
  return upstream_request_->start();
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!upstream_request_->response_complete_);

  ENVOY_STREAM_LOG(trace, "dubbo router: reading response: {} bytes", *callbacks_, data.length());

  // Handle normal response.
  if (!upstream_request_->response_started_) {
    callbacks_->startUpstreamResponse(*upstream_request_->deserializer_.get(),
                                      *upstream_request_->protocol_.get());
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
    upstream_request_->resetStream();
    return;
  }

  if (end_stream) {
    // Response is incomplete, but no more data is coming.
    ENVOY_STREAM_LOG(debug, "dubbo router: response underflow", *callbacks_);
    upstream_request_->onResponseComplete();
    upstream_request_->onResetStream(
        Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    cleanup();
  }
}

void Router::onEvent(Network::ConnectionEvent event) {
  if (!upstream_request_ || upstream_request_->response_complete_) {
    // Client closed connection after completing response.
    return;
  }

  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    upstream_request_->onResetStream(
        Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    break;
  case Network::ConnectionEvent::LocalClose:
    upstream_request_->onResetStream(
        Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure);
    break;
  default:
    // Connected is consumed by the connection pool.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

const Network::Connection* Router::downstreamConnection() const {
  return callbacks_ != nullptr ? callbacks_->connection() : nullptr;
}

void Router::cleanup() {
  if (upstream_request_) {
    upstream_request_.reset();
  }
}

Router::UpstreamRequest::UpstreamRequest(Router& parent, Tcp::ConnectionPool::Instance& pool,
                                         MessageMetadataSharedPtr& metadata,
                                         SerializationType serialization_type,
                                         ProtocolType protocol_type)
    : parent_(parent), conn_pool_(pool), metadata_(metadata),
      deserializer_(
          NamedDeserializerConfigFactory::getFactory(serialization_type).createDeserializer()),
      protocol_(NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol()),
      request_complete_(false), response_started_(false), response_complete_(false) {}

Router::UpstreamRequest::~UpstreamRequest() {}

Network::FilterStatus Router::UpstreamRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

void Router::UpstreamRequest::resetStream() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }

  if (conn_data_ != nullptr) {
    conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    conn_data_.reset();
  }
}

void Router::UpstreamRequest::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!conn_data_) {
    ENVOY_STREAM_LOG(trace, "buffering {} bytes", *parent_.callbacks_, data.length());
    if (!buffered_request_body_) {
      buffered_request_body_ = std::make_unique<Envoy::Buffer::OwnedImpl>();
    }

    buffered_request_body_->move(data);
  } else {
    ENVOY_STREAM_LOG(trace, "proxying {} bytes", *parent_.callbacks_, data.length());
    conn_data_->connection().write(data, false);
    if (end_stream) {
      parent_.callbacks_->streamInfo().onLastUpstreamTxByteSent();
    }
  }
}

void Router::UpstreamRequest::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                            Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(warn, "dubbo upstream request: connection failure '{}'", host->address()->asString());

  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reason);

  conn_pool_handle_ = nullptr;
  parent_.upstream_request_buffer_.drain(parent_.upstream_request_buffer_.length());
}

void Router::UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                          Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(debug, "dubbo upstream request: tcp connection has ready");

  // Only invoke continueDecoding if we'd previously stopped the filter chain.
  bool continue_decoding = conn_pool_handle_ != nullptr;

  onUpstreamHostSelected(host);
  conn_data_ = std::move(conn_data);
  conn_data_->addUpstreamCallbacks(parent_);
  conn_pool_handle_ = nullptr;

  onRequestStart(continue_decoding);
}

void Router::UpstreamRequest::onRequestStart(bool continue_decoding) {
  ENVOY_LOG(debug, "dubbo upstream request: start sending data to the server {}",
            upstream_host_->address()->asString());

  if (buffered_request_body_) {
    conn_data_->connection().write(*buffered_request_body_, false);
    parent_.callbacks_->streamInfo().onLastUpstreamTxByteSent();
  }

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

void Router::UpstreamRequest::onResetStream(Tcp::ConnectionPool::PoolFailureReason reason) {
  if (metadata_->message_type() == MessageType::Oneway) {
    // For oneway requests, we should not attempt a response. Reset the downstream to signal
    // an error.
    ENVOY_LOG(debug, "dubbo upstream request: the request is oneway, reset downstream connection");
    parent_.callbacks_->resetDownstreamConnection();
    return;
  }

  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow:
    parent_.callbacks_->sendLocalReply(
        AppException(AppExceptionType::ServerError,
                     fmt::format("dubbo upstream request: too many connections to '{}'",
                                 upstream_host_->address()->asString())),
        true);
    break;
  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    parent_.callbacks_->resetDownstreamConnection();
    break;
  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
  case Tcp::ConnectionPool::PoolFailureReason::Timeout:
    if (!response_started_) {
      parent_.callbacks_->sendLocalReply(
          AppException(AppExceptionType::ServerError,
                       fmt::format("dubbo upstream request: connection failure '{}'",
                                   upstream_host_->address()->asString())),
          true);
      return;
    }

    // Error occurred after a partial response, propagate the reset to the downstream.
    parent_.callbacks_->resetDownstreamConnection();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
