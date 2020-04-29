#include "extensions/filters/network/rocketmq_proxy/router/router_impl.h"

#include "common/common/enum_to_int.h"

#include "extensions/filters/network/rocketmq_proxy/active_message.h"
#include "extensions/filters/network/rocketmq_proxy/codec.h"
#include "extensions/filters/network/rocketmq_proxy/conn_manager.h"
#include "extensions/filters/network/rocketmq_proxy/protocol.h"
#include "extensions/filters/network/rocketmq_proxy/well_known_names.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {
namespace Router {

RouterImpl::RouterImpl(Envoy::Upstream::ClusterManager& cluster_manager)
    : cluster_manager_(cluster_manager), handle_(nullptr), active_message_(nullptr) {}

RouterImpl::~RouterImpl() {
  if (handle_) {
    handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

Upstream::HostDescriptionConstSharedPtr RouterImpl::upstreamHost() { return upstream_host_; }

void RouterImpl::onAboveWriteBufferHighWatermark() {
  ENVOY_LOG(trace, "Above write buffer high watermark");
}

void RouterImpl::onBelowWriteBufferLowWatermark() {
  ENVOY_LOG(trace, "Below write buffer low watermark");
}

void RouterImpl::onEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose: {
    ENVOY_LOG(error, "Connection to upstream: {} is closed by remote peer",
              upstream_host_->address()->asString());
    // Send local reply to downstream
    active_message_->onError("Connection to upstream is closed by remote peer");
    break;
  }
  case Network::ConnectionEvent::LocalClose: {
    ENVOY_LOG(error, "Connection to upstream: {} has been closed",
              upstream_host_->address()->asString());
    // Send local reply to downstream
    active_message_->onError("Connection to upstream has been closed");
    break;
  }
  default:
    // Ignore other events for now
    ENVOY_LOG(trace, "Ignore event type");
    return;
  }
  active_message_->onReset();
}

const Envoy::Router::MetadataMatchCriteria* RouterImpl::metadataMatchCriteria() {
  if (route_entry_) {
    return route_entry_->metadataMatchCriteria();
  }
  return nullptr;
}

void RouterImpl::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "Received some data from upstream: {} bytes, end_stream: {}", data.length(),
            end_stream);
  if (active_message_->onUpstreamData(data, end_stream, connection_data_)) {
    reset();
  }
}

void RouterImpl::sendRequestToUpstream(ActiveMessage& active_message) {
  active_message_ = &active_message;
  int opaque = active_message_->downstreamRequest()->opaque();
  ASSERT(active_message_->metadata()->hasTopicName());
  std::string topic_name = active_message_->metadata()->topicName();

  RouteConstSharedPtr route = active_message.route();
  if (!route) {
    active_message.onError("No route for current request.");
    ENVOY_LOG(warn, "Can not find route for topic {}", topic_name);
    reset();
    return;
  }

  route_entry_ = route->routeEntry();
  const std::string cluster_name = route_entry_->clusterName();
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(cluster_name);
  if (!cluster) {
    active_message.onError("Cluster does not exist.");
    ENVOY_LOG(warn, "Cluster for {} is not available", cluster_name);
    reset();
    return;
  }

  cluster_info_ = cluster->info();
  if (cluster_info_->maintenanceMode()) {
    ENVOY_LOG(warn, "Cluster {} is under maintenance. Opaque: {}", cluster_name, opaque);
    active_message.onError("Cluster under maintenance.");
    active_message.connectionManager().stats().maintenance_failure_.inc();
    reset();
    return;
  }

  Tcp::ConnectionPool::Instance* conn_pool = cluster_manager_.tcpConnPoolForCluster(
      cluster_name, Upstream::ResourcePriority::Default, this);
  if (!conn_pool) {
    ENVOY_LOG(warn, "No host available for cluster {}. Opaque: {}", cluster_name, opaque);
    active_message.onError("No host available");
    reset();
    return;
  }

  upstream_request_ = std::make_unique<UpstreamRequest>(*this);
  Tcp::ConnectionPool::Cancellable* cancellable = conn_pool->newConnection(*upstream_request_);
  if (cancellable) {
    handle_ = cancellable;
    ENVOY_LOG(trace, "No connection is available for now. Create a cancellable handle. Opaque: {}",
              opaque);
  } else {
    /*
     * UpstreamRequest#onPoolReady or #onPoolFailure should have been invoked.
     */
    ENVOY_LOG(trace,
              "One connection is picked up from connection pool, callback should have been "
              "executed. Opaque: {}",
              opaque);
  }
}

RouterImpl::UpstreamRequest::UpstreamRequest(RouterImpl& router) : router_(router) {}

void RouterImpl::UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                              Upstream::HostDescriptionConstSharedPtr host) {
  router_.connection_data_ = std::move(conn);
  router_.upstream_host_ = host;
  router_.connection_data_->addUpstreamCallbacks(router_);
  if (router_.handle_) {
    ENVOY_LOG(trace, "#onPoolReady, reset cancellable handle to nullptr");
    router_.handle_ = nullptr;
  }
  ENVOY_LOG(debug, "Current chosen host address: {}", host->address()->asString());
  // TODO(lizhanhui): we may optimize out encoding in case we there is no protocol translation.
  Buffer::OwnedImpl buffer;
  Encoder::encode(router_.active_message_->downstreamRequest(), buffer);
  router_.connection_data_->connection().write(buffer, false);
  ENVOY_LOG(trace, "Write data to upstream OK. Opaque: {}",
            router_.active_message_->downstreamRequest()->opaque());

  if (router_.active_message_->metadata()->isOneWay()) {
    ENVOY_LOG(trace,
              "Reset ActiveMessage since data is written and the downstream request is one-way. "
              "Opaque: {}",
              router_.active_message_->downstreamRequest()->opaque());

    // For one-way ack-message requests, we need erase previously stored ack-directive.
    if (enumToSignedInt(RequestCode::AckMessage) ==
        router_.active_message_->downstreamRequest()->code()) {
      auto ack_header = router_.active_message_->downstreamRequest()
                            ->typedCustomHeader<AckMessageRequestHeader>();
      router_.active_message_->connectionManager().eraseAckDirective(ack_header->directiveKey());
    }

    router_.reset();
  }
}

void RouterImpl::UpstreamRequest::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                                Upstream::HostDescriptionConstSharedPtr host) {
  if (router_.handle_) {
    ENVOY_LOG(trace, "#onPoolFailure, reset cancellable handle to nullptr");
    router_.handle_ = nullptr;
  }
  switch (reason) {
  case Tcp::ConnectionPool::PoolFailureReason::Overflow: {
    ENVOY_LOG(error, "Unable to acquire a connection to send request to upstream");
    router_.active_message_->onError("overflow");
  } break;

  case Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure: {
    ENVOY_LOG(error, "Failed to make request to upstream due to remote connection error. Host {}",
              host->address()->asString());
    router_.active_message_->onError("remote connection failure");
  } break;

  case Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure: {
    ENVOY_LOG(error, "Failed to make request to upstream due to local connection error. Host: {}",
              host->address()->asString());
    router_.active_message_->onError("local connection failure");
  } break;

  case Tcp::ConnectionPool::PoolFailureReason::Timeout: {
    ENVOY_LOG(error, "Failed to make request to upstream due to timeout. Host: {}",
              host->address()->asString());
    router_.active_message_->onError("timeout");
  } break;
  }

  // Release resources allocated to this request.
  router_.reset();
}

void RouterImpl::reset() {
  active_message_->onReset();
  if (connection_data_) {
    connection_data_.reset(nullptr);
  }
}

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy