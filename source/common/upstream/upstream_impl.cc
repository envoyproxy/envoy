#include "common/upstream/upstream_impl.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/protocol_json.h"
#include "common/config/tls_context_json.h"
#include "common/config/utility.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/eds.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/logical_dns_cluster.h"
#include "common/upstream/original_dst_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Upstream {
namespace {

const Network::Address::InstanceConstSharedPtr
getSourceAddress(const envoy::api::v2::Cluster& cluster,
                 const envoy::api::v2::core::BindConfig& bind_config) {
  // The source address from cluster config takes precedence.
  if (cluster.upstream_bind_config().has_source_address()) {
    return Network::Address::resolveProtoSocketAddress(
        cluster.upstream_bind_config().source_address());
  }
  // If there's no source address in the cluster config, use any default from the bootstrap proto.
  if (bind_config.has_source_address()) {
    return Network::Address::resolveProtoSocketAddress(bind_config.source_address());
  }

  return nullptr;
}

uint64_t parseFeatures(const envoy::api::v2::Cluster& config) {
  uint64_t features = 0;
  if (config.has_http2_protocol_options()) {
    features |= ClusterInfoImpl::Features::HTTP2;
  }
  if (config.protocol_selection() == envoy::api::v2::Cluster::USE_DOWNSTREAM_PROTOCOL) {
    features |= ClusterInfoImpl::Features::USE_DOWNSTREAM_PROTOCOL;
  }
  if (config.close_connections_on_host_health_failure()) {
    features |= ClusterInfoImpl::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE;
  }
  return features;
}

Network::TcpKeepaliveConfig parseTcpKeepaliveConfig(const envoy::api::v2::Cluster& config) {
  const envoy::api::v2::core::TcpKeepalive& options =
      config.upstream_connection_options().tcp_keepalive();
  return Network::TcpKeepaliveConfig{
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_probes, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_time, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_interval, absl::optional<uint32_t>())};
}

const Network::ConnectionSocket::OptionsSharedPtr
parseClusterSocketOptions(const envoy::api::v2::Cluster& config,
                          const envoy::api::v2::core::BindConfig bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr cluster_options =
      std::make_shared<Network::ConnectionSocket::Options>();
  // Cluster IP_FREEBIND settings, when set, will override the cluster manager wide settings.
  if ((bind_config.freebind().value() && !config.upstream_bind_config().has_freebind()) ||
      config.upstream_bind_config().freebind().value()) {
    Network::Socket::appendOptions(cluster_options,
                                   Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (config.upstream_connection_options().has_tcp_keepalive()) {
    Network::Socket::appendOptions(
        cluster_options,
        Network::SocketOptionFactory::buildTcpKeepaliveOptions(parseTcpKeepaliveConfig(config)));
  }
  // Cluster socket_options trump cluster manager wide.
  if (bind_config.socket_options().size() + config.upstream_bind_config().socket_options().size() >
      0) {
    auto socket_options = config.upstream_bind_config().socket_options().size() > 0
                              ? config.upstream_bind_config().socket_options()
                              : bind_config.socket_options();
    Network::Socket::appendOptions(
        cluster_options, Network::SocketOptionFactory::buildLiteralOptions(socket_options));
  }
  if (cluster_options->empty()) {
    return nullptr;
  }
  return cluster_options;
}

std::map<std::string, ProtocolOptionsConfigConstSharedPtr>
parseExtensionProtocolOptions(const envoy::api::v2::Cluster& config) {
  std::map<std::string, ProtocolOptionsConfigConstSharedPtr> options;
  for (const auto& iter : config.extension_protocol_options()) {
    const std::string& name = iter.first;
    const ProtobufWkt::Struct& config_struct = iter.second;

    auto& factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::NamedNetworkFilterConfigFactory>(name);

    auto object = factory.createProtocolOptionsConfig(
        *Envoy::Config::Utility::translateToFactoryProtocolOptionsConfig(config_struct, factory));
    if (object) {
      options[name] = object;
    }
  }

  return options;
}

} // namespace

Host::CreateConnectionData
HostImpl::createConnection(Event::Dispatcher& dispatcher,
                           const Network::ConnectionSocket::OptionsSharedPtr& options) const {
  return {createConnection(dispatcher, *cluster_, address_, options), shared_from_this()};
}

Host::CreateConnectionData
HostImpl::createHealthCheckConnection(Event::Dispatcher& dispatcher) const {
  return {createConnection(dispatcher, *cluster_, healthCheckAddress(), nullptr),
          shared_from_this()};
}

Network::ClientConnectionPtr
HostImpl::createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                           Network::Address::InstanceConstSharedPtr address,
                           const Network::ConnectionSocket::OptionsSharedPtr& options) {
  Network::ConnectionSocket::OptionsSharedPtr connection_options;
  if (cluster.clusterSocketOptions() != nullptr) {
    if (options) {
      connection_options = std::make_shared<Network::ConnectionSocket::Options>();
      *connection_options = *options;
      std::copy(cluster.clusterSocketOptions()->begin(), cluster.clusterSocketOptions()->end(),
                std::back_inserter(*connection_options));
    } else {
      connection_options = cluster.clusterSocketOptions();
    }
  } else {
    connection_options = options;
  }

  Network::ClientConnectionPtr connection = dispatcher.createClientConnection(
      address, cluster.sourceAddress(), cluster.transportSocketFactory().createTransportSocket(),
      connection_options);
  connection->setBufferLimits(cluster.perConnectionBufferLimitBytes());
  return connection;
}

void HostImpl::weight(uint32_t new_weight) { weight_ = std::max(1U, std::min(128U, new_weight)); }

