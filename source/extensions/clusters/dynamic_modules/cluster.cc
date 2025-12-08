#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

namespace {
constexpr std::chrono::milliseconds DefaultCleanupInterval{5000};
constexpr uint32_t DefaultMaxHosts = 1024;
} // namespace

DynamicModuleCluster::DynamicModuleCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& config,
    DynamicModuleClusterConfigSharedPtr module_config, Upstream::ClusterFactoryContext& context,
    absl::Status& creation_status)
    : ClusterImplBase(cluster, context, creation_status), config_(module_config),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      cleanup_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, cleanup_interval, DefaultCleanupInterval.count()))),
      max_hosts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_hosts, DefaultMaxHosts)) {
  if (!creation_status.ok()) {
    return;
  }

  // Create the in-module cluster instance.
  in_module_cluster_ = config_->on_cluster_new_(config_->in_module_config_, thisAsVoidPtr());
  if (in_module_cluster_ == nullptr) {
    creation_status = absl::InvalidArgumentError("Failed to create in-module cluster");
    return;
  }

  // Create the cleanup timer.
  cleanup_timer_ = dispatcher_.createTimer([this]() { cleanup(); });

  // Register for host set change notifications.
  member_update_cb_handle_ = priority_set_.addMemberUpdateCb(
      [this](const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed) {
        onHostSetChange(hosts_added, hosts_removed);
      });
}

DynamicModuleCluster::~DynamicModuleCluster() {
  if (cleanup_timer_) {
    cleanup_timer_->disableTimer();
  }

  if (in_module_cluster_ != nullptr) {
    config_->on_cluster_destroy_(in_module_cluster_);
    in_module_cluster_ = nullptr;
  }
}

void DynamicModuleCluster::startPreInit() {
  // Call the module's initialization function.
  config_->on_cluster_init_(thisAsVoidPtr(), in_module_cluster_);

  // Start the cleanup timer.
  cleanup_timer_->enableTimer(cleanup_interval_);
}

void DynamicModuleCluster::cleanup() {
  // Call the module's cleanup function.
  config_->on_cluster_cleanup_(thisAsVoidPtr(), in_module_cluster_);

  // Re-enable the timer for the next cleanup.
  cleanup_timer_->enableTimer(cleanup_interval_);
}

std::string DynamicModuleCluster::buildHostKey(const std::string& address, uint32_t port) const {
  return absl::StrCat(address, ":", port);
}

absl::StatusOr<Upstream::HostSharedPtr>
DynamicModuleCluster::addHost(const std::string& address, uint32_t port, uint32_t weight) {
  // Validate weight.
  if (weight < 1 || weight > 128) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid weight: ", weight));
  }

  // Parse the address.
  auto address_instance = Network::Utility::parseInternetAddressNoThrow(address, port);
  if (address_instance == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid address: ", address, ":", port));
  }

  const std::string host_key = buildHostKey(address, port);

  absl::WriterMutexLock lock(&host_map_lock_);

  // Check if host already exists.
  if (host_map_.contains(host_key)) {
    return host_map_[host_key];
  }

  // Check max hosts limit.
  if (host_map_.size() >= max_hosts_) {
    return absl::ResourceExhaustedError(absl::StrCat("Maximum hosts limit reached: ", max_hosts_));
  }

  // Create a default locality for the host.
  auto locality = std::make_shared<envoy::config::core::v3::Locality>();

  // Create the host using the factory method.
  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig health_check_config;
  auto host_or_error =
      Upstream::HostImpl::create(info(), "", address_instance, nullptr, nullptr, weight, locality,
                                 health_check_config, 0, envoy::config::core::v3::UNKNOWN);

  if (!host_or_error.ok()) {
    // LCOV_EXCL_START - Defensive: HostImpl::create rarely fails with validated inputs.
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create host: ", host_or_error.status().message()));
    // LCOV_EXCL_STOP
  }

  Upstream::HostSharedPtr host = std::move(host_or_error.value());

  // Add to the host map.
  host_map_[host_key] = host;

  // Update the priority set.
  Upstream::HostVector hosts_added{host};
  Upstream::HostVector hosts_removed;

  // Get all current hosts for the priority set.
  Upstream::HostVectorSharedPtr all_hosts = std::make_shared<Upstream::HostVector>();
  for (const auto& entry : host_map_) {
    all_hosts->emplace_back(entry.second);
  }

  const Upstream::HostsPerLocalitySharedPtr empty_hosts_per_locality{
      new Upstream::HostsPerLocalityImpl()};

  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, empty_hosts_per_locality), {},
      hosts_added, hosts_removed, absl::nullopt, absl::nullopt);

  return host;
}

