#include "common/filter/tcp_proxy.h"

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "fmt/format.h"

namespace Envoy {
namespace Filter {

TcpProxyConfig::Route::Route(const Json::Object& config) {
  cluster_name_ = config.getString("cluster");

  if (config.hasObject("source_ip_list")) {
    source_ips_ = Network::Address::IpList(config.getStringArray("source_ip_list"));
  }

  if (config.hasObject("source_ports")) {
    const std::string source_ports = config.getString("source_ports");
    Network::Utility::parsePortRangeList(source_ports, source_port_ranges_);
  }

  if (config.hasObject("destination_ip_list")) {
    destination_ips_ = Network::Address::IpList(config.getStringArray("destination_ip_list"));
  }

  if (config.hasObject("destination_ports")) {
    const std::string destination_ports = config.getString("destination_ports");
    Network::Utility::parsePortRangeList(destination_ports, destination_port_ranges_);
  }
}

TcpProxyConfig::TcpProxyConfig(const Json::Object& config,
                               Upstream::ClusterManager& cluster_manager, Stats::Scope& scope)
    : stats_(generateStats(config.getString("stat_prefix"), scope)) {
  config.validateSchema(Json::Schema::TCP_PROXY_NETWORK_FILTER_SCHEMA);

  for (const Json::ObjectSharedPtr& route_desc :
       config.getObject("route_config")->getObjectArray("routes")) {
    routes_.emplace_back(Route(*route_desc));

    if (!cluster_manager.get(route_desc->getString("cluster"))) {
      throw EnvoyException(fmt::format("tcp proxy: unknown cluster '{}' in TCP route",
                                       route_desc->getString("cluster")));
    }
  }
}

const std::string& TcpProxyConfig::getRouteFromEntries(Network::Connection& connection) {
  for (const TcpProxyConfig::Route& route : routes_) {
    if (!route.source_port_ranges_.empty() &&
        !Network::Utility::portInRangeList(connection.remoteAddress(), route.source_port_ranges_)) {
      continue;
    }

    if (!route.source_ips_.empty() && !route.source_ips_.contains(connection.remoteAddress())) {
      continue;
    }

    if (!route.destination_port_ranges_.empty() &&
        !Network::Utility::portInRangeList(connection.localAddress(),
                                           route.destination_port_ranges_)) {
      continue;
    }

    if (!route.destination_ips_.empty() &&
        !route.destination_ips_.contains(connection.localAddress())) {
      continue;
    }

    // if we made it past all checks, the route matches
    return route.cluster_name_;
  }

  // no match, no more routes to try
  return EMPTY_STRING;
}

TcpProxy::TcpProxy(TcpProxyConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), downstream_callbacks_(*this),
      upstream_callbacks_(new UpstreamCallbacks(*this)) {}

TcpProxy::~TcpProxy() {
  if (upstream_connection_) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_.inc();
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_active_.dec();
    read_callbacks_->upstreamHost()->stats().cx_active_.dec();
    read_callbacks_->upstreamHost()
        ->cluster()
        .resourceManager(Upstream::ResourcePriority::Default)
        .connections()
        .dec();
    connected_timespan_->complete();
  }
}

TcpProxyStats TcpProxyConfig::generateStats(const std::string& name, Stats::Scope& scope) {
  std::string final_prefix = fmt::format("tcp.{}.", name);
  return {ALL_TCP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                              POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void TcpProxy::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  ENVOY_CONN_LOG(info, "new tcp proxy session", read_callbacks_->connection());
  config_->stats().downstream_cx_total_.inc();
  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  read_callbacks_->connection().setConnectionStats(
      {config_->stats().downstream_cx_rx_bytes_total_,
       config_->stats().downstream_cx_rx_bytes_buffered_,
       config_->stats().downstream_cx_tx_bytes_total_,
       config_->stats().downstream_cx_tx_bytes_buffered_, nullptr});
}

void TcpProxy::readDisableUpstream(bool disable) {
  if (upstream_connection_->state() != Network::Connection::State::Open) {
    // Because we flush write downstream, we can have a case where upstream has already disconnected
    // and we are waiting to flush. If we had a watermark event during this time we should no
    // longer touch the upstream connection.
    return;
  }

  upstream_connection_->readDisable(disable);
  if (disable) {
    read_callbacks_->upstreamHost()
        ->cluster()
        .stats()
        .upstream_flow_control_paused_reading_total_.inc();
  } else {
    read_callbacks_->upstreamHost()
        ->cluster()
        .stats()
        .upstream_flow_control_resumed_reading_total_.inc();
  }
}

void TcpProxy::readDisableDownstream(bool disable) {
  read_callbacks_->connection().readDisable(disable);
  // The WsHandlerImpl class uses TCP Proxy code with a null config.
  if (!config_) {
    return;
  }

  if (disable) {
    config_->stats().downstream_flow_control_paused_reading_total_.inc();
  } else {
    config_->stats().downstream_flow_control_resumed_reading_total_.inc();
  }
}

void TcpProxy::DownstreamCallbacks::onAboveWriteBufferHighWatermark() {
  ASSERT(!on_high_watermark_called_);
  on_high_watermark_called_ = true;
  // If downstream has too much data buffered, stop reading on the upstream connection.
  parent_.readDisableUpstream(true);
}

void TcpProxy::DownstreamCallbacks::onBelowWriteBufferLowWatermark() {
  ASSERT(on_high_watermark_called_);
  on_high_watermark_called_ = false;
  // The downstream buffer has been drained.  Resume reading from upstream.
  parent_.readDisableUpstream(false);
}

void TcpProxy::UpstreamCallbacks::onAboveWriteBufferHighWatermark() {
  ASSERT(!on_high_watermark_called_);
  on_high_watermark_called_ = true;
  // There's too much data buffered in the upstream write buffer, so stop reading.
  parent_.readDisableDownstream(true);
}

void TcpProxy::UpstreamCallbacks::onBelowWriteBufferLowWatermark() {
  ASSERT(on_high_watermark_called_);
  on_high_watermark_called_ = false;
  // The upstream write buffer is drained.  Resume reading.
  parent_.readDisableDownstream(false);
}

Network::FilterStatus TcpProxy::initializeUpstreamConnection() {
  const std::string& cluster_name = getUpstreamCluster();

  Upstream::ThreadLocalCluster* thread_local_cluster = cluster_manager_.get(cluster_name);

  if (thread_local_cluster) {
    ENVOY_CONN_LOG(debug, "Creating connection to cluster {}", read_callbacks_->connection(),
                   cluster_name);
  } else {
    if (config_) {
      config_->stats().downstream_cx_no_route_.inc();
    }
    onInitFailure();
    return Network::FilterStatus::StopIteration;
  }

  Upstream::ClusterInfoConstSharedPtr cluster = thread_local_cluster->info();
  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    cluster->stats().upstream_cx_overflow_.inc();
    onInitFailure();
    return Network::FilterStatus::StopIteration;
  }
  Upstream::Host::CreateConnectionData conn_info =
      cluster_manager_.tcpConnForCluster(cluster_name, this);

  upstream_connection_ = std::move(conn_info.connection_);
  read_callbacks_->upstreamHost(conn_info.host_description_);
  if (!upstream_connection_) {
    onInitFailure();
    return Network::FilterStatus::StopIteration;
  }

  onUpstreamHostReady();
  cluster->resourceManager(Upstream::ResourcePriority::Default).connections().inc();
  upstream_connection_->addReadFilter(upstream_callbacks_);
  upstream_connection_->addConnectionCallbacks(*upstream_callbacks_);
  upstream_connection_->setConnectionStats(
      {read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_buffered_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &read_callbacks_->upstreamHost()->cluster().stats().bind_errors_});
  upstream_connection_->connect();
  upstream_connection_->noDelay(true);

  connect_timeout_timer_ = read_callbacks_->connection().dispatcher().createTimer(
      [this]() -> void { onConnectTimeout(); });
  connect_timeout_timer_->enableTimer(cluster->connectTimeout());

  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_total_.inc();
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_active_.inc();
  read_callbacks_->upstreamHost()->stats().cx_total_.inc();
  read_callbacks_->upstreamHost()->stats().cx_active_.inc();
  connect_timespan_.reset(new Stats::Timespan(
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_ms_));
  connected_timespan_.reset(new Stats::Timespan(
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_length_ms_));

  return Network::FilterStatus::Continue;
}

void TcpProxy::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", read_callbacks_->connection());
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_timeout_.inc();