HostsPerLocalityConstSharedPtr
HostsPerLocalityImpl::filter(std::function<bool(const Host&)> predicate) const {
  auto* filtered_clone = new HostsPerLocalityImpl();
  HostsPerLocalityConstSharedPtr shared_filtered_clone{filtered_clone};

  filtered_clone->local_ = local_;
  for (const auto& hosts_locality : hosts_per_locality_) {
    HostVector current_locality_hosts;
    for (const auto& host : hosts_locality) {
      if (predicate(*host)) {
        current_locality_hosts.emplace_back(host);
      }
    }
    filtered_clone->hosts_per_locality_.push_back(std::move(current_locality_hosts));
  }

  return shared_filtered_clone;
}

void HostSetImpl::updateHosts(HostVectorConstSharedPtr hosts,
                              HostVectorConstSharedPtr healthy_hosts,
                              HostsPerLocalityConstSharedPtr hosts_per_locality,
                              HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                              LocalityWeightsConstSharedPtr locality_weights,
                              const HostVector& hosts_added, const HostVector& hosts_removed) {
  hosts_ = std::move(hosts);
  healthy_hosts_ = std::move(healthy_hosts);
  hosts_per_locality_ = std::move(hosts_per_locality);
  healthy_hosts_per_locality_ = std::move(healthy_hosts_per_locality);
  locality_weights_ = std::move(locality_weights);
  // Rebuild the locality scheduler by computing the effective weight of each
  // locality in this priority. The scheduler is reset by default, and is rebuilt only if we have
  // locality weights (i.e. using EDS) and there is at least one healthy host in this priority.
  //
  // We omit building a scheduler when there are zero healthy hosts in the priority as all
  // the localities will have zero effective weight. At selection time, we'll only ever try
  // to select a host from such a priority if all priorities have zero healthy hosts. At
  // that point we'll rely on other mechanisms such as panic mode to select a host,
  // none of which rely on the scheduler.
  //
  // TODO(htuch): if the underlying locality index ->
  // envoy::api::v2::core::Locality hasn't changed in hosts_/healthy_hosts_, we
  // could just update locality_weight_ without rebuilding. Similar to how host
  // level WRR works, we would age out the existing entries via picks and lazily
  // apply the new weights.
  locality_scheduler_ = nullptr;
  if (hosts_per_locality_ != nullptr && locality_weights_ != nullptr &&
      !locality_weights_->empty() && !healthy_hosts_->empty()) {
    locality_scheduler_ = std::make_unique<EdfScheduler<LocalityEntry>>();
    locality_entries_.clear();
    for (uint32_t i = 0; i < hosts_per_locality_->get().size(); ++i) {
      const double effective_weight = effectiveLocalityWeight(i);
      if (effective_weight > 0) {
        locality_entries_.emplace_back(std::make_shared<LocalityEntry>(i, effective_weight));
        locality_scheduler_->add(effective_weight, locality_entries_.back());
      }
    }
    // If all effective weights were zero, reset the scheduler.
    if (locality_scheduler_->empty()) {
      locality_scheduler_ = nullptr;
    }
  }
  runUpdateCallbacks(hosts_added, hosts_removed);
}

absl::optional<uint32_t> HostSetImpl::chooseLocality() {
  if (locality_scheduler_ == nullptr) {
    return {};
  }
  const std::shared_ptr<LocalityEntry> locality = locality_scheduler_->pick();
  // We don't build a schedule if there are no weighted localities, so we should always succeed.
  ASSERT(locality != nullptr);
  // If we picked it before, its weight must have been positive.
  ASSERT(locality->effective_weight_ > 0);
  locality_scheduler_->add(locality->effective_weight_, locality);
  return locality->index_;
}

double HostSetImpl::effectiveLocalityWeight(uint32_t index) const {
  ASSERT(locality_weights_ != nullptr);
  ASSERT(hosts_per_locality_ != nullptr);
  const auto& locality_hosts = hosts_per_locality_->get()[index];
  const auto& locality_healthy_hosts = healthy_hosts_per_locality_->get()[index];
  ASSERT(!locality_hosts.empty());
  const double locality_healthy_ratio = 1.0 * locality_healthy_hosts.size() / locality_hosts.size();
  const uint32_t weight = (*locality_weights_)[index];
  // Health ranges from 0-1.0, and is the ratio of healthy hosts to total hosts, modified by the
  // somewhat arbitrary overprovision factor of kOverProvisioningFactor.
  // Eventually the overprovision factor will likely be made configurable.
  const double effective_locality_health_ratio =
      std::min(1.0, (kOverProvisioningFactor / 100.0) * locality_healthy_ratio);
  return weight * effective_locality_health_ratio;
}

HostSet& PrioritySetImpl::getOrCreateHostSet(uint32_t priority) {
  if (host_sets_.size() < priority + 1) {
    for (size_t i = host_sets_.size(); i <= priority; ++i) {
      HostSetImplPtr host_set = createHostSet(i);
      host_set->addMemberUpdateCb([this](uint32_t priority, const HostVector& hosts_added,
                                         const HostVector& hosts_removed) {
        runUpdateCallbacks(priority, hosts_added, hosts_removed);
      });
      host_sets_.push_back(std::move(host_set));
    }
  }
  return *host_sets_[priority];
}

ClusterStats ClusterInfoImpl::generateStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_HISTOGRAM(scope))};
}

ClusterLoadReportStats ClusterInfoImpl::generateLoadReportStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_LOAD_REPORT_STATS(POOL_COUNTER(scope))};
}

