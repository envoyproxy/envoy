#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/common/utility.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route)
    : cluster_name_(route.route().cluster()) {
  for (const auto& header_map : route.match().headers()) {
    config_headers_.push_back(header_map);
  }

  if (route.route().cluster_specifier_case() ==
      envoy::config::filter::network::thrift_proxy::v2alpha1::RouteAction::kWeightedClusters) {

    total_cluster_weight_ = 0UL;
    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      std::unique_ptr<WeightedClusterEntry> cluster_entry(new WeightedClusterEntry(cluster));
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_cluster_weight_ += weighted_clusters_.back()->clusterWeight();
    }
  }
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(uint64_t random_value) const {
  if (weighted_clusters_.empty()) {
    return shared_from_this();
  }

  uint64_t selected_value = random_value % total_cluster_weight_;
  uint64_t begin = 0UL;
  uint64_t end = 0UL;

  // Find the right cluster to route to based on the interval in which
  // the selected value falls. The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
    end = begin + cluster->clusterWeight();
    ASSERT(end <= total_cluster_weight_);

    if (selected_value >= begin && selected_value < end) {
      return cluster;
    }

    begin = end;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool RouteEntryImplBase::headersMatch(const Http::HeaderMap& headers) const {
  return Http::HeaderUtility::matchHeaders(headers, config_headers_);
}

RouteEntryImplBase::WeightedClusterEntry::WeightedClusterEntry(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::WeightedCluster_ClusterWeight&
        cluster)
    : cluster_name_(cluster.name()),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)) {}

MethodNameRouteEntryImpl::MethodNameRouteEntryImpl(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route)
    : RouteEntryImplBase(route), method_name_(route.match().method_name()),
      invert_(route.match().invert()) {
  if (method_name_.empty() && invert_) {
    throw EnvoyException("Cannot have an empty method name with inversion enabled");
  }
}

RouteConstSharedPtr MethodNameRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                      uint64_t random_value) const {
  if (RouteEntryImplBase::headersMatch(metadata.headers())) {
    bool matches =
        method_name_.empty() || (metadata.hasMethodName() && metadata.methodName() == method_name_);

    if (matches ^ invert_) {
      return clusterEntry(random_value);
    }
  }

  return nullptr;
}

ServiceNameRouteEntryImpl::ServiceNameRouteEntryImpl(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route)
    : RouteEntryImplBase(route), invert_(route.match().invert()) {
  const std::string service_name = route.match().service_name();
  if (service_name.empty() && invert_) {
    throw EnvoyException("Cannot have an empty service name with inversion enabled");
  }

  if (!service_name.empty() && !StringUtil::endsWith(service_name, ":")) {
    service_name_ = service_name + ":";
  } else {
    service_name_ = service_name;
  }
}

RouteConstSharedPtr ServiceNameRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                       uint64_t random_value) const {
  if (RouteEntryImplBase::headersMatch(metadata.headers())) {
    bool matches = service_name_.empty() ||
                   (metadata.hasMethodName() &&
                    StringUtil::startsWith(metadata.methodName().c_str(), service_name_));

    if (matches ^ invert_) {
      return clusterEntry(random_value);
    }
  }

  return nullptr;
}

RouteMatcher::RouteMatcher(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration& config) {
  using envoy::config::filter::network::thrift_proxy::v2alpha1::RouteMatch;

  for (const auto& route : config.routes()) {
    switch (route.match().match_specifier_case()) {
    case RouteMatch::MatchSpecifierCase::kMethodName:
      routes_.emplace_back(new MethodNameRouteEntryImpl(route));
      break;
    case RouteMatch::MatchSpecifierCase::kServiceName:
      routes_.emplace_back(new ServiceNameRouteEntryImpl(route));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

RouteConstSharedPtr RouteMatcher::route(const MessageMetadata& metadata,
                                        uint64_t random_value) const {
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata, random_value);
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

FilterStatus Router::transportBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return FilterStatus::Continue;
}

FilterStatus Router::transportEnd() {
  if (upstream_request_->metadata_->messageType() == MessageType::Oneway) {
    // No response expected
    upstream_request_->onResponseComplete();
    cleanup();
  }
  return FilterStatus::Continue;
}

FilterStatus Router::messageBegin(MessageMetadataSharedPtr metadata) {
  // TODO(zuercher): route stats (e.g., no_route, no_cluster, upstream_rq_maintenance_mode, no
  // healtthy upstream)

  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "no cluster match for method '{}'", *callbacks_,
                     metadata->methodName());
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::UnknownMethod,
                     fmt::format("no route for method '{}'", metadata->methodName())));
    return FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(route_entry_->clusterName());
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, route_entry_->clusterName());
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError,
                     fmt::format("unknown cluster '{}'", route_entry_->clusterName())));
    return FilterStatus::StopIteration;
  }

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "cluster '{}' match for method '{}'", *callbacks_,
                   route_entry_->clusterName(), metadata->methodName());

  if (cluster_->maintenanceMode()) {
    callbacks_->sendLocalReply(AppException(
        AppExceptionType::InternalError,
        fmt::format("maintenance mode for cluster '{}'", route_entry_->clusterName())));
    return FilterStatus::StopIteration;
  }

  const std::shared_ptr<const ProtocolOptionsConfig> options =
      cluster_->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(
          NetworkFilterNames::get().ThriftProxy);

  const TransportType transport = options
                                      ? options->transport(callbacks_->downstreamTransportType())
                                      : callbacks_->downstreamTransportType();
  ASSERT(transport != TransportType::Auto);

  const ProtocolType protocol = options ? options->protocol(callbacks_->downstreamProtocolType())
                                        : callbacks_->downstreamProtocolType();
  ASSERT(protocol != ProtocolType::Auto);

  Tcp::ConnectionPool::Instance* conn_pool = cluster_manager_.tcpConnPoolForCluster(
      route_entry_->clusterName(), Upstream::ResourcePriority::Default, this);
  if (!conn_pool) {
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError,
                     fmt::format("no healthy upstream for '{}'", route_entry_->clusterName())));
    return FilterStatus::StopIteration;
  }

  ENVOY_STREAM_LOG(debug, "router decoding request", *callbacks_);

  upstream_request_.reset(new UpstreamRequest(*this, *conn_pool, metadata, transport, protocol));
  return upstream_request_->start();
}

