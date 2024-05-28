#include "source/common/tcp_proxy/tcp_proxy.h"

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/registry/registry.h"
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
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace TcpProxy {

const std::string& PerConnectionCluster::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.tcp_proxy.cluster");
}

class PerConnectionClusterFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return PerConnectionCluster::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<PerConnectionCluster>(data);
  }
};

REGISTER_FACTORY(PerConnectionClusterFactory, StreamInfo::FilterState::ObjectFactory);

class PerConnectionIdleTimeoutMsObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(PerConnectionIdleTimeoutMs); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    uint64_t duration_in_milliseconds = 0;
    if (absl::SimpleAtoi(data, &duration_in_milliseconds)) {
      return std::make_unique<StreamInfo::UInt64AccessorImpl>(duration_in_milliseconds);
    }

    return nullptr;
  }
};

REGISTER_FACTORY(PerConnectionIdleTimeoutMsObjectFactory, StreamInfo::FilterState::ObjectFactory);

Config::SimpleRouteImpl::SimpleRouteImpl(const Config& parent, absl::string_view cluster_name)
    : parent_(parent), cluster_name_(cluster_name) {}

Config::WeightedClusterEntry::WeightedClusterEntry(
    const Config& parent, const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy::
                              WeightedCluster::ClusterWeight& config)
    : parent_(parent), cluster_name_(config.name()), cluster_weight_(config.weight()) {
  if (config.has_metadata_match()) {
    const auto filter_it = config.metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != config.metadata_match().filter_metadata().end()) {
      if (parent.cluster_metadata_match_criteria_) {
        metadata_match_criteria_ =
            parent.cluster_metadata_match_criteria_->mergeMatchCriteria(filter_it->second);
      } else {
        metadata_match_criteria_ =
            std::make_unique<Router::MetadataMatchCriteriaImpl>(filter_it->second);
      }
    }
  }
}

OnDemandStats OnDemandConfig::generateStats(Stats::Scope& scope) {
  return {ON_DEMAND_TCP_PROXY_STATS(POOL_COUNTER(scope))};
}

Config::SharedConfig::SharedConfig(
    const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config,
    Server::Configuration::FactoryContext& context)
    : stats_scope_(context.scope().createScope(fmt::format("tcp.{}", config.stat_prefix()))),
      stats_(generateStats(*stats_scope_)) {
  if (config.has_idle_timeout()) {
    const uint64_t timeout = DurationUtil::durationToMilliseconds(config.idle_timeout());
    if (timeout > 0) {
      idle_timeout_ = std::chrono::milliseconds(timeout);
    }
  } else {
    idle_timeout_ = std::chrono::hours(1);
  }
  if (config.has_tunneling_config()) {
    tunneling_config_helper_ =
        std::make_unique<TunnelingConfigHelperImpl>(*stats_scope_.get(), config, context);
  }
  if (config.has_max_downstream_connection_duration()) {
    const uint64_t connection_duration =
        DurationUtil::durationToMilliseconds(config.max_downstream_connection_duration());
    max_downstream_connection_duration_ = std::chrono::milliseconds(connection_duration);
  }

  if (config.has_access_log_options()) {
    if (config.flush_access_log_on_connected() /* deprecated */) {
      throw EnvoyException(
          "Only one of flush_access_log_on_connected or access_log_options can be specified.");
    }

    if (config.has_access_log_flush_interval() /* deprecated */) {
      throw EnvoyException(
          "Only one of access_log_flush_interval or access_log_options can be specified.");
    }

    flush_access_log_on_connected_ = config.access_log_options().flush_access_log_on_connected();

    if (config.access_log_options().has_access_log_flush_interval()) {
      const uint64_t flush_interval = DurationUtil::durationToMilliseconds(
          config.access_log_options().access_log_flush_interval());
      access_log_flush_interval_ = std::chrono::milliseconds(flush_interval);
    }
  } else {
    flush_access_log_on_connected_ = config.flush_access_log_on_connected();

    if (config.has_access_log_flush_interval()) {
      const uint64_t flush_interval =
          DurationUtil::durationToMilliseconds(config.access_log_flush_interval());
      access_log_flush_interval_ = std::chrono::milliseconds(flush_interval);
    }
  }

  if (config.has_on_demand() && config.on_demand().has_odcds_config()) {
    on_demand_config_ =
        std::make_unique<OnDemandConfig>(config.on_demand(), context, *stats_scope_);
  }
}