ClusterInfoImpl::ClusterInfoImpl(const envoy::api::v2::Cluster& config,
                                 const envoy::api::v2::core::BindConfig& bind_config,
                                 Runtime::Loader& runtime,
                                 Network::TransportSocketFactoryPtr&& socket_factory,
                                 Stats::ScopePtr&& stats_scope, bool added_via_api)
    : runtime_(runtime), name_(config.name()), type_(config.type()),
      max_requests_per_connection_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_requests_per_connection, 0)),
      connect_timeout_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(config, connect_timeout))),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      transport_socket_factory_(std::move(socket_factory)), stats_scope_(std::move(stats_scope)),
      stats_(generateStats(*stats_scope_)),
      load_report_stats_(generateLoadReportStats(load_report_stats_store_)),
      features_(parseFeatures(config)),
      http2_settings_(Http::Utility::parseHttp2Settings(config.http2_protocol_options())),
      extension_protocol_options_(parseExtensionProtocolOptions(config)),
      resource_managers_(config, runtime, name_),
      maintenance_mode_runtime_key_(fmt::format("upstream.maintenance_mode.{}", name_)),
      source_address_(getSourceAddress(config, bind_config)),
      lb_ring_hash_config_(config.ring_hash_lb_config()),
      lb_original_dst_config_(config.original_dst_lb_config()), added_via_api_(added_via_api),
      lb_subset_(LoadBalancerSubsetInfoImpl(config.lb_subset_config())),
      metadata_(config.metadata()), common_lb_config_(config.common_lb_config()),
      cluster_socket_options_(parseClusterSocketOptions(config, bind_config)),
      drain_connections_on_host_removal_(config.drain_connections_on_host_removal()) {

  switch (config.lb_policy()) {
  case envoy::api::v2::Cluster::ROUND_ROBIN:
    lb_type_ = LoadBalancerType::RoundRobin;
    break;
  case envoy::api::v2::Cluster::LEAST_REQUEST:
    lb_type_ = LoadBalancerType::LeastRequest;
    break;
  case envoy::api::v2::Cluster::RANDOM:
    lb_type_ = LoadBalancerType::Random;
    break;
  case envoy::api::v2::Cluster::RING_HASH:
    lb_type_ = LoadBalancerType::RingHash;
    break;
  case envoy::api::v2::Cluster::ORIGINAL_DST_LB:
    if (config.type() != envoy::api::v2::Cluster::ORIGINAL_DST) {
      throw EnvoyException(fmt::format(
          "cluster: LB type 'original_dst_lb' may only be used with cluser type 'original_dst'"));
    }
    lb_type_ = LoadBalancerType::OriginalDst;
    break;
  case envoy::api::v2::Cluster::MAGLEV:
    lb_type_ = LoadBalancerType::Maglev;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  if (config.protocol_selection() == envoy::api::v2::Cluster::USE_CONFIGURED_PROTOCOL) {
    // Make sure multiple protocol configurations are not present
    if (config.has_http_protocol_options() && config.has_http2_protocol_options()) {
      throw EnvoyException(fmt::format("cluster: Both HTTP1 and HTTP2 options may only be "
                                       "configured with non-default 'protocol_selection' values"));
    }
  }

  if (config.common_http_protocol_options().has_idle_timeout()) {
    idle_timeout_ = std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(config.common_http_protocol_options().idle_timeout()));
  }
}

ProtocolOptionsConfigConstSharedPtr
ClusterInfoImpl::extensionProtocolOptions(const std::string& name) const {
  auto i = extension_protocol_options_.find(name);
  if (i != extension_protocol_options_.end()) {
    return i->second;
  }

  return nullptr;
}

