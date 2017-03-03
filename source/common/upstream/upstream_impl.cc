#include "health_checker_impl.h"
#include "logical_dns_cluster.h"
#include "sds.h"
#include "upstream_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/ssl/context.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/ssl/connection_impl.h"
#include "common/ssl/context_config_impl.h"

namespace Upstream {

Outlier::DetectorHostSinkNullImpl HostDescriptionImpl::null_outlier_detector_;

Host::CreateConnectionData HostImpl::createConnection(Event::Dispatcher& dispatcher) const {
  return {createConnection(dispatcher, *cluster_, address_), shared_from_this()};
}

Network::ClientConnectionPtr HostImpl::createConnection(Event::Dispatcher& dispatcher,
                                                        const ClusterInfo& cluster,
                                                        Network::Address::InstancePtr address) {
  if (cluster.sslContext()) {
    return Network::ClientConnectionPtr{
        dispatcher.createSslClientConnection(*cluster.sslContext(), address)};
  } else {
    return Network::ClientConnectionPtr{dispatcher.createClientConnection(address)};
  }
}

void HostImpl::weight(uint32_t new_weight) { weight_ = std::max(1U, std::min(100U, new_weight)); }

void HostSetImpl::addMemberUpdateCb(MemberUpdateCb callback) const {
  callbacks_.emplace_back(callback);
}

ClusterStats ClusterInfoImpl::generateStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_TIMER(scope))};
}

void HostSetImpl::runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                                     const std::vector<HostPtr>& hosts_removed) {
  for (MemberUpdateCb& callback : callbacks_) {
    callback(hosts_added, hosts_removed);
  }
}

ClusterInfoImpl::ClusterInfoImpl(const Json::Object& config, Runtime::Loader& runtime,
                                 Stats::Store& stats, Ssl::ContextManager& ssl_context_manager)
    : runtime_(runtime), name_(config.getString("name")),
      max_requests_per_connection_(config.getInteger("max_requests_per_connection", 0)),
      connect_timeout_(std::chrono::milliseconds(config.getInteger("connect_timeout_ms"))),
      stats_scope_(stats.createScope(fmt::format("cluster.{}.", name_))),
      stats_(generateStats(*stats_scope_)), features_(parseFeatures(config)),
      http_codec_options_(Http::Utility::parseCodecOptions(config)),
      resource_managers_(config, runtime, name_),
      maintenance_mode_runtime_key_(fmt::format("upstream.maintenance_mode.{}", name_)) {

  ssl_ctx_ = nullptr;
  if (config.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(*config.getObject("ssl_context"));
    ssl_ctx_ = ssl_context_manager.createSslClientContext(*stats_scope_, context_config);
  }

  std::string string_lb_type = config.getString("lb_type");
  if (string_lb_type == "round_robin") {
    lb_type_ = LoadBalancerType::RoundRobin;
  } else if (string_lb_type == "least_request") {
    lb_type_ = LoadBalancerType::LeastRequest;
  } else if (string_lb_type == "random") {
    lb_type_ = LoadBalancerType::Random;
  } else if (string_lb_type == "ring_hash") {
    lb_type_ = LoadBalancerType::RingHash;
  } else {
    throw EnvoyException(fmt::format("cluster: unknown LB type '{}'", string_lb_type));
  }
}

const ConstHostListsPtr ClusterImplBase::empty_host_lists_{new std::vector<std::vector<HostPtr>>()};

