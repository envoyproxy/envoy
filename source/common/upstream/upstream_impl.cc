#include "upstream_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/ssl/context.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/ssl/connection_impl.h"
#include "common/ssl/context_manager_impl.h"

namespace Upstream {

OutlierDetectorHostSinkNullImpl HostDescriptionImpl::null_outlier_detector_;

Host::CreateConnectionData HostImpl::createConnection(Event::Dispatcher& dispatcher) const {
  return {createConnection(dispatcher, cluster_, url_), shared_from_this()};
}

Network::ClientConnectionPtr HostImpl::createConnection(Event::Dispatcher& dispatcher,
                                                        const Cluster& cluster,
                                                        const std::string& url) {
  if (cluster.sslContext()) {
    return Network::ClientConnectionPtr{
        dispatcher.createSslClientConnection(*cluster.sslContext(), url)};
  } else {
    return Network::ClientConnectionPtr{dispatcher.createClientConnection(url)};
  }
}

void HostImpl::weight(uint32_t new_weight) { weight_ = std::max(1U, std::min(100U, new_weight)); }

void HostSetImpl::addMemberUpdateCb(MemberUpdateCb callback) const {
  callbacks_.emplace_back(callback);
}

ClusterStats ClusterImplBase::generateStats(const std::string& name, Stats::Store& stats) {
  std::string prefix(fmt::format("cluster.{}.", name));
  return {ALL_CLUSTER_STATS(POOL_COUNTER_PREFIX(stats, prefix), POOL_GAUGE_PREFIX(stats, prefix),
                            POOL_TIMER_PREFIX(stats, prefix))};
}

void HostSetImpl::runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                                     const std::vector<HostPtr>& hosts_removed) {
  for (MemberUpdateCb& callback : callbacks_) {
    callback(hosts_added, hosts_removed);
  }
}

const ConstHostVectorPtr ClusterImplBase::empty_host_list_{new std::vector<HostPtr>{}};

ClusterImplBase::ClusterImplBase(const Json::Object& config, Runtime::Loader& runtime,
                                 Stats::Store& stats, Ssl::ContextManager& ssl_context_manager)
    : name_(config.getString("name")),
      max_requests_per_connection_(config.getInteger("max_requests_per_connection", 0)),
      connect_timeout_(std::chrono::milliseconds(config.getInteger("connect_timeout_ms"))),
      stats_(generateStats(name_, stats)), alt_stat_name_(config.getString("alt_stat_name", "")),
      features_(parseFeatures(config)),
      http_codec_options_(Http::Utility::parseCodecOptions(config)),
      resource_managers_(config, runtime, name_) {

  std::string string_lb_type = config.getString("lb_type");
  if (string_lb_type == "round_robin") {
    lb_type_ = LoadBalancerType::RoundRobin;
  } else if (string_lb_type == "least_request") {
    lb_type_ = LoadBalancerType::LeastRequest;
  } else if (string_lb_type == "random") {
    lb_type_ = LoadBalancerType::Random;
  } else {
    throw EnvoyException(fmt::format("cluster: unknown LB type '{}'", string_lb_type));
  }

  ssl_ctx_ = nullptr;
  if (config.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(config.getObject("ssl_context"));
    ssl_ctx_ = &ssl_context_manager.createSslClientContext(fmt::format("cluster.{}", name_), stats,
                                                           context_config);
  }
}

ConstHostVectorPtr ClusterImplBase::createHealthyHostList(const std::vector<HostPtr>& hosts) {
  HostVectorPtr healthy_list(new std::vector<HostPtr>());
  for (auto host : hosts) {
    if (host->healthy()) {
      healthy_list->emplace_back(host);
    }
  }

  return healthy_list;
}

uint64_t ClusterImplBase::parseFeatures(const Json::Object& config) {
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

ResourceManager& ClusterImplBase::resourceManager(ResourcePriority priority) const {
  ASSERT(enumToInt(priority) < resource_managers_.managers_.size());
  return *resource_managers_.managers_[enumToInt(priority)];
}

void ClusterImplBase::runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                                         const std::vector<HostPtr>& hosts_removed) {
  if (!hosts_added.empty() || !hosts_removed.empty()) {
    stats_.membership_change_.inc();
  }

  stats_.membership_total_.set(hosts().size());
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
      updateHosts(rawHosts(), createHealthyHostList(*rawHosts()), rawLocalZoneHosts(),
                  createHealthyHostList(*rawLocalZoneHosts()), {}, {});
    }
  });
}

void ClusterImplBase::setOutlierDetector(OutlierDetectorPtr&& outlier_detector) {
  if (!outlier_detector) {
    return;
  }

  outlier_detector_ = std::move(outlier_detector);
  outlier_detector_->addChangedStateCb([this](HostPtr) -> void {
    updateHosts(rawHosts(), createHealthyHostList(*rawHosts()), rawLocalZoneHosts(),
                createHealthyHostList(*rawLocalZoneHosts()), {}, {});
  });
}

ClusterImplBase::ResourceManagers::ResourceManagers(const Json::Object& config,
                                                    Runtime::Loader& runtime,
                                                    const std::string& cluster_name) {
  managers_[enumToInt(ResourcePriority::Default)] = load(config, runtime, cluster_name, "default");
  managers_[enumToInt(ResourcePriority::High)] = load(config, runtime, cluster_name, "high");
}