namespace {

Stats::ScopePtr generateStatsScope(const envoy::api::v2::Cluster& config, Stats::Store& stats) {
  return stats.createScope(fmt::format(
      "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
}

} // namespace

Network::TransportSocketFactoryPtr createTransportSocketFactory(
    const envoy::api::v2::Cluster& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  // If the cluster config doesn't have a transport socket configured, override with the default
  // transport socket implementation based on the tls_context. We copy by value first then override
  // if necessary.
  auto transport_socket = config.transport_socket();
  if (!config.has_transport_socket()) {
    if (config.has_tls_context()) {
      transport_socket.set_name(Extensions::TransportSockets::TransportSocketNames::get().Tls);
      MessageUtil::jsonConvert(config.tls_context(), *transport_socket.mutable_config());
    } else {
      transport_socket.set_name(
          Extensions::TransportSockets::TransportSocketNames::get().RawBuffer);
    }
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(transport_socket.name());
  ProtobufTypes::MessagePtr message =
      Config::Utility::translateToFactoryConfig(transport_socket, config_factory);
  return config_factory.createTransportSocketFactory(*message, factory_context);
}

ClusterSharedPtr ClusterImplBase::create(
    const envoy::api::v2::Cluster& cluster, ClusterManager& cm, Stats::Store& stats,
    ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
    Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
    AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) {
  std::unique_ptr<ClusterImplBase> new_cluster;

  // We make this a shared pointer to deal with the distinct ownership
  // scenarios that can exist: in one case, we pass in the "default"
  // DNS resolver that is owned by the Server::Instance. In the case
  // where 'dns_resolvers' is specified, we have per-cluster DNS
  // resolvers that are created here but ownership resides with
  // StrictDnsClusterImpl/LogicalDnsCluster.
  auto selected_dns_resolver = dns_resolver;
  if (!cluster.dns_resolvers().empty()) {
    const auto& resolver_addrs = cluster.dns_resolvers();
    std::vector<Network::Address::InstanceConstSharedPtr> resolvers;
    resolvers.reserve(resolver_addrs.size());
    for (const auto& resolver_addr : resolver_addrs) {
      resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
    }
    selected_dns_resolver = dispatcher.createDnsResolver(resolvers);
  }

  auto stats_scope = generateStatsScope(cluster, stats);
  Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *stats_scope, cm, local_info, dispatcher, random, stats);

  switch (cluster.type()) {
  case envoy::api::v2::Cluster::STATIC:
    new_cluster.reset(new StaticClusterImpl(cluster, runtime, factory_context,
                                            std::move(stats_scope), added_via_api));
    break;
  case envoy::api::v2::Cluster::STRICT_DNS:
    new_cluster.reset(new StrictDnsClusterImpl(cluster, runtime, selected_dns_resolver,
                                               factory_context, std::move(stats_scope),
                                               added_via_api));
    break;
  case envoy::api::v2::Cluster::LOGICAL_DNS:
    new_cluster.reset(new LogicalDnsCluster(cluster, runtime, selected_dns_resolver, tls,
                                            factory_context, std::move(stats_scope),
                                            added_via_api));
    break;
  case envoy::api::v2::Cluster::ORIGINAL_DST:
    if (cluster.lb_policy() != envoy::api::v2::Cluster::ORIGINAL_DST_LB) {
      throw EnvoyException(fmt::format(
          "cluster: cluster type 'original_dst' may only be used with LB type 'original_dst_lb'"));
    }
    if (cluster.has_lb_subset_config() && cluster.lb_subset_config().subset_selectors_size() != 0) {
      throw EnvoyException(fmt::format(
          "cluster: cluster type 'original_dst' may not be used with lb_subset_config"));
    }
    new_cluster.reset(new OriginalDstCluster(cluster, runtime, factory_context,
                                             std::move(stats_scope), added_via_api));
    break;
  case envoy::api::v2::Cluster::EDS:
    if (!cluster.has_eds_cluster_config()) {
      throw EnvoyException("cannot create an EDS cluster without an EDS config");
    }

    // We map SDS to EDS, since EDS provides backwards compatibility with SDS.
    new_cluster.reset(new EdsClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                         added_via_api));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  if (!cluster.health_checks().empty()) {
    // TODO(htuch): Need to support multiple health checks in v2.
    if (cluster.health_checks().size() != 1) {
      throw EnvoyException("Multiple health checks not supported");
    } else {
      new_cluster->setHealthChecker(HealthCheckerFactory::create(
          cluster.health_checks()[0], *new_cluster, runtime, random, dispatcher, log_manager));
    }
  }

  new_cluster->setOutlierDetector(Outlier::DetectorImplFactory::createForCluster(
      *new_cluster, cluster, dispatcher, runtime, outlier_event_logger));
  return std::move(new_cluster);
}

ClusterImplBase::ClusterImplBase(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : runtime_(runtime) {
  auto socket_factory = createTransportSocketFactory(cluster, factory_context);
  info_ = std::make_unique<ClusterInfoImpl>(cluster, factory_context.clusterManager().bindConfig(),
                                            runtime, std::move(socket_factory),
                                            std::move(stats_scope), added_via_api);
  // Create the default (empty) priority set before registering callbacks to
  // avoid getting an update the first time it is accessed.
  priority_set_.getOrCreateHostSet(0);
  priority_set_.addMemberUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) {
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          info_->stats().membership_change_.inc();
        }

        uint32_t healthy_hosts = 0;
        uint32_t hosts = 0;
        for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
          hosts += host_set->hosts().size();
          healthy_hosts += host_set->healthyHosts().size();
        }
        info_->stats().membership_total_.set(hosts);
        info_->stats().membership_healthy_.set(healthy_hosts);
      });
}

HostVectorConstSharedPtr ClusterImplBase::createHealthyHostList(const HostVector& hosts) {
  HostVectorSharedPtr healthy_list(new HostVector());
  for (const auto& host : hosts) {
    if (host->healthy()) {
      healthy_list->emplace_back(host);
    }
  }

  return healthy_list;
}

HostsPerLocalityConstSharedPtr
ClusterImplBase::createHealthyHostLists(const HostsPerLocality& hosts) {
  return hosts.filter([](const Host& host) { return host.healthy(); });
}

bool ClusterInfoImpl::maintenanceMode() const {
  return runtime_.snapshot().featureEnabled(maintenance_mode_runtime_key_, 0);
}

ResourceManager& ClusterInfoImpl::resourceManager(ResourcePriority priority) const {
  ASSERT(enumToInt(priority) < resource_managers_.managers_.size());
  return *resource_managers_.managers_[enumToInt(priority)];
}

void ClusterImplBase::initialize(std::function<void()> callback) {
  ASSERT(!initialization_started_);
  ASSERT(initialization_complete_callback_ == nullptr);
  initialization_complete_callback_ = callback;
  startPreInit();
}

void ClusterImplBase::onPreInitComplete() {
  // Protect against multiple calls.
  if (initialization_started_) {
    return;
  }
  initialization_started_ = true;

  if (health_checker_ && pending_initialize_health_checks_ == 0) {
    for (auto& host_set : prioritySet().hostSetsPerPriority()) {
      pending_initialize_health_checks_ += host_set->hosts().size();
    }

    // TODO(mattklein123): Remove this callback when done.
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, HealthTransition) -> void {
      if (pending_initialize_health_checks_ > 0 && --pending_initialize_health_checks_ == 0) {
        finishInitialization();
      }
    });
  }

  if (pending_initialize_health_checks_ == 0) {
    finishInitialization();
  }
}

void ClusterImplBase::finishInitialization() {
  ASSERT(initialization_complete_callback_ != nullptr);
  ASSERT(initialization_started_);

  // Snap a copy of the completion callback so that we can set it to nullptr to unblock
  // reloadHealthyHosts(). See that function for more info on why we do this.
  auto snapped_callback = initialization_complete_callback_;
  initialization_complete_callback_ = nullptr;

  if (health_checker_ != nullptr) {
    reloadHealthyHosts();
  }

  if (snapped_callback != nullptr) {
    snapped_callback();
  }
}

