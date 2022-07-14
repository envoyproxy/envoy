#include "source/extensions/filters/network/bumping/bumping.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"

namespace Envoy {
namespace Bumping {

Config::SimpleRouteImpl::SimpleRouteImpl(const Config& parent, absl::string_view cluster_name)
    : parent_(parent), cluster_name_(cluster_name) {}  

Config::Config(const envoy::extensions::filters::network::bumping::v3::Bumping& config,
               Server::Configuration::FactoryContext& context)
    : stats_scope_(context.scope().createScope(fmt::format("bumping.{}", config.stat_prefix()))),
      stats_(generateStats(*stats_scope_)),
      max_connect_attempts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connect_attempts, 1)){
  if (!config.cluster().empty()) {
    default_route_ = std::make_shared<const SimpleRouteImpl>(*this, config.cluster());
  }

  for (const envoy::config::accesslog::v3::AccessLog& log_config : config.access_log()) {
    access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
  }
}

BumpingStats Config::generateStats(Stats::Scope& scope) {
  return {ALL_BUMPING_STATS(POOL_COUNTER(scope))};
}

RouteConstSharedPtr Config::getRoute() {
  if (default_route_ != nullptr) {
    return default_route_;
  }

  // no match, no more routes to try
  return nullptr;
}

Filter::Filter(ConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : config_(config), cluster_manager_(cluster_manager),
      upstream_callbacks_(new UpstreamCallbacks(this)) {
  ASSERT(config != nullptr);
}

Filter::~Filter() {
  for (const auto& access_log : config_->accessLogs()) {
    access_log->log(nullptr, nullptr, nullptr, getStreamInfo());
  }
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  ENVOY_CONN_LOG(debug, "new bumping session", read_callbacks_->connection());

  // Need to disable reads and writes to postpone downstream handshakes.
  // This will get re-enabled when transport socket is refreshed with mimic cert.
  //TODO, needs refactoring
  read_callbacks_->connection().readDisable(true);
  read_callbacks_->connection().write_disable = true;
}

StreamInfo::StreamInfo& Filter::getStreamInfo() {
  return read_callbacks_->connection().streamInfo();
}

void Filter::UpstreamCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  parent_->onUpstreamEvent(event);
}

Network::FilterStatus Filter::establishUpstreamConnection() {
  const std::string& cluster_name = route_ ? route_->clusterName() : EMPTY_STRING;
  Upstream::ThreadLocalCluster* thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(cluster_name);

  if (!thread_local_cluster) {
    ENVOY_CONN_LOG(debug, "Cluster not found {}.",
                   read_callbacks_->connection(), cluster_name);
    config_->stats().downstream_cx_no_route_.inc();
    getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoClusterFound);
    onInitFailure(UpstreamFailureReason::NoRoute);
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_CONN_LOG(debug, "Creating connection to cluster {}", read_callbacks_->connection(),
                 cluster_name);

  const Upstream::ClusterInfoConstSharedPtr& cluster = thread_local_cluster->info();

  // Check this here because the TCP conn pool will queue our request waiting for a connection that
  // will never be released.
  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
    cluster->stats().upstream_cx_overflow_.inc();
    onInitFailure(UpstreamFailureReason::ResourceLimitExceeded);
    return Network::FilterStatus::StopIteration;
  }

  const uint32_t max_connect_attempts = config_->maxConnectAttempts();
  if (connect_attempts_ >= max_connect_attempts) {
    getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded);
    cluster->stats().upstream_cx_connect_attempts_exceeded_.inc();
    onInitFailure(UpstreamFailureReason::ConnectFailed);
    return Network::FilterStatus::StopIteration;
  }

  auto& downstream_connection = read_callbacks_->connection();
  auto& filter_state = downstream_connection.streamInfo().filterState();
  transport_socket_options_ = Network::TransportSocketOptionsUtility::fromFilterState(*filter_state);

  if (auto typed_state = filter_state->getDataReadOnly<Network::UpstreamSocketOptionsFilterState>(
          Network::UpstreamSocketOptionsFilterState::key());
      typed_state != nullptr) {
    auto downstream_options = typed_state->value();
    if (!upstream_options_) {
      upstream_options_ = std::make_shared<Network::Socket::Options>();
    }
    Network::Socket::appendOptions(upstream_options_, downstream_options);
  }

  if (!maybeTunnel(*thread_local_cluster)) {
    // Either cluster is unknown or there are no healthy hosts. tcpConnPool() increments
    // cluster->stats().upstream_cx_none_healthy in the latter case.
    getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream);
    onInitFailure(UpstreamFailureReason::NoHealthyUpstream);
  }
  return Network::FilterStatus::StopIteration;
}