Config::Config(const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config,
               Server::Configuration::FactoryContext& context)
    : max_connect_attempts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connect_attempts, 1)),
      upstream_drain_manager_slot_(context.serverFactoryContext().threadLocal().allocateSlot()),
      shared_config_(std::make_shared<SharedConfig>(config, context)),
      random_generator_(context.serverFactoryContext().api().randomGenerator()),
      regex_engine_(context.serverFactoryContext().regexEngine()) {
  upstream_drain_manager_slot_->set([](Event::Dispatcher&) {
    ThreadLocal::ThreadLocalObjectSharedPtr drain_manager =
        std::make_shared<UpstreamDrainManager>();
    return drain_manager;
  });

  if (!config.cluster().empty()) {
    default_route_ = std::make_shared<const SimpleRouteImpl>(*this, config.cluster());
  }

  if (config.has_metadata_match()) {
    const auto& filter_metadata = config.metadata_match().filter_metadata();

    const auto filter_it = filter_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

    if (filter_it != filter_metadata.end()) {
      cluster_metadata_match_criteria_ =
          std::make_unique<Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }

  // Weighted clusters will be enabled only if the default cluster is absent.
  if (default_route_ == nullptr && config.has_weighted_clusters()) {
    total_cluster_weight_ = 0;
    for (const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy::WeightedCluster::
             ClusterWeight& cluster_desc : config.weighted_clusters().clusters()) {
      WeightedClusterEntryConstSharedPtr cluster_entry(
          std::make_shared<const WeightedClusterEntry>(*this, cluster_desc));
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_cluster_weight_ += weighted_clusters_.back()->clusterWeight();
    }
  }

  for (const envoy::config::accesslog::v3::AccessLog& log_config : config.access_log()) {
    access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
  }

  if (!config.hash_policy().empty()) {
    hash_policy_ = std::make_unique<Network::HashPolicyImpl>(config.hash_policy());
  }
}

RouteConstSharedPtr Config::getRegularRouteFromEntries(Network::Connection& connection) {
  // First check if the per-connection state to see if we need to route to a pre-selected cluster
  if (const auto* per_connection_cluster =
          connection.streamInfo().filterState()->getDataReadOnly<PerConnectionCluster>(
              PerConnectionCluster::key());
      per_connection_cluster != nullptr) {
    return std::make_shared<const SimpleRouteImpl>(*this, per_connection_cluster->value());
  }

  if (default_route_ != nullptr) {
    return default_route_;
  }

  // no match, no more routes to try
  return nullptr;
}

RouteConstSharedPtr Config::getRouteFromEntries(Network::Connection& connection) {
  if (weighted_clusters_.empty()) {
    return getRegularRouteFromEntries(connection);
  }
  return WeightedClusterUtil::pickCluster(weighted_clusters_, total_cluster_weight_,
                                          random_generator_.random(), false);
}

UpstreamDrainManager& Config::drainManager() {
  return upstream_drain_manager_slot_->getTyped<UpstreamDrainManager>();
}

Filter::Filter(ConfigSharedPtr config, Upstream::ClusterManager& cluster_manager)
    : tracing_config_(Tracing::EgressConfig::get()), config_(config),
      cluster_manager_(cluster_manager), downstream_callbacks_(*this),
      upstream_callbacks_(new UpstreamCallbacks(this)),
      upstream_decoder_filter_callbacks_(HttpStreamDecoderFilterCallbacks(this)) {
  ASSERT(config != nullptr);
}

Filter::~Filter() {
  // Disable access log flush timer if it is enabled.
  disableAccessLogFlushTimer();

  // Flush the final end stream access log entry.
  flushAccessLog(AccessLog::AccessLogType::TcpConnectionEnd);

  ASSERT(generic_conn_pool_ == nullptr);
  ASSERT(upstream_ == nullptr);
}

TcpProxyStats Config::SharedConfig::generateStats(Stats::Scope& scope) {
  return {ALL_TCP_PROXY_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  initialize(callbacks, true);
}

void Filter::initialize(Network::ReadFilterCallbacks& callbacks, bool set_connection_stats) {
  read_callbacks_ = &callbacks;
  ENVOY_CONN_LOG(debug, "new tcp proxy session", read_callbacks_->connection());

  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  read_callbacks_->connection().enableHalfClose(true);

  // Check that we are generating only the byte meters we need.
  // The Downstream should be unset and the Upstream should be populated.
  ASSERT(getStreamInfo().getDownstreamBytesMeter() == nullptr);
  ASSERT(getStreamInfo().getUpstreamBytesMeter() != nullptr);

  // Need to disable reads so that we don't write to an upstream that might fail
  // in onData(). This will get re-enabled when the upstream connection is
  // established.
  read_callbacks_->connection().readDisable(true);
  getStreamInfo().setDownstreamBytesMeter(std::make_shared<StreamInfo::BytesMeter>());
  getStreamInfo().setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());

  config_->stats().downstream_cx_total_.inc();
  if (set_connection_stats) {
    read_callbacks_->connection().setConnectionStats(
        {config_->stats().downstream_cx_rx_bytes_total_,
         config_->stats().downstream_cx_rx_bytes_buffered_,
         config_->stats().downstream_cx_tx_bytes_total_,
         config_->stats().downstream_cx_tx_bytes_buffered_, nullptr, nullptr});
  }
}

void Filter::onInitFailure(UpstreamFailureReason reason) {
  // If ODCDS fails, the filter will not attempt to create a connection to
  // upstream, as it does not have an assigned upstream. As such the filter will
  // not have started attempting to connect to an upstream and there is no
  // connection pool callback latency to record.
  if (initial_upstream_connection_start_time_.has_value()) {
    if (!getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency()) {
      getStreamInfo().upstreamInfo()->upstreamTiming().recordConnectionPoolCallbackLatency(
          initial_upstream_connection_start_time_.value(),
          read_callbacks_->connection().dispatcher().timeSource());
    }
  }
  read_callbacks_->connection().close(
      Network::ConnectionCloseType::NoFlush,
      absl::StrCat(StreamInfo::LocalCloseReasons::get().TcpProxyInitializationFailure,
                   enumToInt(reason)));
}

void Filter::readDisableUpstream(bool disable) {
  bool success = false;
  if (upstream_) {
    success = upstream_->readDisable(disable);
  }
  if (!success) {
    return;
  }
  if (disable) {
    read_callbacks_->upstreamHost()
        ->cluster()
        .trafficStats()
        ->upstream_flow_control_paused_reading_total_.inc();
  } else {
    read_callbacks_->upstreamHost()
        ->cluster()
        .trafficStats()
        ->upstream_flow_control_resumed_reading_total_.inc();
  }
}

void Filter::readDisableDownstream(bool disable) {
  if (read_callbacks_->connection().state() != Network::Connection::State::Open) {
    // During idle timeouts, we close both upstream and downstream with NoFlush.
    // Envoy still does a best-effort flush which can case readDisableDownstream to be called
    // despite the downstream connection being closed.
    return;
  }

  const Network::Connection::ReadDisableStatus read_disable_status =
      read_callbacks_->connection().readDisable(disable);

  if (read_disable_status == Network::Connection::ReadDisableStatus::TransitionedToReadDisabled) {
    config_->stats().downstream_flow_control_paused_reading_total_.inc();
  } else if (read_disable_status ==
             Network::Connection::ReadDisableStatus::TransitionedToReadEnabled) {
    config_->stats().downstream_flow_control_resumed_reading_total_.inc();
  }
}

StreamInfo::StreamInfo& Filter::getStreamInfo() {
  return read_callbacks_->connection().streamInfo();
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
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  if (drainer_ == nullptr) {
    parent_->onUpstreamEvent(event);
  } else {
    drainer_->onEvent(event);
  }
}

void Filter::UpstreamCallbacks::onAboveWriteBufferHighWatermark() {
  // TCP Tunneling may call on high/low watermark multiple times.
  ASSERT(parent_->config_->tunnelingConfigHelper() || !on_high_watermark_called_);
  on_high_watermark_called_ = true;

  if (parent_ != nullptr) {
    // There's too much data buffered in the upstream write buffer, so stop reading.
    parent_->readDisableDownstream(true);
  }
}

void Filter::UpstreamCallbacks::onBelowWriteBufferLowWatermark() {
  // TCP Tunneling may call on high/low watermark multiple times.
  ASSERT(parent_->config_->tunnelingConfigHelper() || on_high_watermark_called_);
  on_high_watermark_called_ = false;

  if (parent_ != nullptr) {
    // The upstream write buffer is drained. Resume reading.
    parent_->readDisableDownstream(false);
  }
}

void Filter::UpstreamCallbacks::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (parent_) {
    parent_->onUpstreamData(data, end_stream);
  } else {
    drainer_->onData(data, end_stream);
  }
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

Network::FilterStatus Filter::establishUpstreamConnection() {
  const std::string& cluster_name = route_ ? route_->clusterName() : EMPTY_STRING;
  Upstream::ThreadLocalCluster* thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(cluster_name);

  if (!thread_local_cluster) {
    auto odcds = config_->onDemandCds();
    if (!odcds.has_value()) {
      // No ODCDS? It means that on-demand discovery is disabled.
      ENVOY_CONN_LOG(debug, "Cluster not found {} and no on demand cluster set.",
                     read_callbacks_->connection(), cluster_name);
      config_->stats().downstream_cx_no_route_.inc();
      getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoClusterFound);
      onInitFailure(UpstreamFailureReason::NoRoute);
    } else {
      ASSERT(!cluster_discovery_handle_);
      auto callback = std::make_unique<Upstream::ClusterDiscoveryCallback>(
          [this](Upstream::ClusterDiscoveryStatus cluster_status) {
            onClusterDiscoveryCompletion(cluster_status);
          });
      config_->onDemandStats().on_demand_cluster_attempt_.inc();
      cluster_discovery_handle_ = odcds->requestOnDemandClusterDiscovery(
          cluster_name, std::move(callback), config_->odcdsTimeout());
    }
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_CONN_LOG(debug, "Creating connection to cluster {}", read_callbacks_->connection(),
                 cluster_name);
  if (!initial_upstream_connection_start_time_.has_value()) {
    initial_upstream_connection_start_time_.emplace(
        read_callbacks_->connection().dispatcher().timeSource().monotonicTime());
  }

  const Upstream::ClusterInfoConstSharedPtr& cluster = thread_local_cluster->info();
  getStreamInfo().setUpstreamClusterInfo(cluster);

  // Check this here because the TCP conn pool will queue our request waiting for a connection that
  // will never be released.
  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamOverflow);
    cluster->trafficStats()->upstream_cx_overflow_.inc();
    onInitFailure(UpstreamFailureReason::ResourceLimitExceeded);
    return Network::FilterStatus::StopIteration;
  }

  const uint32_t max_connect_attempts = config_->maxConnectAttempts();
  if (connect_attempts_ >= max_connect_attempts) {
    getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
    cluster->trafficStats()->upstream_cx_connect_attempts_exceeded_.inc();
    onInitFailure(UpstreamFailureReason::ConnectFailed);
    return Network::FilterStatus::StopIteration;
  }

  auto& downstream_connection = read_callbacks_->connection();
  auto& filter_state = downstream_connection.streamInfo().filterState();
  if (!filter_state->hasData<Network::ProxyProtocolFilterState>(
          Network::ProxyProtocolFilterState::key())) {
    filter_state->setData(
        Network::ProxyProtocolFilterState::key(),
        std::make_shared<Network::ProxyProtocolFilterState>(Network::ProxyProtocolData{
            downstream_connection.connectionInfoProvider().remoteAddress(),
            downstream_connection.connectionInfoProvider().localAddress()}),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
  }
  transport_socket_options_ =
      Network::TransportSocketOptionsUtility::fromFilterState(*filter_state);

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
    // cluster->trafficStats()->upstream_cx_none_healthy in the latter case.
    getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoHealthyUpstream);
    onInitFailure(UpstreamFailureReason::NoHealthyUpstream);
  }
  return Network::FilterStatus::StopIteration;
}