void ClusterImplBase::setHealthChecker(const HealthCheckerSharedPtr& health_checker) {
  ASSERT(!health_checker_);
  health_checker_ = health_checker;
  health_checker_->start();
  health_checker_->addHostCheckCompleteCb(
      [this](HostSharedPtr, HealthTransition changed_state) -> void {
        // If we get a health check completion that resulted in a state change, signal to
        // update the host sets on all threads.
        if (changed_state == HealthTransition::Changed) {
          reloadHealthyHosts();
        }
      });
}

void ClusterImplBase::setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector) {
  if (!outlier_detector) {
    return;
  }

  outlier_detector_ = outlier_detector;
  outlier_detector_->addChangedStateCb([this](HostSharedPtr) -> void { reloadHealthyHosts(); });
}

void ClusterImplBase::reloadHealthyHosts() {
  // Every time a host changes HC state we cause a full healthy host recalculation which
  // for expensive LBs (ring, subset, etc.) can be quite time consuming. During startup, this
  // can also block worker threads by doing this repeatedly. There is no reason to do this
  // as we will not start taking traffic until we are initialized. By blocking HC updates
  // while initializing we can avoid this.
  if (initialization_complete_callback_ != nullptr) {
    return;
  }

  for (auto& host_set : prioritySet().hostSetsPerPriority()) {
    // TODO(htuch): Can we skip these copies by exporting out const shared_ptr from HostSet?
    HostVectorConstSharedPtr hosts_copy(new HostVector(host_set->hosts()));
    HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().clone();
    host_set->updateHosts(
        hosts_copy, createHealthyHostList(host_set->hosts()), hosts_per_locality_copy,
        createHealthyHostLists(host_set->hostsPerLocality()), host_set->localityWeights(), {}, {});
  }
}

const Network::Address::InstanceConstSharedPtr
ClusterImplBase::resolveProtoAddress(const envoy::api::v2::core::Address& address) {
  try {
    return Network::Address::resolveProtoAddress(address);
  } catch (EnvoyException& e) {
    if (info_->type() == envoy::api::v2::Cluster::STATIC ||
        info_->type() == envoy::api::v2::Cluster::EDS) {
      throw EnvoyException(fmt::format("{}. Consider setting resolver_name or setting cluster type "
                                       "to 'STRICT_DNS' or 'LOGICAL_DNS'",
                                       e.what()));
    }
    throw e;
  }
}

ClusterInfoImpl::ResourceManagers::ResourceManagers(const envoy::api::v2::Cluster& config,
                                                    Runtime::Loader& runtime,
                                                    const std::string& cluster_name) {
  managers_[enumToInt(ResourcePriority::Default)] =
      load(config, runtime, cluster_name, envoy::api::v2::core::RoutingPriority::DEFAULT);
  managers_[enumToInt(ResourcePriority::High)] =
      load(config, runtime, cluster_name, envoy::api::v2::core::RoutingPriority::HIGH);
}

ResourceManagerImplPtr
ClusterInfoImpl::ResourceManagers::load(const envoy::api::v2::Cluster& config,
                                        Runtime::Loader& runtime, const std::string& cluster_name,
                                        const envoy::api::v2::core::RoutingPriority& priority) {
  uint64_t max_connections = 1024;
  uint64_t max_pending_requests = 1024;
  uint64_t max_requests = 1024;
  uint64_t max_retries = 3;

  std::string priority_name;
  switch (priority) {
  case envoy::api::v2::core::RoutingPriority::DEFAULT:
    priority_name = "default";
    break;
  case envoy::api::v2::core::RoutingPriority::HIGH:
    priority_name = "high";
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const std::string runtime_prefix =
      fmt::format("circuit_breakers.{}.{}.", cluster_name, priority_name);

  const auto& thresholds = config.circuit_breakers().thresholds();
  const auto it = std::find_if(
      thresholds.cbegin(), thresholds.cend(),
      [priority](const envoy::api::v2::cluster::CircuitBreakers::Thresholds& threshold) {
        return threshold.priority() == priority;
      });
  if (it != thresholds.cend()) {
    max_connections = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_connections, max_connections);
    max_pending_requests =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_pending_requests, max_pending_requests);
    max_requests = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_requests, max_requests);
    max_retries = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_retries, max_retries);
  }
  return ResourceManagerImplPtr{new ResourceManagerImpl(
      runtime, runtime_prefix, max_connections, max_pending_requests, max_requests, max_retries)};
}

PriorityStateManager::PriorityStateManager(ClusterImplBase& cluster,
                                           const LocalInfo::LocalInfo& local_info)
    : parent_(cluster), local_info_node_(local_info.node()) {}

void PriorityStateManager::initializePriorityFor(
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint) {
  const uint32_t priority = locality_lb_endpoint.priority();
  if (priority_state_.size() <= priority) {
    priority_state_.resize(priority + 1);
  }
  if (priority_state_[priority].first == nullptr) {
    priority_state_[priority].first.reset(new HostVector());
  }
  if (locality_lb_endpoint.has_locality() && locality_lb_endpoint.has_load_balancing_weight()) {
    priority_state_[priority].second[locality_lb_endpoint.locality()] =
        locality_lb_endpoint.load_balancing_weight().value();
  }
}

void PriorityStateManager::registerHostForPriority(
    const std::string& hostname, Network::Address::InstanceConstSharedPtr address,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
    const absl::optional<Upstream::Host::HealthFlag> health_checker_flag) {
  const HostSharedPtr host(new HostImpl(parent_.info(), hostname, address, lb_endpoint.metadata(),
                                        lb_endpoint.load_balancing_weight().value(),
                                        locality_lb_endpoint.locality(),
                                        lb_endpoint.endpoint().health_check_config()));
  registerHostForPriority(host, locality_lb_endpoint, lb_endpoint, health_checker_flag);
}