ClusterPtr ClusterImplBase::create(const Json::Object& cluster, ClusterManager& cm,
                                   Stats::Store& stats, ThreadLocal::Instance& tls,
                                   Network::DnsResolver& dns_resolver,
                                   Ssl::ContextManager& ssl_context_manager,
                                   Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                   Event::Dispatcher& dispatcher,
                                   const Optional<SdsConfig>& sds_config,
                                   const LocalInfo::LocalInfo& local_info,
                                   Outlier::EventLoggerPtr outlier_event_logger) {

  cluster.validateSchema(Json::Schema::CLUSTER_SCHEMA);

  std::unique_ptr<ClusterImplBase> new_cluster;
  std::string string_type = cluster.getString("type");
  if (string_type == "static") {
    new_cluster.reset(new StaticClusterImpl(cluster, runtime, stats, ssl_context_manager));
  } else if (string_type == "strict_dns") {
    new_cluster.reset(new StrictDnsClusterImpl(cluster, runtime, stats, ssl_context_manager,
                                               dns_resolver, dispatcher));
  } else if (string_type == "logical_dns") {
    new_cluster.reset(new LogicalDnsCluster(cluster, runtime, stats, ssl_context_manager,
                                            dns_resolver, tls, dispatcher));
  } else if (string_type == "sds") {
    if (!sds_config.valid()) {
      throw EnvoyException("cannot create an sds cluster without an sds config");
    }

    new_cluster.reset(new SdsClusterImpl(cluster, runtime, stats, ssl_context_manager,
                                         sds_config.value(), local_info, cm, dispatcher, random));
  } else {
    throw EnvoyException(fmt::format("cluster: unknown cluster type '{}'", string_type));
  }

  if (cluster.hasObject("health_check")) {
    Json::ObjectPtr health_check_config = cluster.getObject("health_check");
    std::string hc_type = health_check_config->getString("type");
    if (hc_type == "http") {
      new_cluster->setHealthChecker(HealthCheckerPtr{new ProdHttpHealthCheckerImpl(
          *new_cluster, *health_check_config, dispatcher, runtime, random)});
    } else if (hc_type == "tcp") {
      new_cluster->setHealthChecker(HealthCheckerPtr{new TcpHealthCheckerImpl(
          *new_cluster, *health_check_config, dispatcher, runtime, random)});
    } else {
      throw EnvoyException(fmt::format("cluster: unknown health check type '{}'", hc_type));
    }
  }

  new_cluster->setOutlierDetector(Outlier::DetectorImplFactory::createForCluster(
      *new_cluster, cluster, dispatcher, runtime, outlier_event_logger));
  return std::move(new_cluster);
}

ClusterImplBase::ClusterImplBase(const Json::Object& config, Runtime::Loader& runtime,
                                 Stats::Store& stats, Ssl::ContextManager& ssl_context_manager)
    : runtime_(runtime), info_(new ClusterInfoImpl(config, runtime, stats, ssl_context_manager)) {}

ConstHostVectorPtr ClusterImplBase::createHealthyHostList(const std::vector<HostPtr>& hosts) {
  HostVectorPtr healthy_list(new std::vector<HostPtr>());
  for (const auto& host : hosts) {
    if (host->healthy()) {
      healthy_list->emplace_back(host);
    }
  }

  return healthy_list;
}

ConstHostListsPtr
ClusterImplBase::createHealthyHostLists(const std::vector<std::vector<HostPtr>>& hosts) {
  HostListsPtr healthy_list(new std::vector<std::vector<HostPtr>>());

  for (const auto& hosts_zone : hosts) {
    std::vector<HostPtr> current_zone_hosts;
    for (const auto& host : hosts_zone) {
      if (host->healthy()) {
        current_zone_hosts.emplace_back(host);
      }
    }
    healthy_list->push_back(std::move(current_zone_hosts));
  }

  return healthy_list;
}

bool ClusterInfoImpl::maintenanceMode() const {
  return runtime_.snapshot().featureEnabled(maintenance_mode_runtime_key_, 0);
}

uint64_t ClusterInfoImpl::parseFeatures(const Json::Object& config) {
  uint64_t features = 0;
  for (const std::string& feature : StringUtil::split(config.getString("features", ""), ',')) {
    if (feature == "http2") {
      features |= Features::HTTP2;
    } else {
      throw EnvoyException(fmt::format("unknown cluster feature '{}'", feature));
    }
  }

  return features;
}

ResourceManager& ClusterInfoImpl::resourceManager(ResourcePriority priority) const {
  ASSERT(enumToInt(priority) < resource_managers_.managers_.size());
  return *resource_managers_.managers_[enumToInt(priority)];
}