void Filter::onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus cluster_status) {
  // Clear the cluster_discovery_handle_ before calling establishUpstreamConnection since we may
  // request cluster again.
  cluster_discovery_handle_.reset();
  const std::string& cluster_name = route_ ? route_->clusterName() : EMPTY_STRING;
  switch (cluster_status) {
  case Upstream::ClusterDiscoveryStatus::Missing:
    ENVOY_CONN_LOG(debug, "On demand cluster {} is missing", read_callbacks_->connection(),
                   cluster_name);
    config_->onDemandStats().on_demand_cluster_missing_.inc();
    break;
  case Upstream::ClusterDiscoveryStatus::Timeout:
    ENVOY_CONN_LOG(debug, "On demand cluster {} was not found before timeout.",
                   read_callbacks_->connection(), cluster_name);
    config_->onDemandStats().on_demand_cluster_timeout_.inc();
    break;
  case Upstream::ClusterDiscoveryStatus::Available:
    // cluster_discovery_handle_ would have been cancelled if the downstream were closed.
    ASSERT(!downstream_closed_);
    ENVOY_CONN_LOG(debug, "On demand cluster {} is found. Establishing connection.",
                   read_callbacks_->connection(), cluster_name);
    config_->onDemandStats().on_demand_cluster_success_.inc();
    establishUpstreamConnection();
    return;
  }
  // Failure path.
  config_->stats().downstream_cx_no_route_.inc();
  getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::NoClusterFound);
  onInitFailure(UpstreamFailureReason::NoRoute);
}