void PriorityStateManager::registerHostForPriority(
    const HostSharedPtr& host,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
    const absl::optional<Upstream::Host::HealthFlag> health_checker_flag) {
  const uint32_t priority = locality_lb_endpoint.priority();
  // Should be called after initializePriorityFor.
  ASSERT(priority_state_[priority].first);
  priority_state_[priority].first->emplace_back(host);
  if (health_checker_flag.has_value()) {
    const auto& health_status = lb_endpoint.health_status();
    if (health_status == envoy::api::v2::core::HealthStatus::UNHEALTHY ||
        health_status == envoy::api::v2::core::HealthStatus::DRAINING ||
        health_status == envoy::api::v2::core::HealthStatus::TIMEOUT) {
      priority_state_[priority].first->back()->healthFlagSet(health_checker_flag.value());
    }
  }
}

void PriorityStateManager::updateClusterPrioritySet(
    const uint32_t priority, HostVectorSharedPtr&& current_hosts,
    const absl::optional<HostVector>& hosts_added, const absl::optional<HostVector>& hosts_removed,
    const absl::optional<Upstream::Host::HealthFlag> health_checker_flag) {
  // If local locality is not defined then skip populating per locality hosts.
  const auto& local_locality = local_info_node_.locality();
  ENVOY_LOG(trace, "Local locality: {}", local_locality.DebugString());

  // For non-EDS, most likely the current hosts are from priority_state_[priority].first.
  HostVectorSharedPtr hosts(std::move(current_hosts));
  LocalityWeightsMap empty_locality_map;
  LocalityWeightsMap& locality_weights_map =
      priority_state_.size() > priority ? priority_state_[priority].second : empty_locality_map;
  ASSERT(priority_state_.size() > priority || locality_weights_map.empty());
  LocalityWeightsSharedPtr locality_weights;
  std::vector<HostVector> per_locality;

  // If we are configured for locality weighted LB we populate the locality weights.
  const bool locality_weighted_lb = parent_.info()->lbConfig().has_locality_weighted_lb_config();
  if (locality_weighted_lb) {
    locality_weights = std::make_shared<LocalityWeights>();
  }

  // We use std::map to guarantee a stable ordering for zone aware routing.
  std::map<envoy::api::v2::core::Locality, HostVector, LocalityLess> hosts_per_locality;

  for (const HostSharedPtr& host : *hosts) {
    // Take into consideration when a non-EDS cluster has active health checking, i.e. to mark all
    // the hosts unhealthy (host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC)) and then fire
    // update callbacks to start the health checking process.
    if (health_checker_flag.has_value()) {
      host->healthFlagSet(health_checker_flag.value());
    }
    hosts_per_locality[host->locality()].push_back(host);
  }

  // Do we have hosts for the local locality?
  const bool non_empty_local_locality =
      local_info_node_.has_locality() &&
      hosts_per_locality.find(local_locality) != hosts_per_locality.end();

  // As per HostsPerLocality::get(), the per_locality vector must have the local locality hosts
  // first if non_empty_local_locality.
  if (non_empty_local_locality) {
    per_locality.emplace_back(hosts_per_locality[local_locality]);
    if (locality_weighted_lb) {
      locality_weights->emplace_back(locality_weights_map[local_locality]);
    }
  }

  // After the local locality hosts (if any), we place the remaining locality host groups in
  // lexicographic order. This provides a stable ordering for zone aware routing.
  for (auto& entry : hosts_per_locality) {
    if (!non_empty_local_locality || !LocalityEqualTo()(local_locality, entry.first)) {
      per_locality.emplace_back(entry.second);
      if (locality_weighted_lb) {
        locality_weights->emplace_back(locality_weights_map[entry.first]);
      }
    }
  }

  auto per_locality_shared =
      std::make_shared<HostsPerLocalityImpl>(std::move(per_locality), non_empty_local_locality);

  auto& host_set =
      static_cast<PrioritySetImpl&>(parent_.prioritySet()).getOrCreateHostSet(priority);
  host_set.updateHosts(hosts, ClusterImplBase::createHealthyHostList(*hosts), per_locality_shared,
                       ClusterImplBase::createHealthyHostLists(*per_locality_shared),
                       std::move(locality_weights), hosts_added.value_or(*hosts),
                       hosts_removed.value_or<HostVector>({}));
}

StaticClusterImpl::StaticClusterImpl(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
      priority_state_manager_(new PriorityStateManager(*this, factory_context.localInfo())) {
  // TODO(dio): Use by-reference when cluster.hosts() is removed.
  const envoy::api::v2::ClusterLoadAssignment cluster_load_assignment(
      cluster.has_load_assignment() ? cluster.load_assignment()
                                    : Config::Utility::translateClusterHosts(cluster.hosts()));

  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    priority_state_manager_->initializePriorityFor(locality_lb_endpoint);
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      priority_state_manager_->registerHostForPriority(
          "", resolveProtoAddress(lb_endpoint.endpoint().address()), locality_lb_endpoint,
          lb_endpoint, absl::nullopt);
    }
  }
}

void StaticClusterImpl::startPreInit() {
  // At this point see if we have a health checker. If so, mark all the hosts unhealthy and
  // then fire update callbacks to start the health checking process.
  const auto& health_checker_flag =
      health_checker_ != nullptr
          ? absl::optional<Upstream::Host::HealthFlag>(Host::HealthFlag::FAILED_ACTIVE_HC)
          : absl::nullopt;

  auto& priority_state = priority_state_manager_->priorityState();
  for (size_t i = 0; i < priority_state.size(); ++i) {
    priority_state_manager_->updateClusterPrioritySet(
        i, std::move(priority_state[i].first), absl::nullopt, absl::nullopt, health_checker_flag);
  }
  priority_state_manager_.reset();

  onPreInitComplete();
}

