#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

DynamicModuleClusterHandle::~DynamicModuleClusterHandle() {
  std::shared_ptr<DynamicModuleCluster> cluster = std::move(cluster_);
  cluster_.reset();
  Event::Dispatcher& dispatcher = cluster->dispatcher_;
  dispatcher.post([cluster = std::move(cluster)]() mutable { cluster.reset(); });
}

DynamicModuleCluster::DynamicModuleCluster(
    const envoy::config::cluster::v3::Cluster& cluster, DynamicModuleClusterConfigSharedPtr config,
    bool dynamic_host_discovery, std::chrono::milliseconds cleanup_interval, uint32_t max_hosts,
    Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : ClusterImplBase(cluster, context, creation_status), config_(std::move(config)),
      in_module_cluster_(nullptr),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      dynamic_host_discovery_(dynamic_host_discovery), cleanup_interval_(cleanup_interval),
      max_hosts_(max_hosts),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })) {

  // Create the in-module cluster instance.
  in_module_cluster_ = config_->on_cluster_new_(config_->in_module_config_, this);
  if (in_module_cluster_ == nullptr) {
    creation_status = absl::InvalidArgumentError("Failed to create in-module cluster instance");
    return;
  }

  // Register for host set change notifications.
  auto& priority_set = prioritySet();
  member_update_cb_ = priority_set.addMemberUpdateCb(
      [this](const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed) {
        onHostSetChange(hosts_added, hosts_removed);
        return absl::OkStatus();
      });

  // Initialize the priority set with an empty host set at priority 0.
  priority_set_.getOrCreateHostSet(0);
}

DynamicModuleCluster::~DynamicModuleCluster() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  cleanup_timer_->disableTimer();
  if (in_module_cluster_ != nullptr && config_->on_cluster_destroy_ != nullptr) {
    config_->on_cluster_destroy_(in_module_cluster_);
  }
}

void DynamicModuleCluster::startPreInit() {
  // Call the module's init function.
  config_->on_cluster_init_(in_module_cluster_, this);

  // Enable the cleanup timer.
  cleanup_timer_->enableTimer(cleanup_interval_);
}

void DynamicModuleCluster::cleanup() {
  config_->on_cluster_cleanup_(in_module_cluster_, this);
  cleanup_timer_->enableTimer(cleanup_interval_);
}

void DynamicModuleCluster::preInitComplete() { onPreInitComplete(); }

envoy_dynamic_module_type_cluster_error
DynamicModuleCluster::addHost(const std::string& address, uint32_t weight,
                              Upstream::HostSharedPtr* result_host) {
  // Validate weight.
  if (weight == 0 || weight > 128) {
    return envoy_dynamic_module_type_cluster_error_InvalidWeight;
  }

  // Parse the address.
  Network::Address::InstanceConstSharedPtr resolved_address =
      Network::Utility::parseInternetAddressAndPortNoThrow(address, false);
  if (resolved_address == nullptr) {
    return envoy_dynamic_module_type_cluster_error_InvalidAddress;
  }

  // Check max hosts limit.
  {
    absl::ReaderMutexLock lock(&host_map_lock_);
    if (host_map_.size() >= max_hosts_) {
      return envoy_dynamic_module_type_cluster_error_MaxHostsReached;
    }
    // Check if host already exists.
    if (host_map_.contains(address)) {
      // Host already exists, return it.
      if (result_host != nullptr) {
        *result_host = host_map_.at(address);
      }
      return envoy_dynamic_module_type_cluster_error_Ok;
    }
  }

  // Create the host.
  auto cluster_info = info();
  auto host_result = Upstream::HostImpl::create(
      cluster_info, cluster_info->name() + address, std::move(resolved_address), nullptr, nullptr,
      weight, std::make_shared<envoy::config::core::v3::Locality>(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
      envoy::config::core::v3::UNKNOWN);
  if (!host_result.ok()) {
    return envoy_dynamic_module_type_cluster_error_InvalidAddress; // LCOV_EXCL_LINE
  }
  Upstream::HostSharedPtr host = std::move(host_result.value());

  // Add to our map.
  {
    absl::WriterMutexLock lock(&host_map_lock_);
    host_map_[address] = host;
  }

  // Update the priority set.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  Upstream::HostVectorSharedPtr all_hosts(new Upstream::HostVector(first_host_set.hosts()));
  all_hosts->emplace_back(host);
  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, Upstream::HostsPerLocalityImpl::empty()),
      {}, {host}, {}, absl::nullopt, absl::nullopt);

  if (result_host != nullptr) {
    *result_host = host;
  }

  ENVOY_LOG(debug, "Added host {} with weight {} to dynamic module cluster.", address, weight);
  return envoy_dynamic_module_type_cluster_error_Ok;
}