bool Filter::maybeTunnel(Upstream::ThreadLocalCluster& cluster) {
  GenericConnPoolFactory* factory = nullptr;
  if (cluster.info()->upstreamConfig().has_value()) {
    factory = Envoy::Config::Utility::getFactory<GenericConnPoolFactory>(
        cluster.info()->upstreamConfig().ref());
  } else {
    factory = Envoy::Config::Utility::getFactoryByName<GenericConnPoolFactory>(
        "envoy.filters.connection_pools.tcp.generic");
  }
  if (!factory) {
    return false;
  }
  if (Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
    // TODO(vikaschoudhary16): Initialize route_ once per cluster.
    upstream_decoder_filter_callbacks_.route_ = std::make_shared<Http::NullRouteImpl>(
        cluster.info()->name(),
        *std::unique_ptr<const Router::RetryPolicy>{new Router::RetryPolicyImpl()},
        config_->regexEngine());
  }
  generic_conn_pool_ = factory->createGenericConnPool(
      cluster, config_->tunnelingConfigHelper(), this, *upstream_callbacks_,
      upstream_decoder_filter_callbacks_, getStreamInfo());
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
  if (Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
    // generic_conn_pool_ is an instance of TcpProxy::HttpConnPool.
    // generic_conn_pool_->newStream() is called in maybeTunnel() which initializes an instance of
    // Router::UpstreamRequest. If Router::UpstreamRequest receives headers from the upstream which
    // results in end_stream=true, then via callbacks passed to Router::UpstreamRequest,
    // TcpProxy::Filter::onGenericPoolFailure() gets invoked. If we do not do deferredDelete here,
    // then the same instance of UpstreamRequest which is under execution will go out of scope.
    read_callbacks_->connection().dispatcher().deferredDelete(std::move(generic_conn_pool_));
  } else {
    generic_conn_pool_.reset();
  }

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

void Filter::onGenericPoolReady(StreamInfo::StreamInfo* info,
                                std::unique_ptr<GenericUpstream>&& upstream,
                                Upstream::HostDescriptionConstSharedPtr& host,
                                const Network::ConnectionInfoProvider& address_provider,
                                Ssl::ConnectionInfoConstSharedPtr ssl_info) {
  StreamInfo::UpstreamInfo& upstream_info = *getStreamInfo().upstreamInfo();
  if (!upstream_info.upstreamTiming().connectionPoolCallbackLatency()) {
    upstream_info.upstreamTiming().recordConnectionPoolCallbackLatency(
        initial_upstream_connection_start_time_.value(),
        read_callbacks_->connection().dispatcher().timeSource());
  }
  upstream_ = std::move(upstream);
  generic_conn_pool_.reset();
  read_callbacks_->upstreamHost(host);
  // No need to set information using address_provider in case routing via Router::UpstreamRequest
  // because in that case, information is already set by the
  // Router::UpstreamRequest::onPoolReady() method before reaching here.
  if (upstream_info.upstreamLocalAddress() == nullptr) {
    upstream_info.setUpstreamLocalAddress(address_provider.localAddress());
    upstream_info.setUpstreamRemoteAddress(address_provider.remoteAddress());
  }
  upstream_info.setUpstreamHost(host);
  upstream_info.setUpstreamSslConnection(ssl_info);
  onUpstreamConnection();
  read_callbacks_->continueReading();
  if (info) {
    upstream_info.setUpstreamFilterState(info->filterState());
  }
} // namespace TcpProxy

const Router::MetadataMatchCriteria* Filter::metadataMatchCriteria() {
  const Router::MetadataMatchCriteria* route_criteria =
      (route_ != nullptr) ? route_->metadataMatchCriteria() : nullptr;

  const auto& request_metadata = getStreamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = request_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

  if (filter_it != request_metadata.end() && route_criteria != nullptr) {
    metadata_match_criteria_ = route_criteria->mergeMatchCriteria(filter_it->second);
    return metadata_match_criteria_.get();
  } else if (filter_it != request_metadata.end()) {
    metadata_match_criteria_ =
        std::make_unique<Router::MetadataMatchCriteriaImpl>(filter_it->second);
    return metadata_match_criteria_.get();
  } else {
    return route_criteria;
  }
}

const std::string& TunnelResponseHeaders::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.tcp_proxy.propagate_response_headers");
}

