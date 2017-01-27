#include "tcp_proxy.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"
#include "common/common/empty_string.h"
#include "common/json/json_loader.h"

namespace Filter {

TcpProxyConfig::Route::Route(const Json::Object& config) {
  if (config.hasObject("cluster")) {
    cluster_name_ = config.getString("cluster");
  } else {
    throw EnvoyException("tcp proxy: route without cluster");
  }

  if (config.hasObject("source_ip_list")) {
    source_ips_ = Network::IpList(config.getStringArray("source_ip_list"));
  }

  if (config.hasObject("source_ports")) {
    Network::Utility::parsePortRangeList(config.getString("source_ports"), source_port_ranges_);
  }

  if (config.hasObject("destination_ip_list")) {
    destination_ips_ = Network::IpList(config.getStringArray("destination_ip_list"));
  }

  if (config.hasObject("destination_ports")) {
    Network::Utility::parsePortRangeList(config.getString("destination_ports"),
                                         destination_port_ranges_);
  }
}

TcpProxyConfig::TcpProxyConfig(const Json::Object& config,
                               Upstream::ClusterManager& cluster_manager, Stats::Store& stats_store)
    : stats_(generateStats(config.getString("stat_prefix"), stats_store)) {
  config.validateSchema(Json::Schema::TCP_PROXY_NETWORK_FILTER_SCHEMA);

  for (const Json::ObjectPtr& route_desc :
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
        !Network::Utility::portInRangeList(
            Network::Utility::portFromUrl(connection.remoteAddress()), route.source_port_ranges_)) {
      continue; // no match, try next route
    }

    if (!route.source_ips_.empty() &&
        !route.source_ips_.contains(Network::Utility::hostFromUrl(connection.remoteAddress()))) {
      continue; // no match, try next route
    }

    if (!route.destination_port_ranges_.empty() &&
        !Network::Utility::portInRangeList(Network::Utility::portFromUrl(connection.localAddress()),
                                           route.destination_port_ranges_)) {
      continue; // no match, try next route
    }

    if (!route.destination_ips_.empty() &&
        !route.destination_ips_.contains(
            Network::Utility::hostFromUrl(connection.localAddress()))) {
      continue; // no match, try next route
    }

    // if we made it past all checks, the route matches
    return route.cluster_name_;
  }

  // no match, no more routes to try
  return EMPTY_STRING;
}

TcpProxy::TcpProxy(TcpProxyConfigPtr config, Upstream::ClusterManager& cluster_manager)
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

TcpProxyStats TcpProxyConfig::generateStats(const std::string& name, Stats::Store& store) {
  std::string final_prefix = fmt::format("tcp.{}.", name);
  return {ALL_TCP_PROXY_STATS(POOL_COUNTER_PREFIX(store, final_prefix),
                              POOL_GAUGE_PREFIX(store, final_prefix))};
}

void TcpProxy::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  conn_log_info("new tcp proxy session", read_callbacks_->connection());
  config_->stats().downstream_cx_total_.inc();
  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  read_callbacks_->connection().setBufferStats({config_->stats().downstream_cx_rx_bytes_total_,
                                                config_->stats().downstream_cx_rx_bytes_buffered_,
                                                config_->stats().downstream_cx_tx_bytes_total_,
                                                config_->stats().downstream_cx_tx_bytes_buffered_});
}

Network::FilterStatus TcpProxy::initializeUpstreamConnection() {
  const std::string& cluster_name = config_->getRouteFromEntries(read_callbacks_->connection());

  Upstream::ClusterInfoPtr cluster = cluster_manager_.get(cluster_name);

  if (cluster) {
    conn_log_debug("Creating connection to cluster {}", read_callbacks_->connection(),
                   cluster_name);
  } else {
    config_->stats().downstream_cx_no_route_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    cluster->stats().upstream_cx_overflow_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  Upstream::Host::CreateConnectionData conn_info = cluster_manager_.tcpConnForCluster(cluster_name);

  upstream_connection_ = std::move(conn_info.connection_);
  read_callbacks_->upstreamHost(conn_info.host_description_);
  if (!upstream_connection_) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  cluster->resourceManager(Upstream::ResourcePriority::Default).connections().inc();

  upstream_connection_->addReadFilter(upstream_callbacks_);
  upstream_connection_->addConnectionCallbacks(*upstream_callbacks_);
  upstream_connection_->setBufferStats(
      {read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_buffered_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_buffered_});
  upstream_connection_->connect();
  upstream_connection_->noDelay(true);

  connect_timeout_timer_ = read_callbacks_->connection().dispatcher().createTimer(
      [this]() -> void { onConnectTimeout(); });
  connect_timeout_timer_->enableTimer(cluster->connectTimeout());

  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_total_.inc();
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_active_.inc();
  read_callbacks_->upstreamHost()->stats().cx_total_.inc();
  read_callbacks_->upstreamHost()->stats().cx_active_.inc();
  connect_timespan_ =
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_ms_.allocateSpan();
  connected_timespan_ =
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_length_ms_.allocateSpan();

  return Network::FilterStatus::Continue;
}

void TcpProxy::onConnectTimeout() {
  conn_log_debug("connect timeout", read_callbacks_->connection());
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_timeout_.inc();

  // This will close the upstream connection as well.
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

Network::FilterStatus TcpProxy::onData(Buffer::Instance& data) {
  conn_log_trace("received {} bytes", read_callbacks_->connection(), data.length());
  upstream_connection_->write(data);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

void TcpProxy::onDownstreamEvent(uint32_t event) {
  if ((event & Network::ConnectionEvent::RemoteClose ||
       event & Network::ConnectionEvent::LocalClose) &&
      upstream_connection_) {
    // TODO: If we close without flushing here we may drop some data. The downstream connection
    //       is about to go away. So to support this we need to either have a way for the downstream
    //       connection to stick around, or, we need to be able to pass this connection to a flush
    //       worker which will attempt to flush the remaining data with a timeout.
    upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpProxy::onUpstreamData(Buffer::Instance& data) {
  read_callbacks_->connection().write(data);
  ASSERT(0 == data.length());
}

void TcpProxy::onUpstreamEvent(uint32_t event) {
  if (event & Network::ConnectionEvent::RemoteClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_remote_.inc();
  }

  if (event & Network::ConnectionEvent::LocalClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_local_.inc();
  }

  if (event & Network::ConnectionEvent::RemoteClose) {
    if (connect_timeout_timer_) {
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_fail_.inc();
      read_callbacks_->upstreamHost()->stats().cx_connect_fail_.inc();
    }

    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  } else if (event & Network::ConnectionEvent::Connected) {
    connect_timespan_->complete();
  }

  if (connect_timeout_timer_) {
    connect_timeout_timer_->disableTimer();
    connect_timeout_timer_.reset();
  }
}

} // Filter