bool BaseDynamicClusterImpl::updateDynamicHostList(
    const HostVector& new_hosts, HostVector& current_priority_hosts,
    HostVector& hosts_added_to_current_priority, HostVector& hosts_removed_from_current_priority,
    std::unordered_map<std::string, HostSharedPtr>& updated_hosts) {
  uint64_t max_host_weight = 1;

  // Did hosts change?
  //
  // Has the EDS health status changed the health of any endpoint? If so, we
  // rebuild the hosts vectors. We only do this if the health status of an
  // endpoint has materially changed (e.g. if previously failing active health
  // checks, we just note it's now failing EDS health status but don't rebuild).
  //
  // Likewise, if metadata for an endpoint changed we rebuild the hosts vectors.
  //
  // TODO(htuch): We can be smarter about this potentially, and not force a full
  // host set update on health status change. The way this would work is to
  // implement a HealthChecker subclass that provides thread local health
  // updates to the Cluster object. This will probably make sense to do in
  // conjunction with https://github.com/envoyproxy/envoy/issues/2874.
  bool hosts_changed = false;

  // Go through and see if the list we have is different from what we just got. If it is, we make a
  // new host list and raise a change notification. This uses an N^2 search given that this does not
  // happen very often and the list sizes should be small (see
  // https://github.com/envoyproxy/envoy/issues/2874). We also check for duplicates here. It's
  // possible for DNS to return the same address multiple times, and a bad SDS implementation could
  // do the same thing.

  // Keep track of hosts we see in new_hosts that we are able to match up with an existing host.
  std::unordered_set<std::string> existing_hosts_for_current_priority(
      current_priority_hosts.size());
  HostVector final_hosts;
  for (const HostSharedPtr& host : new_hosts) {
    if (updated_hosts.count(host->address()->asString())) {
      continue;
    }

    auto existing_host = all_hosts_.find(host->address()->asString());

    if (existing_host != all_hosts_.end()) {
      existing_hosts_for_current_priority.emplace(existing_host->first);
      // If we find a host matched based on address, we keep it. However we do change weight inline
      // so do that here.
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      if (existing_host->second->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH) !=
          host->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH)) {
        const bool previously_healthy = existing_host->second->healthy();
        if (host->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH)) {
          existing_host->second->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
          // If the host was previously healthy and we're now unhealthy, we need to
          // rebuild.
          hosts_changed |= previously_healthy;
        } else {
          existing_host->second->healthFlagClear(Host::HealthFlag::FAILED_EDS_HEALTH);
          // If the host was previously unhealthy and now healthy, we need to
          // rebuild.
          hosts_changed |= !previously_healthy && existing_host->second->healthy();
        }
      }

      // Did metadata change?
      const bool metadata_changed = !Protobuf::util::MessageDifferencer::Equivalent(
          *host->metadata(), *existing_host->second->metadata());
      if (metadata_changed) {
        // First, update the entire metadata for the endpoint.
        existing_host->second->metadata(*host->metadata());

        // Also, given that the canary attribute of an endpoint is derived from its metadata
        // (e.g.: from envoy.lb/canary), we do a blind update here since it's cheaper than testing
        // to see if it actually changed. We must update this besides just updating the metadata,
        // because it'll be used by the router filter to compute upstream stats.
        existing_host->second->canary(host->canary());

        // If metadata changed, we need to rebuild. See github issue #3810.
        hosts_changed = true;
      }

      existing_host->second->weight(host->weight());
      final_hosts.push_back(existing_host->second);
      updated_hosts[existing_host->second->address()->asString()] = existing_host->second;
    } else {
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      // If we are depending on a health checker, we initialize to unhealthy.
      if (health_checker_ != nullptr) {
        host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      }

      updated_hosts[host->address()->asString()] = host;
      final_hosts.push_back(host);
      hosts_added_to_current_priority.push_back(host);
    }
  }

  // Remove hosts from current_priority_hosts that were matched to an existing host in the previous
  // loop.
  current_priority_hosts.erase(std::remove_if(current_priority_hosts.begin(),
                                              current_priority_hosts.end(),
                                              [&existing_hosts_for_current_priority](auto host) {
                                                return existing_hosts_for_current_priority.count(
                                                    host->address()->asString());
                                              }),
                               current_priority_hosts.end());

  // The remaining hosts are hosts that are not referenced in the config update. We remove them from
  // the priority if any of the following is true:
  // - Active health checking is not enabled.
  // - The removed hosts are failing active health checking.
  // - We have explicitly configured the cluster to remove hosts regardless of active health status.
  const bool dont_remove_healthy_hosts =
      health_checker_ != nullptr && !info()->drainConnectionsOnHostRemoval();
  if (!current_priority_hosts.empty() && dont_remove_healthy_hosts) {
    for (auto i = current_priority_hosts.begin(); i != current_priority_hosts.end();) {
      if (!(*i)->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
        if ((*i)->weight() > max_host_weight) {
          max_host_weight = (*i)->weight();
        }

        final_hosts.push_back(*i);
        updated_hosts[(*i)->address()->asString()] = *i;
        i = current_priority_hosts.erase(i);
      } else {
        i++;
      }
    }
  }

  // At this point we've accounted for all the new hosts as well the hosts that previously
  // existed in this priority.

  // TODO(mattklein123): This stat is used by both the RR and LR load balancer to decide at
  // runtime whether to use either the weighted or unweighted mode. If we extend weights to
  // static clusters or DNS SRV clusters we need to make sure this gets set. Better, we should
  // avoid pivoting on this entirely and probably just force a host set refresh if any weights
  // change.
  info_->stats().max_host_weight_.set(max_host_weight);

  // Whatever remains in current_priority_hosts should be removed.
  if (!hosts_added_to_current_priority.empty() || !current_priority_hosts.empty()) {
    hosts_removed_from_current_priority = std::move(current_priority_hosts);
    hosts_changed = true;
  }

  // During the update we populated final_hosts with all the hosts that should remain
  // in the current priority, so move them back into current_priority_hosts.
  current_priority_hosts = std::move(final_hosts);
  // We return false here in the absence of EDS health status or metadata changes, because we
  // have no changes to host vector status (modulo weights). When we have EDS
  // health status or metadata changed, we return true, causing updateHosts() to fire in the
  // caller.
  return hosts_changed;
}