const std::string& TunnelResponseTrailers::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.tcp_proxy.propagate_response_trailers");
}

TunnelingConfigHelperImpl::TunnelingConfigHelperImpl(
    Stats::Scope& stats_scope,
    const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config_message,
    Server::Configuration::FactoryContext& context)
    : use_post_(config_message.tunneling_config().use_post()),
      header_parser_(Envoy::Router::HeaderParser::configure(
          config_message.tunneling_config().headers_to_add())),
      propagate_response_headers_(config_message.tunneling_config().propagate_response_headers()),
      propagate_response_trailers_(config_message.tunneling_config().propagate_response_trailers()),
      post_path_(config_message.tunneling_config().post_path()),
      route_stat_name_storage_("tcpproxy_tunneling", context.scope().symbolTable()),
      // TODO(vikaschoudhary16): figure out which of the following router_config_ members are
      // not required by tcp_proxy and move them to a different class
      router_config_(context.serverFactoryContext(), route_stat_name_storage_.statName(),
                     context.serverFactoryContext().localInfo(), stats_scope,
                     context.serverFactoryContext().clusterManager(),
                     context.serverFactoryContext().runtime(),
                     context.serverFactoryContext().api().randomGenerator(),
                     std::make_unique<Router::ShadowWriterImpl>(
                         context.serverFactoryContext().clusterManager()),
                     true, false, false, false, false, false, {},
                     context.serverFactoryContext().api().timeSource(),
                     context.serverFactoryContext().httpContext(),
                     context.serverFactoryContext().routerContext()),
      server_factory_context_(context.serverFactoryContext()) {
  if (!post_path_.empty() && !use_post_) {
    throw EnvoyException("Can't set a post path when POST method isn't used");
  }
  post_path_ = post_path_.empty() ? "/" : post_path_;

  envoy::config::core::v3::SubstitutionFormatString substitution_format_config;
  substitution_format_config.mutable_text_format_source()->set_inline_string(
      config_message.tunneling_config().hostname());
  hostname_fmt_ = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
      substitution_format_config, context);
}

