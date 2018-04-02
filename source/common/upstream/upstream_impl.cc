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
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
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
#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/eds.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/logical_dns_cluster.h"
#include "common/upstream/original_dst_cluster.h"

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

uint64_t parseFeatures(const envoy::api::v2::Cluster& config,
                       const envoy::api::v2::core::BindConfig bind_config) {
  uint64_t features = 0;
  if (config.has_http2_protocol_options()) {
    features |= ClusterInfoImpl::Features::HTTP2;
  }
  if (config.protocol_selection() == envoy::api::v2::Cluster::USE_DOWNSTREAM_PROTOCOL) {
    features |= ClusterInfoImpl::Features::USE_DOWNSTREAM_PROTOCOL;
  }
  // Cluster IP_FREEBIND settings, when set, will override the cluster manager wide settings.
  if ((bind_config.freebind().value() && !config.upstream_bind_config().has_freebind()) ||
      config.upstream_bind_config().freebind().value()) {
    features |= ClusterInfoImpl::Features::FREEBIND;
  }
  return features;
}

// Socket::Option implementation for API-defined upstream options. This same
// object can be extended to handle additional upstream socket options.
class UpstreamSocketOption : public Network::SocketOptionImpl {
public:
  UpstreamSocketOption(const ClusterInfo& cluster_info)
      : Network::SocketOptionImpl({}, cluster_info.features() & ClusterInfo::Features::FREEBIND
                                          ? true
                                          : absl::optional<bool>{}) {}
};

} // namespace

Host::CreateConnectionData
HostImpl::createConnection(Event::Dispatcher& dispatcher,
                           const Network::ConnectionSocket::OptionsSharedPtr& options) const {
  return {createConnection(dispatcher, *cluster_, address_, options), shared_from_this()};
}

Network::ClientConnectionPtr
HostImpl::createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                           Network::Address::InstanceConstSharedPtr address,
                           const Network::ConnectionSocket::OptionsSharedPtr& options) {
  Network::ConnectionSocket::OptionsSharedPtr cluster_options;
  if (cluster.features() & ClusterInfo::Features::FREEBIND) {
    cluster_options = std::make_shared<Network::ConnectionSocket::Options>();
    if (options) {
      *cluster_options = *options;
    }
    cluster_options->emplace_back(new UpstreamSocketOption(cluster));
  } else {
    cluster_options = options;
  }
  Network::ClientConnectionPtr connection = dispatcher.createClientConnection(
      address, cluster.sourceAddress(), cluster.transportSocketFactory().createTransportSocket(),
      cluster_options);
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
  // Rebuild the locality scheduler.
  // TODO(htuch): if the underlying locality index ->
  // envoy::api::v2::core::Locality hasn't changed in hosts_/healthy_hosts_, we
  // could just update locality_weight_ without rebuilding. Similar to how host
  // level WRR works, we would age out the existing entries via picks and lazily
  // apply the new weights.
  if (hosts_per_locality_ != nullptr && locality_weights_ != nullptr &&
      !locality_weights_->empty()) {
    locality_scheduler_ = std::make_unique<EdfScheduler<LocalityEntry>>();
    locality_entries_.clear();
    for (uint32_t i = 0; i < hosts_per_locality_->get().size(); ++i) {
      const double effective_weight = effectiveLocalityWeight(i);
      if (effective_weight > 0) {
        locality_entries_.emplace_back(std::make_shared<LocalityEntry>(i, effective_weight));
        locality_scheduler_->add(effective_weight, locality_entries_.back());
      }
    }
  } else {
    locality_scheduler_ = nullptr;
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
                                 Runtime::Loader& runtime, Stats::Store& stats,
                                 Ssl::ContextManager& ssl_context_manager, bool added_via_api)
    : runtime_(runtime), name_(config.name()), type_(config.type()),
      max_requests_per_connection_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_requests_per_connection, 0)),
      connect_timeout_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(config, connect_timeout))),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      stats_scope_(stats.createScope(fmt::format(
          "cluster.{}.",
          config.alt_stat_name().empty() ? name_ : std::string(config.alt_stat_name())))),
      stats_(generateStats(*stats_scope_)),
      load_report_stats_(generateLoadReportStats(load_report_stats_store_)),
      features_(parseFeatures(config, bind_config)),
      http2_settings_(Http::Utility::parseHttp2Settings(config.http2_protocol_options())),
      resource_managers_(config, runtime, name_),
      maintenance_mode_runtime_key_(fmt::format("upstream.maintenance_mode.{}", name_)),
      source_address_(getSourceAddress(config, bind_config)),
      lb_ring_hash_config_(envoy::api::v2::Cluster::RingHashLbConfig(config.ring_hash_lb_config())),
      ssl_context_manager_(ssl_context_manager), added_via_api_(added_via_api),
      lb_subset_(LoadBalancerSubsetInfoImpl(config.lb_subset_config())),
      metadata_(config.metadata()), common_lb_config_(config.common_lb_config()) {

  // If the cluster doesn't have a transport socket configured, override with the default transport
  // socket implementation based on the tls_context. We copy by value first then override if
  // necessary.
  auto transport_socket = config.transport_socket();
  if (!config.has_transport_socket()) {
    if (config.has_tls_context()) {
      transport_socket.set_name(Config::TransportSocketNames::get().SSL);
      MessageUtil::jsonConvert(config.tls_context(), *transport_socket.mutable_config());
    } else {
      transport_socket.set_name(Config::TransportSocketNames::get().RAW_BUFFER);
    }
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(transport_socket.name());
  ProtobufTypes::MessagePtr message =
      Config::Utility::translateToFactoryConfig(transport_socket, config_factory);
  transport_socket_factory_ = config_factory.createTransportSocketFactory(*message, *this);

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
    NOT_REACHED;
  }

  if (config.protocol_selection() == envoy::api::v2::Cluster::USE_CONFIGURED_PROTOCOL) {
    // Make sure multiple protocol configurations are not present
    if (config.has_http_protocol_options() && config.has_http2_protocol_options()) {
      throw EnvoyException(fmt::format("cluster: Both HTTP1 and HTTP2 options may only be "
                                       "configured with non-default 'protocol_selection' values"));
    }
  }

  if (config.common_http_protocol_options().has_idle_timeout()) {
    idle_timeout_ = std::chrono::milliseconds(Protobuf::util::TimeUtil::DurationToMilliseconds(
        config.common_http_protocol_options().idle_timeout()));
  }
}