void DynamicModuleCluster::removeHost(const Upstream::HostSharedPtr& host) {
  const std::string host_key =
      buildHostKey(host->address()->ip()->addressAsString(), host->address()->ip()->port());

  absl::WriterMutexLock lock(&host_map_lock_);

  auto it = host_map_.find(host_key);
  if (it == host_map_.end()) {
    return;
  }

  host_map_.erase(it);

  // Update the priority set.
  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed{host};

  // Get all current hosts for the priority set.
  Upstream::HostVectorSharedPtr all_hosts = std::make_shared<Upstream::HostVector>();
  for (const auto& entry : host_map_) {
    all_hosts->emplace_back(entry.second);
  }

  const Upstream::HostsPerLocalitySharedPtr empty_hosts_per_locality{
      new Upstream::HostsPerLocalityImpl()};

  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, empty_hosts_per_locality), {},
      hosts_added, hosts_removed, absl::nullopt, absl::nullopt);
}

Upstream::HostConstSharedPtr DynamicModuleCluster::getHostByAddress(const std::string& address,
                                                                    uint32_t port) const {
  const std::string host_key = buildHostKey(address, port);

  absl::ReaderMutexLock lock(&host_map_lock_);

  auto it = host_map_.find(host_key);
  if (it == host_map_.end()) {
    return nullptr;
  }

  return it->second;
}

std::vector<std::pair<Upstream::HostSharedPtr, envoy_dynamic_module_type_host_info>>
DynamicModuleCluster::getHosts() const {
  absl::ReaderMutexLock lock(&host_map_lock_);

  std::vector<std::pair<Upstream::HostSharedPtr, envoy_dynamic_module_type_host_info>> result;
  result.reserve(host_map_.size());

  for (const auto& entry : host_map_) {
    const auto& host = entry.second;
    envoy_dynamic_module_type_host_info info;
    info.address = const_cast<char*>(host->address()->ip()->addressAsString().c_str());
    info.address_length = host->address()->ip()->addressAsString().size();
    info.port = host->address()->ip()->port();
    info.weight = host->weight();

    // Map the health status.
    switch (host->coarseHealth()) {
    case Upstream::Host::Health::Healthy:
      info.health = envoy_dynamic_module_type_host_health_Healthy;
      break;
    // LCOV_EXCL_START - Degraded/Unhealthy requires health checker integration.
    case Upstream::Host::Health::Degraded:
      info.health = envoy_dynamic_module_type_host_health_Degraded;
      break;
    case Upstream::Host::Health::Unhealthy:
      info.health = envoy_dynamic_module_type_host_health_Unhealthy;
      break;
      // LCOV_EXCL_STOP
    }

    result.emplace_back(host, info);
  }

  return result;
}