std::string TunnelingConfigHelperImpl::host(const StreamInfo::StreamInfo& stream_info) const {
  return hostname_fmt_->formatWithContext({}, stream_info);
}

void TunnelingConfigHelperImpl::propagateResponseHeaders(
    Http::ResponseHeaderMapPtr&& headers,
    const StreamInfo::FilterStateSharedPtr& filter_state) const {
  if (!propagate_response_headers_) {
    return;
  }
  filter_state->setData(
      TunnelResponseHeaders::key(), std::make_shared<TunnelResponseHeaders>(std::move(headers)),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
}

void TunnelingConfigHelperImpl::propagateResponseTrailers(
    Http::ResponseTrailerMapPtr&& trailers,
    const StreamInfo::FilterStateSharedPtr& filter_state) const {
  if (!propagate_response_trailers_) {
    return;
  }
  filter_state->setData(
      TunnelResponseTrailers::key(), std::make_shared<TunnelResponseTrailers>(std::move(trailers)),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
}

void Filter::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", read_callbacks_->connection());
  read_callbacks_->upstreamHost()->outlierDetector().putResult(
      Upstream::Outlier::Result::LocalOriginTimeout);
  getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);

  // Raise LocalClose, which will trigger a reconnect if needed/configured.
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
}

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "downstream connection received {} bytes, end_stream={}, has upstream {}",
                 read_callbacks_->connection(), data.length(), end_stream, upstream_ != nullptr);
  getStreamInfo().getDownstreamBytesMeter()->addWireBytesReceived(data.length());
  if (upstream_) {
    getStreamInfo().getUpstreamBytesMeter()->addWireBytesSent(data.length());
    upstream_->encodeData(data, end_stream);
  }
  // The upstream should consume all of the data.
  // Before there is an upstream the connection should be readDisabled. If the upstream is
  // destroyed, there should be no further reads as well.
  ASSERT(0 == data.length());
  resetIdleTimer(); // TODO(ggreenway) PERF: do we need to reset timer on both send and receive?
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus Filter::onNewConnection() {
  if (config_->maxDownstreamConnectionDuration()) {
    connection_duration_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onMaxDownstreamConnectionDuration(); });
    connection_duration_timer_->enableTimer(config_->maxDownstreamConnectionDuration().value());
  }

  if (config_->accessLogFlushInterval().has_value()) {
    access_log_flush_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [this]() -> void { onAccessLogFlushInterval(); });
    resetAccessLogFlushTimer();
  }

  // Set UUID for the connection. This is used for logging and tracing.
  getStreamInfo().setStreamIdProvider(
      std::make_shared<StreamInfo::StreamIdProviderImpl>(config_->randomGenerator().uuid()));

  ASSERT(upstream_ == nullptr);
  route_ = pickRoute();
  return establishUpstreamConnection();
}

bool Filter::startUpstreamSecureTransport() {
  bool switched_to_tls = upstream_->startUpstreamSecureTransport();
  if (switched_to_tls) {
    StreamInfo::UpstreamInfo& upstream_info = *getStreamInfo().upstreamInfo();
    upstream_info.setUpstreamSslConnection(upstream_->getUpstreamConnectionSslInfo());
  }
  return switched_to_tls;
}

void Filter::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    downstream_closed_ = true;
    // Cancel the potential odcds callback.
    cluster_discovery_handle_ = nullptr;
  }

  ENVOY_CONN_LOG(trace, "on downstream event {}, has upstream = {}", read_callbacks_->connection(),
                 static_cast<int>(event), upstream_ != nullptr);

  if (upstream_) {
    Tcp::ConnectionPool::ConnectionDataPtr conn_data(upstream_->onDownstreamEvent(event));
    if (conn_data != nullptr &&
        conn_data->connection().state() != Network::Connection::State::Closed) {
      config_->drainManager().add(config_->sharedConfig(), std::move(conn_data),
                                  std::move(upstream_callbacks_), std::move(idle_timer_),
                                  idle_timeout_, read_callbacks_->upstreamHost());
    }
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      upstream_.reset();
      disableIdleTimer();
    }
  }

  if (generic_conn_pool_) {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      // Cancel the conn pool request and close any excess pending requests.
      generic_conn_pool_.reset();
    }
  }
}