ClusterSharedPtr ClusterImplBase::create(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                         Stats::Store& stats, ThreadLocal::Instance& tls,
                                         Network::DnsResolverSharedPtr dns_resolver,
                                         Ssl::ContextManager& ssl_context_manager,
                                         Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                         Event::Dispatcher& dispatcher,
                                         const LocalInfo::LocalInfo& local_info,
                                         Outlier::EventLoggerSharedPtr outlier_event_logger,
                                         bool added_via_api) {
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

  switch (cluster.type()) {
  case envoy::api::v2::Cluster::STATIC:
    new_cluster.reset(
        new StaticClusterImpl(cluster, runtime, stats, ssl_context_manager, cm, added_via_api));
    break;
  case envoy::api::v2::Cluster::STRICT_DNS:
    new_cluster.reset(new StrictDnsClusterImpl(cluster, runtime, stats, ssl_context_manager,
                                               selected_dns_resolver, cm, dispatcher,
                                               added_via_api));
    break;
  case envoy::api::v2::Cluster::LOGICAL_DNS:
    new_cluster.reset(new LogicalDnsCluster(cluster, runtime, stats, ssl_context_manager,
                                            selected_dns_resolver, tls, cm, dispatcher,
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
    new_cluster.reset(new OriginalDstCluster(cluster, runtime, stats, ssl_context_manager, cm,
                                             dispatcher, added_via_api));
    break;
  case envoy::api::v2::Cluster::EDS:
    if (!cluster.has_eds_cluster_config()) {
      throw EnvoyException("cannot create an EDS cluster without an EDS config");
    }

    // We map SDS to EDS, since EDS provides backwards compatibility with SDS.
    new_cluster.reset(new EdsClusterImpl(cluster, runtime, stats, ssl_context_manager, local_info,
                                         cm, dispatcher, random, added_via_api));
    break;
  default:
    NOT_REACHED;
  }

  if (!cluster.health_checks().empty()) {
    // TODO(htuch): Need to support multiple health checks in v2.
    ASSERT(cluster.health_checks().size() == 1);
    new_cluster->setHealthChecker(HealthCheckerFactory::create(
        cluster.health_checks()[0], *new_cluster, runtime, random, dispatcher));
  }

  new_cluster->setOutlierDetector(Outlier::DetectorImplFactory::createForCluster(
      *new_cluster, cluster, dispatcher, runtime, outlier_event_logger));
  return std::move(new_cluster);
}

ClusterImplBase::ClusterImplBase(const envoy::api::v2::Cluster& cluster,
                                 const envoy::api::v2::core::BindConfig& bind_config,
                                 Runtime::Loader& runtime, Stats::Store& stats,
                                 Ssl::ContextManager& ssl_context_manager, bool added_via_api)
    : runtime_(runtime), info_(new ClusterInfoImpl(cluster, bind_config, runtime, stats,
                                                   ssl_context_manager, added_via_api)) {
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
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, bool) -> void {
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
  health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, bool changed_state) -> void {
    // If we get a health check completion that resulted in a state change, signal to
    // update the host sets on all threads.
    if (changed_state) {
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
    NOT_REACHED;
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

StaticClusterImpl::StaticClusterImpl(const envoy::api::v2::Cluster& cluster,
                                     Runtime::Loader& runtime, Stats::Store& stats,
                                     Ssl::ContextManager& ssl_context_manager, ClusterManager& cm,
                                     bool added_via_api)
    : ClusterImplBase(cluster, cm.bindConfig(), runtime, stats, ssl_context_manager, added_via_api),
      initial_hosts_(new HostVector()) {

  for (const auto& host : cluster.hosts()) {
    initial_hosts_->emplace_back(HostSharedPtr{new HostImpl(
        info_, "", resolveProtoAddress(host), envoy::api::v2::core::Metadata::default_instance(), 1,
        envoy::api::v2::core::Locality().default_instance())});
  }
}

void StaticClusterImpl::startPreInit() {
  // At this point see if we have a health checker. If so, mark all the hosts unhealthy and then
  // fire update callbacks to start the health checking process.
  if (health_checker_) {
    for (const auto& host : *initial_hosts_) {
      host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
  }

  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  first_host_set.updateHosts(initial_hosts_, createHealthyHostList(*initial_hosts_),
                             HostsPerLocalityImpl::empty(), HostsPerLocalityImpl::empty(), {},
                             *initial_hosts_, {});
  initial_hosts_ = nullptr;

  onPreInitComplete();
}

bool BaseDynamicClusterImpl::updateDynamicHostList(const HostVector& new_hosts,
                                                   HostVector& current_hosts,
                                                   HostVector& hosts_added,
                                                   HostVector& hosts_removed, bool depend_on_hc) {
  uint64_t max_host_weight = 1;
  // Has the EDS health status changed the health of any endpoint? If so, we
  // rebuild the hosts vectors. We only do this if the health status of an
  // endpoint has materially changed (e.g. if previously failing active health
  // checks, we just note it's now failing EDS health status but don't rebuild).
  // TODO(htuch): We can be smarter about this potentially, and not force a full
  // host set update on health status change. The way this would work is to
  // implement a HealthChecker subclass that provides thread local health
  // updates to the Cluster objeect. This will probably make sense to do in
  // conjunction with https://github.com/envoyproxy/envoy/issues/2874.
  bool health_changed = false;

  // Go through and see if the list we have is different from what we just got. If it is, we make a
  // new host list and raise a change notification. This uses an N^2 search given that this does not
  // happen very often and the list sizes should be small (see
  // https://github.com/envoyproxy/envoy/issues/2874). We also check for duplicates here. It's
  // possible for DNS to return the same address multiple times, and a bad SDS implementation could
  // do the same thing.
  std::unordered_set<std::string> host_addresses;
  HostVector final_hosts;
  for (const HostSharedPtr& host : new_hosts) {
    if (host_addresses.count(host->address()->asString())) {
      continue;
    }
    host_addresses.emplace(host->address()->asString());

    bool found = false;
    for (auto i = current_hosts.begin(); i != current_hosts.end();) {
      // If we find a host matched based on address, we keep it. However we do change weight inline
      // so do that here.
      if (*(*i)->address() == *host->address()) {
        if (host->weight() > max_host_weight) {
          max_host_weight = host->weight();
        }

        if ((*i)->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH) !=
            host->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH)) {
          const bool previously_healthy = (*i)->healthy();
          if (host->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH)) {
            (*i)->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
            // If the host was previously healthy and we're now unhealthy, we need to
            // rebuild.
            health_changed |= previously_healthy;
          } else {
            (*i)->healthFlagClear(Host::HealthFlag::FAILED_EDS_HEALTH);
            // If the host was previously unhealthy and now healthy, we need to
            // rebuild.
            health_changed |= !previously_healthy && (*i)->healthy();
          }
        }

        (*i)->weight(host->weight());
        final_hosts.push_back(*i);
        i = current_hosts.erase(i);
        found = true;
      } else {
        i++;
      }
    }

    if (!found) {
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      final_hosts.push_back(host);
      hosts_added.push_back(host);

      // If we are depending on a health checker, we initialize to unhealthy.
      if (depend_on_hc) {
        hosts_added.back()->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      }
    }
  }

  // If there are removed hosts, check to see if we should only delete if unhealthy.
  if (!current_hosts.empty() && depend_on_hc) {
    for (auto i = current_hosts.begin(); i != current_hosts.end();) {
      if (!(*i)->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
        if ((*i)->weight() > max_host_weight) {
          max_host_weight = (*i)->weight();
        }

        final_hosts.push_back(*i);
        i = current_hosts.erase(i);
      } else {
        i++;
      }
    }
  }

  info_->stats().max_host_weight_.set(max_host_weight);

  if (!hosts_added.empty() || !current_hosts.empty()) {
    hosts_removed = std::move(current_hosts);
    current_hosts = std::move(final_hosts);
    return true;
  } else {
    // During the search we moved all of the hosts from hosts_ into final_hosts so just
    // move them back.
    current_hosts = std::move(final_hosts);
    // We return false here in the absence of EDS health status, because we
    // have no changes to host vector status (modulo weights). When we have EDS
    // health status, we return true, causing updateHosts() to fire in the
    // caller.
    return health_changed;
  }
}

StrictDnsClusterImpl::StrictDnsClusterImpl(const envoy::api::v2::Cluster& cluster,
                                           Runtime::Loader& runtime, Stats::Store& stats,
                                           Ssl::ContextManager& ssl_context_manager,
                                           Network::DnsResolverSharedPtr dns_resolver,
                                           ClusterManager& cm, Event::Dispatcher& dispatcher,
                                           bool added_via_api)
    : BaseDynamicClusterImpl(cluster, cm.bindConfig(), runtime, stats, ssl_context_manager,
                             added_via_api),
      dns_resolver_(dns_resolver),
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
    NOT_REACHED;
  }

  for (const auto& host : cluster.hosts()) {
    resolve_targets_.emplace_back(
        new ResolveTarget(*this, dispatcher,
                          fmt::format("tcp://{}:{}", host.socket_address().address(),
                                      host.socket_address().port_value())));
  }
}