FilterStatus Router::messageEnd() {
  ProtocolConverter::messageEnd();

  Buffer::OwnedImpl transport_buffer;

  upstream_request_->metadata_->setProtocol(upstream_request_->protocol_->type());

  upstream_request_->transport_->encodeFrame(transport_buffer, *upstream_request_->metadata_,
                                             upstream_request_buffer_);
  upstream_request_->conn_data_->connection().write(transport_buffer, false);
  upstream_request_->onRequestComplete();
  return FilterStatus::Continue;
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!upstream_request_->response_complete_);

  if (upstream_request_->upgrade_response_ != nullptr) {
    // Handle upgrade response.
    if (!upstream_request_->upgrade_response_->onData(data)) {
      // Wait for more data.
      return;
    }

    upstream_request_->protocol_->completeUpgrade(*upstream_request_->conn_state_,
                                                  *upstream_request_->upgrade_response_);

    upstream_request_->upgrade_response_.reset();
    upstream_request_->onRequestStart(true);
  } else {
    // Handle normal response.
    if (!upstream_request_->response_started_) {
      callbacks_->startUpstreamResponse(*upstream_request_->transport_,
                                        *upstream_request_->protocol_);
      upstream_request_->response_started_ = true;
    }

    if (callbacks_->upstreamData(data)) {
      upstream_request_->onResponseComplete();
      cleanup();
      return;
    }
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
                                         MessageMetadataSharedPtr& metadata,
                                         TransportType transport_type, ProtocolType protocol_type)
    : parent_(parent), conn_pool_(pool), metadata_(metadata),
      transport_(NamedTransportConfigFactory::getFactory(transport_type).createTransport()),
      protocol_(NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol()),
      request_complete_(false), response_started_(false), response_complete_(false) {}

Router::UpstreamRequest::~UpstreamRequest() {}

FilterStatus Router::UpstreamRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    return FilterStatus::StopIteration;
  }

  if (upgrade_response_ != nullptr) {
    // Pause while we wait for an upgrade response.
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}

void Router::UpstreamRequest::resetStream() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel();
  }

  if (conn_data_ != nullptr) {
    conn_state_ = nullptr;
    conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
    conn_data_.reset();
  }
}

void Router::UpstreamRequest::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                            Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  onResetStream(reason);
}

void Router::UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                          Upstream::HostDescriptionConstSharedPtr host) {
  // Only invoke continueDecoding if we'd previously stopped the filter chain.
  bool continue_decoding = conn_pool_handle_ != nullptr;

  onUpstreamHostSelected(host);
  conn_data_ = std::move(conn_data);
  conn_data_->addUpstreamCallbacks(parent_);
  conn_pool_handle_ = nullptr;

  conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  if (conn_state_ == nullptr) {
    conn_data_->setConnectionState(std::make_unique<ThriftConnectionState>());
    conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  }

  if (protocol_->supportsUpgrade()) {
    upgrade_response_ =
        protocol_->attemptUpgrade(*transport_, *conn_state_, parent_.upstream_request_buffer_);
    if (upgrade_response_ != nullptr) {
      conn_data_->connection().write(parent_.upstream_request_buffer_, false);
      return;
    }
  }

  onRequestStart(continue_decoding);
}

void Router::UpstreamRequest::onRequestStart(bool continue_decoding) {
  parent_.initProtocolConverter(*protocol_, parent_.upstream_request_buffer_);

  metadata_->setSequenceId(conn_state_->nextSequenceId());
  parent_.convertMessageBegin(metadata_);

  if (continue_decoding) {
    parent_.callbacks_->continueDecoding();
  }
}

void Router::UpstreamRequest::onRequestComplete() { request_complete_ = true; }

void Router::UpstreamRequest::onResponseComplete() {
  response_complete_ = true;
  conn_state_ = nullptr;
  conn_data_.reset();
}

void Router::UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void Router::UpstreamRequest::onResetStream(Tcp::ConnectionPool::PoolFailureReason reason) {
  if (metadata_->messageType() == MessageType::Oneway) {
    // For oneway requests, we should not attempt a response. Reset the downstream to signal
    // an error.
    parent_.callbacks_->resetDownstreamConnection();
    return;
  }

  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow:
    parent_.callbacks_->sendLocalReply(AppException(
        AppExceptionType::InternalError,
        fmt::format("too many connections to '{}'", upstream_host_->address()->asString())));
    break;
  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    parent_.callbacks_->resetDownstreamConnection();
    break;
  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
  case Tcp::ConnectionPool::PoolFailureReason::Timeout:
    // TODO(zuercher): distinguish between these cases where appropriate (particularly timeout)
    if (!response_started_) {
      parent_.callbacks_->sendLocalReply(AppException(
          AppExceptionType::InternalError,
          fmt::format("connection failure '{}'", upstream_host_->address()->asString())));
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
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