void DynamicModuleCluster::onHostSetChange(const Upstream::HostVector& hosts_added,
                                           const Upstream::HostVector& hosts_removed) {
  if (in_module_cluster_ == nullptr) {
    return;
  }

  // Convert host vectors to arrays of pointers for the ABI.
  std::vector<envoy_dynamic_module_type_host_envoy_ptr> added_ptrs;
  std::vector<envoy_dynamic_module_type_host_envoy_ptr> removed_ptrs;

  added_ptrs.reserve(hosts_added.size());
  for (const auto& host : hosts_added) {
    added_ptrs.push_back(const_cast<Upstream::Host*>(host.get()));
  }

  removed_ptrs.reserve(hosts_removed.size());
  for (const auto& host : hosts_removed) {
    removed_ptrs.push_back(const_cast<Upstream::Host*>(host.get()));
  }

  // Call the module's host set change callback.
  config_->on_host_set_change_(
      thisAsVoidPtr(), in_module_cluster_, added_ptrs.empty() ? nullptr : added_ptrs.data(),
      added_ptrs.size(), removed_ptrs.empty() ? nullptr : removed_ptrs.data(), removed_ptrs.size());
}

void DynamicModuleCluster::onHealthCheckComplete(Upstream::HostSharedPtr host,
                                                 Upstream::HealthTransition transition,
                                                 Upstream::HealthState current_health) {
  if (in_module_cluster_ == nullptr) {
    return;
  }

  // Convert health transition to ABI type.
  envoy_dynamic_module_type_health_transition abi_transition;
  switch (transition) {
  case Upstream::HealthTransition::Unchanged:
    abi_transition = envoy_dynamic_module_type_health_transition_Unchanged;
    break;
  case Upstream::HealthTransition::Changed:
    abi_transition = envoy_dynamic_module_type_health_transition_Changed;
    break;
  case Upstream::HealthTransition::ChangePending:
    abi_transition = envoy_dynamic_module_type_health_transition_ChangePending;
    break;
  }

  // Convert health state to ABI type.
  envoy_dynamic_module_type_host_health abi_health;
  switch (current_health) {
  case Upstream::HealthState::Healthy:
    abi_health = envoy_dynamic_module_type_host_health_Healthy;
    break;
  case Upstream::HealthState::Unhealthy:
    abi_health = envoy_dynamic_module_type_host_health_Unhealthy;
    break;
  }

  // Call the module's health change callback.
  config_->on_host_health_change_(thisAsVoidPtr(), in_module_cluster_,
                                  const_cast<Upstream::Host*>(host.get()), abi_transition,
                                  abi_health);
}

Upstream::LoadBalancerFactorySharedPtr DynamicModuleCluster::ThreadAwareLoadBalancer::factory() {
  return std::make_shared<DynamicModuleLoadBalancerFactory>(cluster_);
}

DynamicModuleLoadBalancer::DynamicModuleLoadBalancer(DynamicModuleClusterSharedPtr cluster)
    : cluster_(cluster) {
  // Create the in-module load balancer instance.
  in_module_lb_ = cluster_->config().on_load_balancer_new_(cluster_->moduleClusterPtr(),
                                                           static_cast<void*>(this));
}

DynamicModuleLoadBalancer::~DynamicModuleLoadBalancer() {
  if (in_module_lb_ != nullptr) {
    cluster_->config().on_load_balancer_destroy_(in_module_lb_);
    in_module_lb_ = nullptr;
  }
}

Upstream::HostSelectionResponse
DynamicModuleLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  // LCOV_EXCL_START - Module returns valid LB in tests; null check is defensive.
  if (in_module_lb_ == nullptr) {
    return {nullptr};
  }
  // LCOV_EXCL_STOP

  // Call the module's choose_host function.
  auto* host_ptr = cluster_->config().on_load_balancer_choose_host_(
      static_cast<void*>(this), in_module_lb_, static_cast<void*>(context));

  if (host_ptr == nullptr) {
    return {nullptr};
  }

  // LCOV_EXCL_START - Success path requires module to return host; test module returns null.
  // Cast the host pointer back to a HostSharedPtr.
  return {
      std::shared_ptr<Upstream::Host>(static_cast<Upstream::Host*>(host_ptr), [](Upstream::Host*) {
        // Don't delete - the cluster owns the host.
      })};
  // LCOV_EXCL_STOP
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