void StrictDnsClusterImpl::startPreInit() {
  for (const ResolveTargetPtr& target : resolve_targets_) {
    target->startResolve();
  }
}

void StrictDnsClusterImpl::updateAllHosts(const HostVector& hosts_added,
                                          const HostVector& hosts_removed) {
  // At this point we know that we are different so make a new host list and notify.
  HostVectorSharedPtr new_hosts(new HostVector());
  for (const ResolveTargetPtr& target : resolve_targets_) {
    for (const HostSharedPtr& host : target->hosts_) {
      new_hosts->emplace_back(host);
    }
  }

  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  first_host_set.updateHosts(new_hosts, createHealthyHostList(*new_hosts),
                             HostsPerLocalityImpl::empty(), HostsPerLocalityImpl::empty(), {},
                             hosts_added, hosts_removed);
}

StrictDnsClusterImpl::ResolveTarget::ResolveTarget(StrictDnsClusterImpl& parent,
                                                   Event::Dispatcher& dispatcher,
                                                   const std::string& url)
    : parent_(parent), dns_address_(Network::Utility::hostFromTcpUrl(url)),
      port_(Network::Utility::portFromTcpUrl(url)),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })) {}

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

        HostVector new_hosts;
        for (const Network::Address::InstanceConstSharedPtr& address : address_list) {
          // TODO(mattklein123): Currently the DNS interface does not consider port. We need to make
          // a new address that has port in it. We need to both support IPv6 as well as potentially
          // move port handling into the DNS interface itself, which would work better for SRV.
          ASSERT(address != nullptr);
          new_hosts.emplace_back(new HostImpl(parent_.info_, dns_address_,
                                              Network::Utility::getAddressWithPort(*address, port_),
                                              envoy::api::v2::core::Metadata::default_instance(), 1,
                                              envoy::api::v2::core::Locality().default_instance()));
        }

        HostVector hosts_added;
        HostVector hosts_removed;
        if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, false)) {
          ENVOY_LOG(debug, "DNS hosts have changed for {}", dns_address_);
          parent_.updateAllHosts(hosts_added, hosts_removed);
        }

        // If there is an initialize callback, fire it now. Note that if the cluster refers to
        // multiple DNS names, this will return initialized after a single DNS resolution completes.
        // This is not perfect but is easier to code and unclear if the extra complexity is needed
        // so will start with this.
        parent_.onPreInitComplete();
        resolve_timer_->enableTimer(parent_.dns_refresh_rate_ms_);
      });
}

} // namespace Upstream
} // namespace Envoy
