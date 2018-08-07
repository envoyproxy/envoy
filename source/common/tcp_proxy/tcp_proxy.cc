#include "common/tcp_proxy/tcp_proxy.h"

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/config/well_known_names.h"
#include "common/router/metadatamatchcriteria_impl.h"

namespace Envoy {
namespace TcpProxy {

Config::Route::Route(
    const envoy::config::filter::network::tcp_proxy::v2::TcpProxy::DeprecatedV1::TCPRoute& config) {
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

Config::SharedConfig::SharedConfig(
    const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& config,
    Server::Configuration::FactoryContext& context)
    : stats_scope_(context.scope().createScope(fmt::format("tcp.{}.", config.stat_prefix()))),
      stats_(generateStats(*stats_scope_)) {
  if (config.has_idle_timeout()) {
    idle_timeout_ =
        std::chrono::milliseconds(DurationUtil::durationToMilliseconds(config.idle_timeout()));
  }
}

Config::Config(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& config,
               Server::Configuration::FactoryContext& context)
    : max_connect_attempts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connect_attempts, 1)),
      upstream_drain_manager_slot_(context.threadLocal().allocateSlot()),
      shared_config_(std::make_shared<SharedConfig>(config, context)) {

  upstream_drain_manager_slot_->set([](Event::Dispatcher&) {
    return ThreadLocal::ThreadLocalObjectSharedPtr(new UpstreamDrainManager());
  });

  if (config.has_deprecated_v1()) {
    for (const envoy::config::filter::network::tcp_proxy::v2::TcpProxy::DeprecatedV1::TCPRoute&
             route_desc : config.deprecated_v1().routes()) {
      routes_.emplace_back(Route(route_desc));
    }
  }

  if (!config.cluster().empty()) {
    envoy::config::filter::network::tcp_proxy::v2::TcpProxy::DeprecatedV1::TCPRoute default_route;
    default_route.set_cluster(config.cluster());
    routes_.emplace_back(default_route);
  }

  if (config.has_metadata_match()) {
    const auto& filter_metadata = config.metadata_match().filter_metadata();

    const auto filter_it = filter_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

    if (filter_it != filter_metadata.end()) {
      cluster_metadata_match_criteria_ =
          std::make_unique<Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }

  for (const envoy::config::filter::accesslog::v2::AccessLog& log_config : config.access_log()) {
    access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
  }
}

const std::string& Config::getRouteFromEntries(Network::Connection& connection) {
  for (const Config::Route& route : routes_) {
    if (!route.source_port_ranges_.empty() &&
        !Network::Utility::portInRangeList(*connection.remoteAddress(),
                                           route.source_port_ranges_)) {
      continue;
    }

    if (!route.source_ips_.empty() && !route.source_ips_.contains(*connection.remoteAddress())) {
      continue;
    }

    if (!route.destination_port_ranges_.empty() &&
        !Network::Utility::portInRangeList(*connection.localAddress(),
                                           route.destination_port_ranges_)) {
      continue;
    }

    if (!route.destination_ips_.empty() &&
        !route.destination_ips_.contains(*connection.localAddress())) {
      continue;
    }

    // if we made it past all checks, the route matches
    return route.cluster_name_;
  }

  // no match, no more routes to try
  return EMPTY_STRING;
}

UpstreamDrainManager& Config::drainManager() {
  return upstream_drain_manager_slot_->getTyped<UpstreamDrainManager>();
}

Filter::Filter(ConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager), downstream_callbacks_(*this),
      upstream_callbacks_(new UpstreamCallbacks(this)) {
  ASSERT(config != nullptr);
}

Filter::~Filter() {
  getRequestInfo().onRequestComplete();

  for (const auto& access_log : config_->accessLogs()) {
    access_log->log(nullptr, nullptr, nullptr, getRequestInfo());
  }

  if (upstream_connection_) {
    finalizeUpstreamConnectionStats();
  }
}

TcpProxyStats Config::SharedConfig::generateStats(Stats::Scope& scope) {
  return {ALL_TCP_PROXY_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

namespace {
void finalizeConnectionStats(const Upstream::HostDescription& host,
                             Stats::Timespan connected_timespan) {
  host.cluster().stats().upstream_cx_destroy_.inc();
  host.cluster().stats().upstream_cx_active_.dec();
  host.stats().cx_active_.dec();
  host.cluster().resourceManager(Upstream::ResourcePriority::Default).connections().dec();
  connected_timespan.complete();
}
} // namespace

void Filter::finalizeUpstreamConnectionStats() {
  finalizeConnectionStats(*read_callbacks_->upstreamHost(), *connected_timespan_);
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  initialize(callbacks, true);
}

void Filter::initialize(Network::ReadFilterCallbacks& callbacks, bool set_connection_stats) {
  read_callbacks_ = &callbacks;
  ENVOY_CONN_LOG(debug, "new tcp proxy session", read_callbacks_->connection());

  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  read_callbacks_->connection().enableHalfClose(true);
  getRequestInfo().setDownstreamLocalAddress(read_callbacks_->connection().localAddress());
  getRequestInfo().setDownstreamRemoteAddress(read_callbacks_->connection().remoteAddress());

  // Need to disable reads so that we don't write to an upstream that might fail
  // in onData(). This will get re-enabled when the upstream connection is
  // established.
  read_callbacks_->connection().readDisable(true);

  config_->stats().downstream_cx_total_.inc();
  if (set_connection_stats) {
    read_callbacks_->connection().setConnectionStats(
        {config_->stats().downstream_cx_rx_bytes_total_,
         config_->stats().downstream_cx_rx_bytes_buffered_,
         config_->stats().downstream_cx_tx_bytes_total_,
         config_->stats().downstream_cx_tx_bytes_buffered_, nullptr});
  }
}

void Filter::readDisableUpstream(bool disable) {
  if (upstream_connection_ == nullptr ||
      upstream_connection_->state() != Network::Connection::State::Open) {
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

void Filter::readDisableDownstream(bool disable) {
  read_callbacks_->connection().readDisable(disable);

  if (disable) {
    config_->stats().downstream_flow_control_paused_reading_total_.inc();
  } else {
    config_->stats().downstream_flow_control_resumed_reading_total_.inc();
  }
}

void Filter::DownstreamCallbacks::onAboveWriteBufferHighWatermark() {
  ASSERT(!on_high_watermark_called_);
  on_high_watermark_called_ = true;
  // If downstream has too much data buffered, stop reading on the upstream connection.
  parent_.readDisableUpstream(true);
}

void Filter::DownstreamCallbacks::onBelowWriteBufferLowWatermark() {
  ASSERT(on_high_watermark_called_);
  on_high_watermark_called_ = false;
  // The downstream buffer has been drained. Resume reading from upstream.
  parent_.readDisableUpstream(false);
}

void Filter::UpstreamCallbacks::onEvent(Network::ConnectionEvent event) {
  if (drainer_ == nullptr) {
    parent_->onUpstreamEvent(event);
  } else {
    drainer_->onEvent(event);
  }
}

void Filter::UpstreamCallbacks::onAboveWriteBufferHighWatermark() {
  ASSERT(!on_high_watermark_called_);
  on_high_watermark_called_ = true;

  if (parent_ != nullptr) {
    // There's too much data buffered in the upstream write buffer, so stop reading.
    parent_->readDisableDownstream(true);
  }
}

void Filter::UpstreamCallbacks::onBelowWriteBufferLowWatermark() {
  ASSERT(on_high_watermark_called_);
  on_high_watermark_called_ = false;

  if (parent_ != nullptr) {
    // The upstream write buffer is drained. Resume reading.
    parent_->readDisableDownstream(false);
  }
}

Network::FilterStatus Filter::UpstreamCallbacks::onData(Buffer::Instance& data, bool end_stream) {
  if (parent_) {
    parent_->onUpstreamData(data, end_stream);
  } else {
    drainer_->onData(data, end_stream);
  }
  return Network::FilterStatus::StopIteration;
}

void Filter::UpstreamCallbacks::onBytesSent() {
  if (drainer_ == nullptr) {
    parent_->resetIdleTimer();
  } else {
    drainer_->onBytesSent();
  }
}

void Filter::UpstreamCallbacks::onIdleTimeout() {
  if (drainer_ == nullptr) {
    parent_->onIdleTimeout();
  } else {
    drainer_->onIdleTimeout();
  }
}

void Filter::UpstreamCallbacks::drain(Drainer& drainer) {
  ASSERT(drainer_ == nullptr); // This should only get set once.
  drainer_ = &drainer;
  parent_ = nullptr;
}

Network::FilterStatus Filter::initializeUpstreamConnection() {
  ASSERT(upstream_connection_ == nullptr);

  const std::string& cluster_name = getUpstreamCluster();

  Upstream::ThreadLocalCluster* thread_local_cluster = cluster_manager_.get(cluster_name);

  if (thread_local_cluster) {
    ENVOY_CONN_LOG(debug, "Creating connection to cluster {}", read_callbacks_->connection(),
                   cluster_name);
  } else {
    config_->stats().downstream_cx_no_route_.inc();
    getRequestInfo().setResponseFlag(RequestInfo::ResponseFlag::NoRouteFound);
    onInitFailure(UpstreamFailureReason::NO_ROUTE);
    return Network::FilterStatus::StopIteration;
  }

  Upstream::ClusterInfoConstSharedPtr cluster = thread_local_cluster->info();
  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    getRequestInfo().setResponseFlag(RequestInfo::ResponseFlag::UpstreamOverflow);
    cluster->stats().upstream_cx_overflow_.inc();
    onInitFailure(UpstreamFailureReason::RESOURCE_LIMIT_EXCEEDED);
    return Network::FilterStatus::StopIteration;
  }

  const uint32_t max_connect_attempts = config_->maxConnectAttempts();
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
    getRequestInfo().setResponseFlag(RequestInfo::ResponseFlag::NoHealthyUpstream);
    onInitFailure(UpstreamFailureReason::NO_HEALTHY_UPSTREAM);
    return Network::FilterStatus::StopIteration;
  }

  connect_attempts_++;
  cluster->resourceManager(Upstream::ResourcePriority::Default).connections().inc();
  upstream_connection_->addReadFilter(upstream_callbacks_);
  upstream_connection_->addConnectionCallbacks(*upstream_callbacks_);
  upstream_connection_->enableHalfClose(true);
  upstream_connection_->setConnectionStats(
      {read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_buffered_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &read_callbacks_->upstreamHost()->cluster().stats().bind_errors_});
  upstream_connection_->connect();
  upstream_connection_->noDelay(true);
  getRequestInfo().onUpstreamHostSelected(conn_info.host_description_);
  getRequestInfo().setUpstreamLocalAddress(upstream_connection_->localAddress());

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

void Filter::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", read_callbacks_->connection());
  read_callbacks_->upstreamHost()->outlierDetector().putResult(Upstream::Outlier::Result::TIMEOUT);
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_timeout_.inc();
  getRequestInfo().setResponseFlag(RequestInfo::ResponseFlag::UpstreamConnectionFailure);

  // This will cause a LocalClose event to be raised, which will trigger a reconnect if
  // needed/configured.
  upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
}

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "downstream connection received {} bytes, end_stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);
  getRequestInfo().addBytesReceived(data.length());
  upstream_connection_->write(data, end_stream);
  ASSERT(0 == data.length());
  resetIdleTimer(); // TODO(ggreenway) PERF: do we need to reset timer on both send and receive?
  return Network::FilterStatus::StopIteration;
}

void Filter::onDownstreamEvent(Network::ConnectionEvent event) {
  if (upstream_connection_) {
    if (event == Network::ConnectionEvent::RemoteClose) {
      upstream_connection_->close(Network::ConnectionCloseType::FlushWrite);

      if (upstream_connection_ != nullptr &&
          upstream_connection_->state() != Network::Connection::State::Closed) {
        config_->drainManager().add(config_->sharedConfig(), std::move(upstream_connection_),
                                    std::move(upstream_callbacks_), std::move(idle_timer_),
                                    read_callbacks_->upstreamHost(),
                                    std::move(connected_timespan_));
      }
    } else if (event == Network::ConnectionEvent::LocalClose) {
      upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
      disableIdleTimer();
    }
  }
}

void Filter::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "upstream connection received {} bytes, end_stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);
  getRequestInfo().addBytesSent(data.length());
  read_callbacks_->connection().write(data, end_stream);
  ASSERT(0 == data.length());
  resetIdleTimer(); // TODO(ggreenway) PERF: do we need to reset timer on both send and receive?
}

void Filter::onUpstreamEvent(Network::ConnectionEvent event) {
  bool connecting = false;

  // The timer must be cleared before, not after, processing the event because
  // if initializeUpstreamConnection() is called it will reset the timer, so
  // clearing after that call will leave the timer unset.
  if (connect_timeout_timer_) {
    connecting = true;
    connect_timeout_timer_->disableTimer();
    connect_timeout_timer_.reset();
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    finalizeUpstreamConnectionStats();
    read_callbacks_->connection().dispatcher().deferredDelete(std::move(upstream_connection_));
    disableIdleTimer();

    auto& destroy_ctx_stat =
        (event == Network::ConnectionEvent::RemoteClose)
            ? read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_remote_
            : read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_local_;
    destroy_ctx_stat.inc();

    if (connecting) {
      if (event == Network::ConnectionEvent::RemoteClose) {
        getRequestInfo().setResponseFlag(RequestInfo::ResponseFlag::UpstreamConnectionFailure);
        read_callbacks_->upstreamHost()->outlierDetector().putResult(
            Upstream::Outlier::Result::CONNECT_FAILED);
        read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_fail_.inc();
        read_callbacks_->upstreamHost()->stats().cx_connect_fail_.inc();
      }

      initializeUpstreamConnection();
    } else {
      if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
        read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      }
    }
  } else if (event == Network::ConnectionEvent::Connected) {
    connect_timespan_->complete();

    // Re-enable downstream reads now that the upstream connection is established
    // so we have a place to send downstream data to.
    read_callbacks_->connection().readDisable(false);

    read_callbacks_->upstreamHost()->outlierDetector().putResult(
        Upstream::Outlier::Result::SUCCESS);
    onConnectionSuccess();

    if (config_->idleTimeout()) {
      // The idle_timer_ can be moved to a Drainer, so related callbacks call into
      // the UpstreamCallbacks, which has the same lifetime as the timer, and can dispatch
      // the call to either TcpProxy or to Drainer, depending on the current state.
      idle_timer_ =
          read_callbacks_->connection().dispatcher().createTimer([upstream_callbacks =
                                                                      upstream_callbacks_]() {
            upstream_callbacks->onIdleTimeout();
          });
      resetIdleTimer();
      read_callbacks_->connection().addBytesSentCallback([this](uint64_t) { resetIdleTimer(); });
      upstream_connection_->addBytesSentCallback([upstream_callbacks = upstream_callbacks_](
          uint64_t) { upstream_callbacks->onBytesSent(); });
    }
  }
}

void Filter::onIdleTimeout() {
  ENVOY_CONN_LOG(debug, "Session timed out", read_callbacks_->connection());
  config_->stats().idle_timeout_.inc();

  // This results in also closing the upstream connection.
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void Filter::resetIdleTimer() {
  if (idle_timer_ != nullptr) {
    ASSERT(config_->idleTimeout());
    idle_timer_->enableTimer(config_->idleTimeout().value());
  }
}

void Filter::disableIdleTimer() {
  if (idle_timer_ != nullptr) {
    idle_timer_->disableTimer();
    idle_timer_.reset();
  }
}

UpstreamDrainManager::~UpstreamDrainManager() {
  // If connections aren't closed before they are destructed an ASSERT fires,
  // so cancel all pending drains, which causes the connections to be closed.
  while (!drainers_.empty()) {
    auto begin = drainers_.begin();
    Drainer* key = begin->first;
    begin->second->cancelDrain();

    // cancelDrain() should cause that drainer to be removed from drainers_.
    // ASSERT so that we don't end up in an infinite loop.
    ASSERT(drainers_.find(key) == drainers_.end());
  }
}

void UpstreamDrainManager::add(const Config::SharedConfigSharedPtr& config,
                               Network::ClientConnectionPtr&& upstream_connection,
                               const std::shared_ptr<Filter::UpstreamCallbacks>& callbacks,
                               Event::TimerPtr&& idle_timer,
                               const Upstream::HostDescriptionConstSharedPtr& upstream_host,
                               Stats::TimespanPtr&& connected_timespan) {
  DrainerPtr drainer(new Drainer(*this, config, callbacks, std::move(upstream_connection),
                                 std::move(idle_timer), upstream_host,
                                 std::move(connected_timespan)));
  callbacks->drain(*drainer);

  // Use temporary to ensure we get the pointer before we move it out of drainer
  Drainer* ptr = drainer.get();
  drainers_[ptr] = std::move(drainer);
}

void UpstreamDrainManager::remove(Drainer& drainer, Event::Dispatcher& dispatcher) {
  auto it = drainers_.find(&drainer);
  ASSERT(it != drainers_.end());
  dispatcher.deferredDelete(std::move(it->second));
  drainers_.erase(it);
}

Drainer::Drainer(UpstreamDrainManager& parent, const Config::SharedConfigSharedPtr& config,
                 const std::shared_ptr<Filter::UpstreamCallbacks>& callbacks,
                 Network::ClientConnectionPtr&& connection, Event::TimerPtr&& idle_timer,
                 const Upstream::HostDescriptionConstSharedPtr& upstream_host,
                 Stats::TimespanPtr&& connected_timespan)
    : parent_(parent), callbacks_(callbacks), upstream_connection_(std::move(connection)),
      timer_(std::move(idle_timer)), connected_timespan_(std::move(connected_timespan)),
      upstream_host_(upstream_host), config_(config) {
  config_->stats().upstream_flush_total_.inc();
  config_->stats().upstream_flush_active_.inc();
}

void Drainer::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (timer_ != nullptr) {
      timer_->disableTimer();
    }
    config_->stats().upstream_flush_active_.dec();
    finalizeConnectionStats(*upstream_host_, *connected_timespan_);
    parent_.remove(*this, upstream_connection_->dispatcher());
  }
}

void Drainer::onData(Buffer::Instance& data, bool) {
  if (data.length() > 0) {
    // There is no downstream connection to send any data to, but the upstream
    // sent some data. Try to behave similar to what the kernel would do
    // when it receives data on a connection where the application has closed
    // the socket or ::shutdown(fd, SHUT_RD), and close/reset the connection.
    cancelDrain();
  }
}

void Drainer::onIdleTimeout() {
  config_->stats().idle_timeout_.inc();
  cancelDrain();
}

void Drainer::onBytesSent() {
  if (timer_ != nullptr) {
    timer_->enableTimer(config_->idleTimeout().value());
  }
}

void Drainer::cancelDrain() {
  // This sends onEvent(LocalClose).
  upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
}

} // namespace TcpProxy
} // namespace Envoy