void ClusterImplBase::runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                                         const std::vector<HostPtr>& hosts_removed) {
  if (!hosts_added.empty() || !hosts_removed.empty()) {
    info_->stats().membership_change_.inc();
  }

  info_->stats().membership_healthy_.set(healthyHosts().size());
  info_->stats().membership_total_.set(hosts().size());
  HostSetImpl::runUpdateCallbacks(hosts_added, hosts_removed);
}

void ClusterImplBase::setHealthChecker(HealthCheckerPtr&& health_checker) {
  ASSERT(!health_checker_);
  health_checker_ = std::move(health_checker);
  health_checker_->start();
  health_checker_->addHostCheckCompleteCb([this](HostPtr, bool changed_state) -> void {
    // If we get a health check completion that resulted in a state change, signal to
    // update the host sets on all threads.
    if (changed_state) {
      reloadHealthyHosts();
    }
  });
}

void ClusterImplBase::setOutlierDetector(Outlier::DetectorPtr outlier_detector) {
  if (!outlier_detector) {
    return;
  }

  outlier_detector_ = std::move(outlier_detector);
  outlier_detector_->addChangedStateCb([this](HostPtr) -> void { reloadHealthyHosts(); });
}

void ClusterImplBase::reloadHealthyHosts() {
  ConstHostVectorPtr hosts_copy(new std::vector<HostPtr>(hosts()));
  ConstHostListsPtr hosts_per_zone_copy(new std::vector<std::vector<HostPtr>>(hostsPerZone()));
  updateHosts(hosts_copy, createHealthyHostList(hosts()), hosts_per_zone_copy,
              createHealthyHostLists(hostsPerZone()), {}, {});
}

ClusterInfoImpl::ResourceManagers::ResourceManagers(const Json::Object& config,
                                                    Runtime::Loader& runtime,
                                                    const std::string& cluster_name) {
  managers_[enumToInt(ResourcePriority::Default)] = load(config, runtime, cluster_name, "default");
  managers_[enumToInt(ResourcePriority::High)] = load(config, runtime, cluster_name, "high");
}

ResourceManagerImplPtr ClusterInfoImpl::ResourceManagers::load(const Json::Object& config,
                                                               Runtime::Loader& runtime,
                                                               const std::string& cluster_name,
                                                               const std::string& priority) {
  uint64_t max_connections = 1024;
  uint64_t max_pending_requests = 1024;
  uint64_t max_requests = 1024;
  uint64_t max_retries = 3;
  std::string runtime_prefix = fmt::format("circuit_breakers.{}.{}.", cluster_name, priority);

  Json::ObjectPtr settings = config.getObject("circuit_breakers", true)->getObject(priority, true);
  max_connections = settings->getInteger("max_connections", max_connections);
  max_pending_requests = settings->getInteger("max_pending_requests", max_pending_requests);
  max_requests = settings->getInteger("max_requests", max_requests);
  max_retries = settings->getInteger("max_retries", max_retries);

  return ResourceManagerImplPtr{new ResourceManagerImpl(
      runtime, runtime_prefix, max_connections, max_pending_requests, max_requests, max_retries)};
}

StaticClusterImpl::StaticClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                                     Stats::Store& stats, Ssl::ContextManager& ssl_context_manager)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager) {
  std::vector<Json::ObjectPtr> hosts_json = config.getObjectArray("hosts");
  HostVectorPtr new_hosts(new std::vector<HostPtr>());
  for (Json::ObjectPtr& host : hosts_json) {
    new_hosts->emplace_back(HostPtr{new HostImpl(
        info_, "", Network::Utility::resolveUrl(host->getString("url")), false, 1, "")});
  }

  updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_lists_, empty_host_lists_,
              {}, {});
}