StrictDnsClusterImpl::StrictDnsClusterImpl(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                             added_via_api),
      local_info_(factory_context.localInfo()), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))) {
  switch (cluster.dns_lookup_family()) {
  case envoy::api::v2::Cluster::V6_ONLY:
    dns_lookup_family_ = Network::DnsLookupFamily::V6Only;
    break;
  case envoy::api::v2::Cluster::V4_ONLY:
    dns_lookup_family_ = Network::DnsLookupFamily::V4Only;
    break;
  case envoy::api::v2::Cluster::AUTO:
    dns_lookup_family_ = Network::DnsLookupFamily::Auto;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const envoy::api::v2::ClusterLoadAssignment load_assignment(
      cluster.has_load_assignment() ? cluster.load_assignment()
                                    : Config::Utility::translateClusterHosts(cluster.hosts()));
  const auto& locality_lb_endpoints = load_assignment.endpoints();
  for (const auto& locality_lb_endpoint : locality_lb_endpoints) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      const auto& host = lb_endpoint.endpoint().address();
      const std::string& url = fmt::format("tcp://{}:{}", host.socket_address().address(),
                                           host.socket_address().port_value());
      resolve_targets_.emplace_back(new ResolveTarget(*this, factory_context.dispatcher(), url,
                                                      locality_lb_endpoint, lb_endpoint));
    }
  }
}

void StrictDnsClusterImpl::startPreInit() {
  for (const ResolveTargetPtr& target : resolve_targets_) {
    target->startResolve();
  }
}

void StrictDnsClusterImpl::updateAllHosts(const HostVector& hosts_added,
                                          const HostVector& hosts_removed,
                                          uint32_t current_priority) {
  PriorityStateManager priority_state_manager(*this, local_info_);
  // At this point we know that we are different so make a new host list and notify.
  for (const ResolveTargetPtr& target : resolve_targets_) {
    priority_state_manager.initializePriorityFor(target->locality_lb_endpoint_);
    for (const HostSharedPtr& host : target->hosts_) {
      if (target->locality_lb_endpoint_.priority() == current_priority) {
        priority_state_manager.registerHostForPriority(host, target->locality_lb_endpoint_,
                                                       target->lb_endpoint_, absl::nullopt);
      }
    }
  }

  // TODO(dio): Add assertion in here.
  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt);
}

StrictDnsClusterImpl::ResolveTarget::ResolveTarget(
    StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher, const std::string& url,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
    : parent_(parent), dns_address_(Network::Utility::hostFromTcpUrl(url)),
      port_(Network::Utility::portFromTcpUrl(url)),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })),
      locality_lb_endpoint_(locality_lb_endpoint), lb_endpoint_(lb_endpoint) {}

StrictDnsClusterImpl::ResolveTarget::~ResolveTarget() {
  if (active_query_) {
    active_query_->cancel();
  }
}

void StrictDnsClusterImpl::ResolveTarget::startResolve() {
  ENVOY_LOG(debug, "starting async DNS resolution for {}", dns_address_);
  parent_.info_->stats().update_attempt_.inc();

  active_query_ = parent_.dns_resolver_->resolve(
      dns_address_, parent_.dns_lookup_family_,
      [this](std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
        active_query_ = nullptr;
        ENVOY_LOG(debug, "async DNS resolution complete for {}", dns_address_);
        parent_.info_->stats().update_success_.inc();

        std::unordered_map<std::string, HostSharedPtr> updated_hosts;
        HostVector new_hosts;
        for (const Network::Address::InstanceConstSharedPtr& address : address_list) {
          // TODO(mattklein123): Currently the DNS interface does not consider port. We need to
          // make a new address that has port in it. We need to both support IPv6 as well as
          // potentially move port handling into the DNS interface itself, which would work better
          // for SRV.
          ASSERT(address != nullptr);
          new_hosts.emplace_back(new HostImpl(
              parent_.info_, dns_address_, Network::Utility::getAddressWithPort(*address, port_),
              lb_endpoint_.metadata(), lb_endpoint_.load_balancing_weight().value(),
              locality_lb_endpoint_.locality(), lb_endpoint_.endpoint().health_check_config()));
        }

        HostVector hosts_added;
        HostVector hosts_removed;
        if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed,
                                          updated_hosts)) {
          ENVOY_LOG(debug, "DNS hosts have changed for {}", dns_address_);
          parent_.updateAllHosts(hosts_added, hosts_removed, locality_lb_endpoint_.priority());
        }

        parent_.updateHostMap(std::move(updated_hosts));

        // If there is an initialize callback, fire it now. Note that if the cluster refers to
        // multiple DNS names, this will return initialized after a single DNS resolution
        // completes. This is not perfect but is easier to code and unclear if the extra
        // complexity is needed so will start with this.
        parent_.onPreInitComplete();
        resolve_timer_->enableTimer(parent_.dns_refresh_rate_ms_);
      });
}

} // namespace Upstream
} // namespace Envoy