  // This will close the upstream connection as well.
  onConnectTimeoutError();
}

Network::FilterStatus TcpProxy::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "received {} bytes", read_callbacks_->connection(), data.length());
  upstream_connection_->write(data);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

void TcpProxy::onDownstreamEvent(Network::ConnectionEvent event) {
  if ((event == Network::ConnectionEvent::RemoteClose ||
       event == Network::ConnectionEvent::LocalClose) &&
      upstream_connection_) {
    // TODO(mattklein123): If we close without flushing here we may drop some data. The downstream
    // connection is about to go away. So to support this we need to either have a way for the
    // downstream connection to stick around, or, we need to be able to pass this connection to a
    // flush worker which will attempt to flush the remaining data with a timeout.
    upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpProxy::onUpstreamData(Buffer::Instance& data) {
  read_callbacks_->connection().write(data);
  ASSERT(0 == data.length());
}

void TcpProxy::onUpstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_remote_.inc();
    if (connect_timeout_timer_) {
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_fail_.inc();
      read_callbacks_->upstreamHost()->stats().cx_connect_fail_.inc();
    }

    onConnectionFailure();
  } else if (event == Network::ConnectionEvent::LocalClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_local_.inc();
  } else if (event == Network::ConnectionEvent::Connected) {
    connect_timespan_->complete();
    onConnectionSuccess();
  }

  if (connect_timeout_timer_) {
    connect_timeout_timer_->disableTimer();
    connect_timeout_timer_.reset();
  }
}

} // namespace Filter
} // namespace Envoy