bool BaseDynamicClusterImpl::updateDynamicHostList(const std::vector<HostPtr>& new_hosts,
                                                   std::vector<HostPtr>& current_hosts,
                                                   std::vector<HostPtr>& hosts_added,
                                                   std::vector<HostPtr>& hosts_removed,
                                                   bool depend_on_hc) {
  uint64_t max_host_weight = 1;

  // Go through and see if the list we have is different from what we just got. If it is, we
  // make a new host list and raise a change notification. This uses an N^2 search given that
  // this does not happen very often and the list sizes should be small. We also check for
  // duplicates here. It's possible for DNS to return the same address multiple times, and a bad
  // SDS implementation could do the same thing.
  std::unordered_set<std::string> host_addresses;
  std::vector<HostPtr> final_hosts;
  for (HostPtr host : new_hosts) {
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
    return false;
  }
}

StrictDnsClusterImpl::StrictDnsClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                                           Stats::Store& stats,
                                           Ssl::ContextManager& ssl_context_manager,
                                           Network::DnsResolver& dns_resolver,
                                           Event::Dispatcher& dispatcher)
    : BaseDynamicClusterImpl(config, runtime, stats, ssl_context_manager),
      dns_resolver_(dns_resolver), dns_refresh_rate_ms_(std::chrono::milliseconds(
                                       config.getInteger("dns_refresh_rate_ms", 5000))) {
  for (Json::ObjectPtr& host : config.getObjectArray("hosts")) {
    resolve_targets_.emplace_back(new ResolveTarget(*this, dispatcher, host->getString("url")));
  }
  // We have to first construct resolve_targets_ before invoking startResolve(),
  // since startResolve() might resolve immediately and relies on
  // resolve_targets_ indirectly for performing host updates on resolution.
  for (const ResolveTargetPtr& target : resolve_targets_) {
    target->startResolve();
  }
}

void StrictDnsClusterImpl::updateAllHosts(const std::vector<HostPtr>& hosts_added,
                                          const std::vector<HostPtr>& hosts_removed) {
  // At this point we know that we are different so make a new host list and notify.
  HostVectorPtr new_hosts(new std::vector<HostPtr>());
  for (const ResolveTargetPtr& target : resolve_targets_) {
    for (const HostPtr& host : target->hosts_) {
      new_hosts->emplace_back(host);
    }
  }

  updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_lists_, empty_host_lists_,
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
  log_debug("starting async DNS resolution for {}", dns_address_);
  parent_.info_->stats().update_attempt_.inc();

  active_query_ = parent_.dns_resolver_.resolve(
      dns_address_, [this](std::list<Network::Address::InstancePtr>&& address_list) -> void {
        active_query_ = nullptr;
        log_debug("async DNS resolution complete for {}", dns_address_);
        parent_.info_->stats().update_success_.inc();

        std::vector<HostPtr> new_hosts;
        for (Network::Address::InstancePtr address : address_list) {
          // TODO(mklein123): Currently the DNS interface does not consider port. We need to make a
          // new address that has port in it. We need to both support IPv6 as well as potentially
          // move port handling into the DNS interface itself, which would work better for SRV.
          new_hosts.emplace_back(
              new HostImpl(parent_.info_, dns_address_,
                           Network::Address::InstancePtr{new Network::Address::Ipv4Instance(
                               address->ip()->addressAsString(), port_)},
                           false, 1, ""));
        }

        std::vector<HostPtr> hosts_added;
        std::vector<HostPtr> hosts_removed;
        if (parent_.updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, false)) {
          log_debug("DNS hosts have changed for {}", dns_address_);
          parent_.updateAllHosts(hosts_added, hosts_removed);
        }

        // If there is an initialize callback, fire it now. Note that if the cluster refers to
        // multiple DNS names, this will return initialized after a single DNS resolution completes.
        // This is not perfect but is easier to code and unclear if the extra complexity is needed
        // so will start with this.
        if (parent_.initialize_callback_) {
          parent_.initialize_callback_();
          parent_.initialize_callback_ = nullptr;
        }
        parent_.initialized_ = true;

        resolve_timer_->enableTimer(parent_.dns_refresh_rate_ms_);
      });
}

} // Upstream