bool Filter::maybeTunnel(Upstream::ThreadLocalCluster& cluster) {
  TcpProxy::GenericConnPoolFactory* factory = nullptr;
  if (cluster.info()->upstreamConfig().has_value()) {
    factory = Envoy::Config::Utility::getFactory<TcpProxy::GenericConnPoolFactory>(
        cluster.info()->upstreamConfig().value());
  } else {
    factory = Envoy::Config::Utility::getFactoryByName<TcpProxy::GenericConnPoolFactory>(
        "envoy.filters.connection_pools.tcp.generic");
  }
  if (!factory) {
    return false;
  }

  generic_conn_pool_ = factory->createGenericConnPool(cluster, TcpProxy::TunnelingConfigHelperOptConstRef(),
                                                      this, *upstream_callbacks_);
  if (generic_conn_pool_) {
    connecting_ = true;
    connect_attempts_++;
    getStreamInfo().setAttemptCount(connect_attempts_);
    generic_conn_pool_->newStream(*this);
    // Because we never return open connections to the pool, this either has a handle waiting on
    // connection completion, or onPoolFailure has been invoked. Either way, stop iteration.
    return true;
  }
  return false;
}

void Filter::onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                                  absl::string_view failure_reason,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  generic_conn_pool_.reset();
  read_callbacks_->upstreamHost(host);
  getStreamInfo().upstreamInfo()->setUpstreamHost(host);
  getStreamInfo().upstreamInfo()->setUpstreamTransportFailureReason(failure_reason);

  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
    upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
    break;
  case ConnectionPool::PoolFailureReason::Timeout:
    onConnectTimeout();
    break;
  }
}

void Filter::onGenericPoolReady(StreamInfo::StreamInfo*,
                                std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                                Upstream::HostDescriptionConstSharedPtr&,
                                const Network::Address::InstanceConstSharedPtr&,
                                Ssl::ConnectionInfoConstSharedPtr) {

  // Ssl::ConnectionInfoConstSharedPtr should contains enough info for cert mimicking
  //TODO Cert mimick
  upstream_ = std::move(upstream);
  generic_conn_pool_.reset();
  onUpstreamConnection();
  read_callbacks_->continueReading();
}

void Filter::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", read_callbacks_->connection());
  getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure);

  // Raise LocalClose, which will trigger a reconnect if needed/configured.
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  ASSERT(upstream_ == nullptr);
  route_ = pickRoute();
  return establishUpstreamConnection();
}

void Filter::onUpstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  // Update the connecting flag before processing the event because we may start a new connection
  // attempt in establishUpstreamConnection.
  bool connecting = connecting_;
  connecting_ = false;

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    upstream_.reset();
    disableIdleTimer();

    // happens in on onPoolFailure
    if (connecting) {
      if (event == Network::ConnectionEvent::RemoteClose) {
        getStreamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure);
      }
      establishUpstreamConnection();
    } 
  }
}

void Filter::onUpstreamConnection() {
  connecting_ = false;

  //TODO, interact with Cert Provider

  // Re-enable downstream reads and writes now that the upstream connection is established
  // so we have a place to send downstream data to.
  read_callbacks_->connection().readDisable(false);
  read_callbacks_->connection().write_disable = false;

  ENVOY_CONN_LOG(debug, "TCP:onUpstreamEvent(), requestedServerName: {}",
                 read_callbacks_->connection(),
                 getStreamInfo().downstreamAddressProvider().requestedServerName());
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
} // namespace Bumping
} // namespace Envoy