envoy_dynamic_module_type_cluster_error
DynamicModuleCluster::removeHost(Upstream::HostSharedPtr host) {
  std::string address = host->address()->asString();

  {
    absl::WriterMutexLock lock(&host_map_lock_);
    auto it = host_map_.find(address);
    if (it == host_map_.end()) {
      return envoy_dynamic_module_type_cluster_error_HostNotFound;
    }
    host_map_.erase(it);
  }

  // Update the priority set.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  Upstream::HostVectorSharedPtr remaining_hosts(new Upstream::HostVector());
  for (const auto& h : first_host_set.hosts()) {
    if (h->address()->asString() != address) {
      remaining_hosts->emplace_back(h);
    }
  }
  priority_set_.updateHosts(0,
                            Upstream::HostSetImpl::partitionHosts(
                                remaining_hosts, Upstream::HostsPerLocalityImpl::empty()),
                            {}, {}, {host}, absl::nullopt, absl::nullopt);

  ENVOY_LOG(debug, "Removed host {} from dynamic module cluster.", address);
  return envoy_dynamic_module_type_cluster_error_Ok;
}

std::vector<envoy_dynamic_module_type_host_info> DynamicModuleCluster::getHosts() {
  std::vector<envoy_dynamic_module_type_host_info> result;
  absl::ReaderMutexLock lock(&host_map_lock_);
  result.reserve(host_map_.size());
  for (const auto& [addr, host] : host_map_) {
    envoy_dynamic_module_type_host_info info;
    info.host = const_cast<Upstream::Host*>(host.get());
    info.address = {addr.data(), addr.size()};
    info.weight = host->weight();
    info.health = convertHealth(host->coarseHealth());
    result.push_back(info);
  }
  return result;
}

Upstream::HostSharedPtr DynamicModuleCluster::getHostByAddress(const std::string& address) {
  absl::ReaderMutexLock lock(&host_map_lock_);
  auto it = host_map_.find(address);
  if (it != host_map_.end()) {
    return it->second;
  }
  return nullptr;
}

envoy_dynamic_module_type_cluster_error
DynamicModuleCluster::setHostWeight(Upstream::HostSharedPtr host, uint32_t weight) {
  if (weight == 0 || weight > 128) {
    return envoy_dynamic_module_type_cluster_error_InvalidWeight;
  }
  host->weight(weight);
  return envoy_dynamic_module_type_cluster_error_Ok;
}