ResourceManagerImplPtr ClusterImplBase::ResourceManagers::load(const Json::Object& config,
                                                               Runtime::Loader& runtime,
                                                               const std::string& cluster_name,
                                                               const std::string& priority) {
  uint64_t max_connections = 1024;
  uint64_t max_pending_requests = 1024;
  uint64_t max_requests = 1024;
  uint64_t max_retries = 3;
  std::string runtime_prefix = fmt::format("circuit_breakers.{}.{}.", cluster_name, priority);

  Json::Object settings = config.getObject("circuit_breakers", true).getObject(priority, true);
  max_connections = settings.getInteger("max_connections", max_connections);
  max_pending_requests = settings.getInteger("max_pending_requests", max_pending_requests);
  max_requests = settings.getInteger("max_requests", max_requests);
  max_retries = settings.getInteger("max_retries", max_retries);

  return ResourceManagerImplPtr{new ResourceManagerImpl(
      runtime, runtime_prefix, max_connections, max_pending_requests, max_requests, max_retries)};
}

StaticClusterImpl::StaticClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                                     Stats::Store& stats, Ssl::ContextManager& ssl_context_manager)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager) {
  std::vector<Json::Object> hosts_json = config.getObjectArray("hosts");
  HostVectorPtr new_hosts(new std::vector<HostPtr>());
  for (Json::Object& host : hosts_json) {
    std::string url = host.getString("url");
    // resolve the URL to make sure it's valid
    Network::Utility::resolve(url);
    new_hosts->emplace_back(HostPtr{new HostImpl(*this, url, false, 1, "")});
  }

  updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_list_, empty_host_list_, {},
              {});
}

bool BaseDynamicClusterImpl::updateDynamicHostList(const std::vector<HostPtr>& new_hosts,
                                                   std::vector<HostPtr>& current_hosts,
                                                   std::vector<HostPtr>& hosts_added,
                                                   std::vector<HostPtr>& hosts_removed,
                                                   bool depend_on_hc) {
  uint64_t max_host_weight = 1;
  std::unordered_set<std::string> zones;

  // Go through and see if the list we have is different from what we just got. If it is, we
  // make a new host list and raise a change notification. This uses an N^2 search given that
  // this does not happen very often and the list sizes should be small.
  std::vector<HostPtr> final_hosts;
  for (HostPtr host : new_hosts) {
    bool found = false;
    for (auto i = current_hosts.begin(); i != current_hosts.end();) {
      // If we find a host matched based on URL, we keep it. However we do change weight inline so
      // do that here.
      if ((*i)->url() == host->url()) {
        zones.insert((*i)->zone());
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
      zones.insert(host->zone());

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
        zones.insert((*i)->zone());

        final_hosts.push_back(*i);
        i = current_hosts.erase(i);
      } else {
        i++;
      }
    }
  }

  stats_.max_host_weight_.set(max_host_weight);
  stats_.upstream_zone_count_.set(zones.size());

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
                                           Network::DnsResolver& dns_resolver)
    : BaseDynamicClusterImpl(config, runtime, stats, ssl_context_manager),
      dns_resolver_(dns_resolver), dns_refresh_rate_ms_(std::chrono::milliseconds(
                                       config.getInteger("dns_refresh_rate_ms", 5000))) {
  for (Json::Object& host : config.getObjectArray("hosts")) {
    resolve_targets_.emplace_back(new ResolveTarget(*this, host.getString("url")));
  }
}

StrictDnsClusterImpl::~StrictDnsClusterImpl() {}

void StrictDnsClusterImpl::updateAllHosts(const std::vector<HostPtr>& hosts_added,
                                          const std::vector<HostPtr>& hosts_removed) {
  // At this point we know that we are different so make a new host list and notify.
  HostVectorPtr new_hosts(new std::vector<HostPtr>());
  for (const ResolveTargetPtr& target : resolve_targets_) {
    for (const HostPtr& host : target->hosts_) {
      new_hosts->emplace_back(host);
    }
  }

  updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_list_, empty_host_list_,
              hosts_added, hosts_removed);
}

StrictDnsClusterImpl::ResolveTarget::ResolveTarget(StrictDnsClusterImpl& parent,
                                                   const std::string& url)
    : parent_(parent), dns_address_(Network::Utility::hostFromUrl(url)),
      port_(Network::Utility::portFromUrl(url)),
      resolve_timer_(
          parent_.dns_resolver_.dispatcher().createTimer([this]() -> void { startResolve(); })) {

  startResolve();
}

void StrictDnsClusterImpl::ResolveTarget::startResolve() {
  log_debug("starting async DNS resolution for {}", dns_address_);
  parent_.stats_.update_attempt_.inc();

  parent_.dns_resolver_.resolve(
      dns_address_, [this](std::list<std::string>&& address_list) -> void {

        log_debug("async DNS resolution complete for {}", dns_address_);
        parent_.stats_.update_success_.inc();

        std::vector<HostPtr> new_hosts;
        for (const std::string& address : address_list) {
          new_hosts.emplace_back(
              new HostImpl(parent_, Network::Utility::urlForTcp(address, port_), false, 1, ""));
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

        resolve_timer_->enableTimer(parent_.dns_refresh_rate_ms_);
      });
}

void HostDescriptionImpl::checkUrl() {
  if (url_.find(Network::Utility::TCP_SCHEME) == 0) {
    Network::Utility::hostFromUrl(url_);
    Network::Utility::portFromUrl(url_);
  } else if (url_.find(Network::Utility::UNIX_SCHEME) == 0) {
    Network::Utility::pathFromUrl(url_);
  } else {
    throw EnvoyException(fmt::format("malformed url: {}", url_));
  }
}

} // Upstream