void Filter::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "upstream connection received {} bytes, end_stream={}",
                 read_callbacks_->connection(), data.length(), end_stream);
  getStreamInfo().getUpstreamBytesMeter()->addWireBytesReceived(data.length());
  getStreamInfo().getDownstreamBytesMeter()->addWireBytesSent(data.length());
  read_callbacks_->connection().write(data, end_stream);
  ASSERT(0 == data.length());
  resetIdleTimer(); // TODO(ggreenway) PERF: do we need to reset timer on both send and receive?
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
    if (Runtime::runtimeFeatureEnabled(
            "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
      read_callbacks_->connection().dispatcher().deferredDelete(std::move(upstream_));
    } else {
      upstream_.reset();
    }
    disableIdleTimer();

    if (connecting) {
      if (event == Network::ConnectionEvent::RemoteClose) {
        getStreamInfo().setResponseFlag(StreamInfo::UpstreamConnectionFailure);
        // upstreamHost can be nullptr if we received a disconnect from the upstream before
        // receiving any response
        if (read_callbacks_->upstreamHost() != nullptr) {
          read_callbacks_->upstreamHost()->outlierDetector().putResult(
              Upstream::Outlier::Result::LocalOriginConnectFailed);
        }
      }
      if (!downstream_closed_) {
        route_ = pickRoute();
        establishUpstreamConnection();
      }
    } else {
      // TODO(botengyao): propagate RST back to downstream connection if RST is received.
      if (read_callbacks_->connection().state() == Network::Connection::State::Open) {
        read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      }
    }
  }
}

void Filter::onUpstreamConnection() {
  connecting_ = false;
  // Re-enable downstream reads now that the upstream connection is established
  // so we have a place to send downstream data to.
  read_callbacks_->connection().readDisable(false);

  read_callbacks_->upstreamHost()->outlierDetector().putResult(
      Upstream::Outlier::Result::LocalOriginConnectSuccessFinal);

  ENVOY_CONN_LOG(debug, "TCP:onUpstreamEvent(), requestedServerName: {}",
                 read_callbacks_->connection(),
                 getStreamInfo().downstreamAddressProvider().requestedServerName());

  idle_timeout_ = config_->idleTimeout();
  if (const auto* per_connection_idle_timeout =
          getStreamInfo().filterState()->getDataReadOnly<StreamInfo::UInt64Accessor>(
              PerConnectionIdleTimeoutMs);
      per_connection_idle_timeout != nullptr) {
    idle_timeout_ = std::chrono::milliseconds(per_connection_idle_timeout->value());
  }

  if (idle_timeout_) {
    // The idle_timer_ can be moved to a Drainer, so related callbacks call into
    // the UpstreamCallbacks, which has the same lifetime as the timer, and can dispatch
    // the call to either TcpProxy or to Drainer, depending on the current state.
    idle_timer_ = read_callbacks_->connection().dispatcher().createTimer(
        [upstream_callbacks = upstream_callbacks_]() { upstream_callbacks->onIdleTimeout(); });
    resetIdleTimer();
    read_callbacks_->connection().addBytesSentCallback([this](uint64_t) {
      resetIdleTimer();
      return true;
    });
    if (upstream_) {
      upstream_->addBytesSentCallback([upstream_callbacks = upstream_callbacks_](uint64_t) -> bool {
        upstream_callbacks->onBytesSent();
        return true;
      });
    }
  }

  if (config_->flushAccessLogOnConnected()) {
    flushAccessLog(AccessLog::AccessLogType::TcpUpstreamConnected);
  }
}

void Filter::onIdleTimeout() {
  ENVOY_CONN_LOG(debug, "Session timed out", read_callbacks_->connection());
  config_->stats().idle_timeout_.inc();

  // This results in also closing the upstream connection.
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                      StreamInfo::LocalCloseReasons::get().TcpSessionIdleTimeout);
}

void Filter::onMaxDownstreamConnectionDuration() {
  ENVOY_CONN_LOG(debug, "max connection duration reached", read_callbacks_->connection());
  getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::DurationTimeout);
  config_->stats().max_downstream_connection_duration_.inc();
  read_callbacks_->connection().close(
      Network::ConnectionCloseType::NoFlush,
      StreamInfo::LocalCloseReasons::get().MaxConnectionDurationReached);
}