void DynamicModuleCluster::onHostSetChange(const Upstream::HostVector& hosts_added,
                                           const Upstream::HostVector& hosts_removed) {
  if (config_->on_host_set_change_ == nullptr) {
    return; // LCOV_EXCL_LINE
  }

  // Store address strings separately to ensure they outlive the host_info structs.
  std::vector<std::string> added_addresses;
  added_addresses.reserve(hosts_added.size());
  std::vector<envoy_dynamic_module_type_host_info> added_infos;
  added_infos.reserve(hosts_added.size());
  for (const auto& host : hosts_added) {
    added_addresses.push_back(host->address()->asString());
    const std::string& addr = added_addresses.back();
    envoy_dynamic_module_type_host_info info;
    info.host = const_cast<Upstream::Host*>(host.get());
    info.address = {addr.data(), addr.size()};
    info.weight = host->weight();
    info.health = convertHealth(host->coarseHealth());
    added_infos.push_back(info);
  }

  std::vector<std::string> removed_addresses;
  removed_addresses.reserve(hosts_removed.size());
  std::vector<envoy_dynamic_module_type_host_info> removed_infos;
  removed_infos.reserve(hosts_removed.size());
  for (const auto& host : hosts_removed) {
    removed_addresses.push_back(host->address()->asString());
    const std::string& addr = removed_addresses.back();
    envoy_dynamic_module_type_host_info info;
    info.host = const_cast<Upstream::Host*>(host.get());
    info.address = {addr.data(), addr.size()};
    info.weight = host->weight();
    info.health = convertHealth(host->coarseHealth());
    removed_infos.push_back(info);
  }

  config_->on_host_set_change_(in_module_cluster_, added_infos.data(), added_infos.size(),
                               removed_infos.data(), removed_infos.size());
}

void DynamicModuleCluster::onHealthCheckComplete(Upstream::HostSharedPtr host,
                                                 Upstream::HealthTransition health_transition) {
  if (config_->on_host_health_change_ == nullptr) {
    return; // LCOV_EXCL_LINE
  }

  config_->on_host_health_change_(in_module_cluster_, const_cast<Upstream::Host*>(host.get()),
                                  convertHealth(host->coarseHealth()),
                                  convertHealthTransition(health_transition));
}

envoy_dynamic_module_type_host_health
DynamicModuleCluster::convertHealth(Upstream::Host::Health health) {
  switch (health) {
  case Upstream::Host::Health::Healthy:
    return envoy_dynamic_module_type_host_health_Healthy;
  case Upstream::Host::Health::Unhealthy:
    return envoy_dynamic_module_type_host_health_Unhealthy;
  case Upstream::Host::Health::Degraded:
    return envoy_dynamic_module_type_host_health_Degraded;
  }
  return envoy_dynamic_module_type_host_health_Unknown; // LCOV_EXCL_LINE
}

envoy_dynamic_module_type_health_transition
DynamicModuleCluster::convertHealthTransition(Upstream::HealthTransition transition) {
  switch (transition) {
  case Upstream::HealthTransition::Unchanged:
    return envoy_dynamic_module_type_health_transition_Unchanged;
  case Upstream::HealthTransition::Changed:
    return envoy_dynamic_module_type_health_transition_Changed;
  case Upstream::HealthTransition::ChangePending:
    return envoy_dynamic_module_type_health_transition_ChangePending;
  }
  return envoy_dynamic_module_type_health_transition_Unchanged; // LCOV_EXCL_LINE
}

// DynamicModuleLoadBalancer implementation.

DynamicModuleLoadBalancer::DynamicModuleLoadBalancer(
    const DynamicModuleClusterHandleSharedPtr& handle)
    : handle_(handle), in_module_lb_(nullptr) {
  in_module_lb_ =
      handle_->cluster_->config()->on_load_balancer_new_(handle_->cluster_->inModuleCluster());
}

DynamicModuleLoadBalancer::~DynamicModuleLoadBalancer() {
  if (in_module_lb_ != nullptr &&
      handle_->cluster_->config()->on_load_balancer_destroy_ != nullptr) {
    handle_->cluster_->config()->on_load_balancer_destroy_(in_module_lb_);
  }
}

Upstream::HostSelectionResponse
DynamicModuleLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (in_module_lb_ == nullptr) {
    return {nullptr};
  }

  auto* host_ptr =
      handle_->cluster_->config()->on_load_balancer_choose_host_(in_module_lb_, context);
  if (host_ptr == nullptr) {
    return {nullptr};
  }

  // Find the host in the cluster's priority set.
  const auto& priority_set = handle_->cluster_->prioritySet();
  for (const auto& host_set : priority_set.hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      if (host.get() == host_ptr) {
        return {host};
      }
    }
  }

  return {nullptr};
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
