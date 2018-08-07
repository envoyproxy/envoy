#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route)
    : cluster_name_(route.route().cluster()) {}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry() const { return shared_from_this(); }

MethodNameRouteEntryImpl::MethodNameRouteEntryImpl(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route)
    : RouteEntryImplBase(route), method_name_(route.match().method()) {}

RouteConstSharedPtr MethodNameRouteEntryImpl::matches(const MessageMetadata& metadata) const {
  if (method_name_.empty()) {
    return clusterEntry();
  }

  if (metadata.hasMethodName() && metadata.methodName() == method_name_) {
    return clusterEntry();
  }

  return nullptr;
}

RouteMatcher::RouteMatcher(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration& config) {
  for (const auto& route : config.routes()) {
    routes_.emplace_back(new MethodNameRouteEntryImpl(route));
  }
}

RouteConstSharedPtr RouteMatcher::route(const MessageMetadata& metadata) const {
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata);
    if (nullptr != route_entry) {
      return route_entry;
    }
  }

  return nullptr;
}

void Router::onDestroy() {
  if (upstream_request_ != nullptr) {
    upstream_request_->resetStream();
  }
  cleanup();
}

void Router::setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;

  // TODO(zuercher): handle buffer limits
}

void Router::resetUpstreamConnection() {
  if (upstream_request_ != nullptr) {
    upstream_request_->resetStream();
  }
}

ThriftFilters::FilterStatus Router::transportBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return ThriftFilters::FilterStatus::Continue;
}

ThriftFilters::FilterStatus Router::transportEnd() {
  if (upstream_request_->metadata_->messageType() == MessageType::Oneway) {
    // No response expected
    upstream_request_->onResponseComplete();
    cleanup();
  }
  return ThriftFilters::FilterStatus::Continue;
}

ThriftFilters::FilterStatus Router::messageBegin(MessageMetadataSharedPtr metadata) {
  // TODO(zuercher): route stats (e.g., no_route, no_cluster, upstream_rq_maintenance_mode, no
  // healtthy upstream)

  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "no cluster match for method '{}'", *callbacks_,
                     metadata->methodName());
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::UnknownMethod,
                     fmt::format("no route for method '{}'", metadata->methodName())));
    return ThriftFilters::FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(route_entry_->clusterName());
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, route_entry_->clusterName());
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError,
                     fmt::format("unknown cluster '{}'", route_entry_->clusterName())));
    return ThriftFilters::FilterStatus::StopIteration;
  }

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "cluster '{}' match for method '{}'", *callbacks_,
                   route_entry_->clusterName(), metadata->methodName());

  if (cluster_->maintenanceMode()) {
    callbacks_->sendLocalReply(AppException(
        AppExceptionType::InternalError,
        fmt::format("maintenance mode for cluster '{}'", route_entry_->clusterName())));
    return ThriftFilters::FilterStatus::StopIteration;
  }

  Tcp::ConnectionPool::Instance* conn_pool = cluster_manager_.tcpConnPoolForCluster(
      route_entry_->clusterName(), Upstream::ResourcePriority::Default, this);
  if (!conn_pool) {
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError,
                     fmt::format("no healthy upstream for '{}'", route_entry_->clusterName())));
    return ThriftFilters::FilterStatus::StopIteration;
  }

  ENVOY_STREAM_LOG(debug, "router decoding request", *callbacks_);

  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool, metadata));
  upstream_request_->start();
  return ThriftFilters::FilterStatus::StopIteration;
}

ThriftFilters::FilterStatus Router::messageEnd() {
  ProtocolConverter::messageEnd();

  Buffer::OwnedImpl transport_buffer;

  upstream_request_->transport_->encodeFrame(transport_buffer, *upstream_request_->metadata_,
                                             upstream_request_buffer_);
  upstream_request_->conn_data_->connection().write(transport_buffer, false);
  upstream_request_->onRequestComplete();
  return ThriftFilters::FilterStatus::Continue;
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!upstream_request_->response_complete_);

  if (!upstream_request_->response_started_) {
    callbacks_->startUpstreamResponse(upstream_request_->transport_->type(), protocolType());
    upstream_request_->response_started_ = true;
  }

  if (callbacks_->upstreamData(data)) {
    upstream_request_->onResponseComplete();
    cleanup();
    return;
  }

  if (end_stream) {
    // Response is incomplete, but no more data is coming.
    upstream_request_->onResponseComplete();
    upstream_request_->onResetStream(
        Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    cleanup();
  }
}

void Router::onEvent(Network::ConnectionEvent event) {
  ASSERT(upstream_request_ && !upstream_request_->response_complete_);

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
  if (callbacks_ != nullptr) {
    return callbacks_->connection();
  }

  return nullptr;
}

void Router::convertMessageBegin(MessageMetadataSharedPtr metadata) {
  ProtocolConverter::messageBegin(metadata);
}

void Router::cleanup() { upstream_request_.reset(); }

Router::UpstreamRequest::UpstreamRequest(Router& parent, Tcp::ConnectionPool::Instance& pool,
                                         MessageMetadataSharedPtr& metadata)
    : parent_(parent), conn_pool_(pool), metadata_(metadata), request_complete_(false),
      response_started_(false), response_complete_(false) {}

Router::UpstreamRequest::~UpstreamRequest() {}

void Router::UpstreamRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(*this);
  if (handle) {
    conn_pool_handle_ = handle;
  }
}

void Router::UpstreamRequest::resetStream() {
  if (conn_data_ != nullptr) {
    conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    conn_data_ = nullptr;
  }
}

void Router::UpstreamRequest::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                            Upstream::HostDescriptionConstSharedPtr host) {
  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reason);
}

void Router::UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionData& conn_data,
                                          Upstream::HostDescriptionConstSharedPtr host) {
  onUpstreamHostSelected(host);
  conn_data_ = &conn_data;
  conn_data_->addUpstreamCallbacks(parent_);

  conn_pool_handle_ = nullptr;

  // TODO(zuercher): let cluster specify a specific transport and protocol
  transport_ =
      NamedTransportConfigFactory::getFactory(parent_.callbacks_->downstreamTransportType())
          .createTransport();

  parent_.initProtocolConverter(
      NamedProtocolConfigFactory::getFactory(parent_.callbacks_->downstreamProtocolType())
          .createProtocol(),
      parent_.upstream_request_buffer_);

  // TODO(zuercher): need to use an upstream-connection-specific sequence id
  parent_.convertMessageBegin(metadata_);

  parent_.callbacks_->continueDecoding();
}

void Router::UpstreamRequest::onRequestComplete() { request_complete_ = true; }

void Router::UpstreamRequest::onResponseComplete() {
  response_complete_ = true;
  if (conn_data_ != nullptr) {
    conn_data_->release();
  }
  conn_data_ = nullptr;
}

void Router::UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void Router::UpstreamRequest::onResetStream(Tcp::ConnectionPool::PoolFailureReason reason) {
  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow:
    parent_.callbacks_->sendLocalReply(AppException(
        AppExceptionType::InternalError,
        fmt::format("too many connections to '{}'", upstream_host_->address()->asString())));
    break;
  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure:
  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
  case Tcp::ConnectionPool::PoolFailureReason::Timeout:
    // TODO(zuercher): distinguish between these cases where appropriate (particularly timeout)
    if (!response_started_) {
      parent_.callbacks_->sendLocalReply(AppException(
          AppExceptionType::InternalError,
          fmt::format("connection failure '{}'", upstream_host_->address()->asString())));
      return;
    }

    parent_.callbacks_->resetDownstreamConnection();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