void Filter::onAccessLogFlushInterval() {
  flushAccessLog(AccessLog::AccessLogType::TcpPeriodic);
  const SystemTime now = read_callbacks_->connection().dispatcher().timeSource().systemTime();
  getStreamInfo().getDownstreamBytesMeter()->takeDownstreamPeriodicLoggingSnapshot(now);
  if (getStreamInfo().getUpstreamBytesMeter()) {
    getStreamInfo().getUpstreamBytesMeter()->takeDownstreamPeriodicLoggingSnapshot(now);
  }
  resetAccessLogFlushTimer();
}

void Filter::flushAccessLog(AccessLog::AccessLogType access_log_type) {
  const Formatter::HttpFormatterContext log_context{nullptr, nullptr, nullptr, {}, access_log_type};

  for (const auto& access_log : config_->accessLogs()) {
    access_log->log(log_context, getStreamInfo());
  }
}

void Filter::resetAccessLogFlushTimer() {
  if (access_log_flush_timer_ != nullptr) {
    ASSERT(config_->accessLogFlushInterval().has_value());
    access_log_flush_timer_->enableTimer(config_->accessLogFlushInterval().value());
  }
}

void Filter::disableAccessLogFlushTimer() {
  if (access_log_flush_timer_ != nullptr) {
    access_log_flush_timer_->disableTimer();
    access_log_flush_timer_.reset();
  }
}

void Filter::resetIdleTimer() {
  if (idle_timer_ != nullptr) {
    ASSERT(idle_timeout_);
    idle_timer_->enableTimer(idle_timeout_.value());
  }
}

void Filter::disableIdleTimer() {
  if (idle_timer_ != nullptr) {
    idle_timer_->disableTimer();
    idle_timer_.reset();
  }
}

Filter::HttpStreamDecoderFilterCallbacks::HttpStreamDecoderFilterCallbacks(Filter* parent)
    : parent_(parent), request_trailer_map_(Http::RequestTrailerMapImpl::create()) {}

UpstreamDrainManager::~UpstreamDrainManager() {
  // If connections aren't closed before they are destructed an ASSERT fires,
  // so cancel all pending drains, which causes the connections to be closed.
  if (!drainers_.empty()) {
    auto& dispatcher = drainers_.begin()->second->dispatcher();
    while (!drainers_.empty()) {
      auto begin = drainers_.begin();
      Drainer* key = begin->first;
      begin->second->cancelDrain();

      // cancelDrain() should cause that drainer to be removed from drainers_.
      // ASSERT so that we don't end up in an infinite loop.
      ASSERT(drainers_.find(key) == drainers_.end());
    }

    // This destructor is run when shutting down `ThreadLocal`. The destructor of some objects use
    // earlier `ThreadLocal` slots (for accessing the runtime snapshot) so they must run before that
    // slot is destructed. Clear the list to enforce that ordering.
    dispatcher.clearDeferredDeleteList();
  }
}

void UpstreamDrainManager::add(const Config::SharedConfigSharedPtr& config,
                               Tcp::ConnectionPool::ConnectionDataPtr&& upstream_conn_data,
                               const std::shared_ptr<Filter::UpstreamCallbacks>& callbacks,
                               Event::TimerPtr&& idle_timer,
                               absl::optional<std::chrono::milliseconds> idle_timeout,
                               const Upstream::HostDescriptionConstSharedPtr& upstream_host) {
  DrainerPtr drainer(new Drainer(*this, config, callbacks, std::move(upstream_conn_data),
                                 std::move(idle_timer), idle_timeout, upstream_host));
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
                 Tcp::ConnectionPool::ConnectionDataPtr&& conn_data, Event::TimerPtr&& idle_timer,
                 absl::optional<std::chrono::milliseconds> idle_timeout,
                 const Upstream::HostDescriptionConstSharedPtr& upstream_host)
    : parent_(parent), callbacks_(callbacks), upstream_conn_data_(std::move(conn_data)),
      idle_timer_(std::move(idle_timer)), idle_timeout_(idle_timeout),
      upstream_host_(upstream_host), config_(config) {
  ENVOY_CONN_LOG(trace, "draining the upstream connection", upstream_conn_data_->connection());
  config_->stats().upstream_flush_total_.inc();
  config_->stats().upstream_flush_active_.inc();
}

void Drainer::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (idle_timer_ != nullptr) {
      idle_timer_->disableTimer();
    }
    config_->stats().upstream_flush_active_.dec();
    parent_.remove(*this, upstream_conn_data_->connection().dispatcher());
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
  if (idle_timer_ != nullptr) {
    idle_timer_->enableTimer(idle_timeout_.value());
  }
}

void Drainer::cancelDrain() {
  // This sends onEvent(LocalClose).
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

Event::Dispatcher& Drainer::dispatcher() { return upstream_conn_data_->connection().dispatcher(); }

} // namespace TcpProxy
} // namespace Envoy
