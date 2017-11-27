#include "common/filter/tcp_proxy.h"

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"

#include "api/filter/network/http_connection_manager.pb.h"
#include "fmt/format.h"

namespace Envoy {
namespace Filter {

TcpProxyConfig::Route::Route(
    const envoy::api::v2::filter::network::TcpProxy::DeprecatedV1::TCPRoute& config) {
  cluster_name_ = config.cluster();

  source_ips_ = Network::Address::IpList(config.source_ip_list());
  destination_ips_ = Network::Address::IpList(config.destination_ip_list());

  if (!config.source_ports().empty()) {
    Network::Utility::parsePortRangeList(config.source_ports(), source_port_ranges_);
  }

  if (!config.destination_ports().empty()) {
    Network::Utility::parsePortRangeList(config.destination_ports(), destination_port_ranges_);
  }
}

TcpProxyConfig::TcpProxyConfig(const envoy::api::v2::filter::network::TcpProxy& config,
                               Server::Configuration::FactoryContext& context)
    : stats_(generateStats(config.stat_prefix(), context.scope())),
      max_connect_attempts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connect_attempts, 1)) {

  if (config.has_deprecated_v1()) {
    for (const envoy::api::v2::filter::network::TcpProxy::DeprecatedV1::TCPRoute& route_desc :
         config.deprecated_v1().routes()) {
      if (!context.clusterManager().get(route_desc.cluster())) {
        throw EnvoyException(
            fmt::format("tcp proxy: unknown cluster '{}' in TCP route", route_desc.cluster()));
      }
      routes_.emplace_back(Route(route_desc));
    }
  }

  if (!config.cluster().empty()) {
    envoy::api::v2::filter::network::TcpProxy::DeprecatedV1::TCPRoute default_route;
    default_route.set_cluster(config.cluster());
    routes_.emplace_back(default_route);
  }

  for (const envoy::api::v2::filter::accesslog::AccessLog& log_config : config.access_log()) {
    access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
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

// TODO(ggreenway): refactor this and websocket code so that config_ is always non-null.
TcpProxy::TcpProxy(TcpProxyConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), downstream_callbacks_(*this),
      upstream_callbacks_(new UpstreamCallbacks(*this)) {}

TcpProxy::~TcpProxy() {
  if (config_ != nullptr) {
    for (const auto& access_log : config_->accessLogs()) {
      access_log->log(nullptr, nullptr, request_info_);
    }
  }

  if (upstream_connection_) {
    finalizeUpstreamConnectionStats();
  }
}

TcpProxyStats TcpProxyConfig::generateStats(const std::string& name, Stats::Scope& scope) {
  std::string final_prefix = fmt::format("tcp.{}.", name);
  return {ALL_TCP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                              POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void TcpProxy::finalizeUpstreamConnectionStats() {
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

void TcpProxy::closeUpstreamConnection() {
  finalizeUpstreamConnectionStats();
  upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
  read_callbacks_->connection().dispatcher().deferredDelete(std::move(upstream_connection_));
}

void TcpProxy::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  ENVOY_CONN_LOG(debug, "new tcp proxy session", read_callbacks_->connection());

  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  request_info_.downstream_address_ = read_callbacks_->connection().remoteAddress().asString();

  // Need to disable reads so that we don't write to an upstream that might fail
  // in onData().  This will get re-enabled when the upstream connection is
  // established.
  read_callbacks_->connection().readDisable(true);

  if (!config_) {
    return;
  }
  config_->stats().downstream_cx_total_.inc();
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
    request_info_.setResponseFlag(AccessLog::ResponseFlag::NoRouteFound);
    onInitFailure(UpstreamFailureReason::NO_ROUTE);
    return Network::FilterStatus::StopIteration;
  }

  Upstream::ClusterInfoConstSharedPtr cluster = thread_local_cluster->info();
  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    request_info_.setResponseFlag(AccessLog::ResponseFlag::UpstreamOverflow);
    cluster->stats().upstream_cx_overflow_.inc();
    onInitFailure(UpstreamFailureReason::RESOURCE_LIMIT_EXCEEDED);
    return Network::FilterStatus::StopIteration;
  }

  const uint32_t max_connect_attempts = (config_ != nullptr) ? config_->maxConnectAttempts() : 1;
  if (connect_attempts_ >= max_connect_attempts) {
    cluster->stats().upstream_cx_connect_attempts_exceeded_.inc();
    onInitFailure(UpstreamFailureReason::CONNECT_FAILED);
    return Network::FilterStatus::StopIteration;
  }

  Upstream::Host::CreateConnectionData conn_info =
      cluster_manager_.tcpConnForCluster(cluster_name, this);

  upstream_connection_ = std::move(conn_info.connection_);
  read_callbacks_->upstreamHost(conn_info.host_description_);
  if (!upstream_connection_) {
    // tcpConnForCluster() increments cluster->stats().upstream_cx_none_healthy.
    request_info_.setResponseFlag(AccessLog::ResponseFlag::NoHealthyUpstream);
    onInitFailure(UpstreamFailureReason::NO_HEALTHY_UPSTREAM);
    return Network::FilterStatus::StopIteration;
  }

  connect_attempts_++;
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
  request_info_.onUpstreamHostSelected(conn_info.host_description_);
  request_info_.upstream_local_address_ = upstream_connection_->localAddress().asString();

  ASSERT(connect_timeout_timer_ == nullptr);
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
  request_info_.setResponseFlag(AccessLog::ResponseFlag::UpstreamConnectionFailure);

  closeUpstreamConnection();
  initializeUpstreamConnection();
}

Network::FilterStatus TcpProxy::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "received {} bytes", read_callbacks_->connection(), data.length());
  request_info_.bytes_received_ += data.length();
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
  request_info_.bytes_sent_ += data.length();
  read_callbacks_->connection().write(data);
  ASSERT(0 == data.length());
}

void TcpProxy::onUpstreamEvent(Network::ConnectionEvent event) {
  bool connecting = false;

  // The timer must be cleared before, not after, processing the event because
  // if initializeUpstreamConnection() is called it will reset the timer, so
  // clearing after that call will leave the timer unset.
  if (connect_timeout_timer_) {
    connecting = true;
    connect_timeout_timer_->disableTimer();
    connect_timeout_timer_.reset();
  }

  if (event == Network::ConnectionEvent::RemoteClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_remote_.inc();
    if (connecting) {
      request_info_.setResponseFlag(AccessLog::ResponseFlag::UpstreamConnectionFailure);
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_fail_.inc();
      read_callbacks_->upstreamHost()->stats().cx_connect_fail_.inc();
      closeUpstreamConnection();
      initializeUpstreamConnection();
    } else {
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    }
  } else if (event == Network::ConnectionEvent::LocalClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_local_.inc();
  } else if (event == Network::ConnectionEvent::Connected) {
    connect_timespan_->complete();

    // Re-enable downstream reads now that the upstream connection is established
    // so we have a place to send downstream data to.
    read_callbacks_->connection().readDisable(false);

    onConnectionSuccess();
  }
}

} // namespace Filter
} // namespace Envoy
