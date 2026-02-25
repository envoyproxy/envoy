#include "source/extensions/load_balancing_policies/dynamic_modules/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

DynamicModuleLoadBalancer::DynamicModuleLoadBalancer(DynamicModuleLbConfigSharedPtr config,
                                                     const Upstream::PrioritySet& priority_set,
                                                     const std::string& cluster_name)
    : config_(std::move(config)), priority_set_(priority_set), cluster_name_(cluster_name),
      in_module_lb_(nullptr) {
  // Create the in-module load balancer instance.
  in_module_lb_ = config_->on_lb_new_(config_->in_module_config_, this);
  if (in_module_lb_ == nullptr) {
    ENVOY_LOG(error, "failed to create in-module load balancer instance");
  }
}

DynamicModuleLoadBalancer::~DynamicModuleLoadBalancer() {
  if (in_module_lb_ != nullptr && config_->on_lb_destroy_ != nullptr) {
    config_->on_lb_destroy_(in_module_lb_);
    in_module_lb_ = nullptr;
  }
}

Upstream::HostSelectionResponse
DynamicModuleLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (in_module_lb_ == nullptr) {
    return {nullptr};
  }

  // Call the module's chooseHost function.
  uint32_t priority = 0;
  uint32_t host_index = 0;
  bool selected = config_->on_choose_host_(this, in_module_lb_, context, &priority, &host_index);

  if (!selected) {
    return {nullptr};
  }

  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    ENVOY_LOG(warn, "dynamic module returned invalid priority {} (priorities: {})", priority,
              host_sets.size());
    return {nullptr};
  }

  const auto& healthy_hosts = host_sets[priority]->healthyHosts();
  if (host_index >= healthy_hosts.size()) {
    ENVOY_LOG(warn,
              "dynamic module returned invalid host index {} at priority {} (healthy hosts: {})",
              host_index, priority, healthy_hosts.size());
    return {nullptr};
  }

  return {healthy_hosts[host_index]};
}

Upstream::HostConstSharedPtr
DynamicModuleLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext*) {
  // Not implemented - return nullptr.
  return nullptr;
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
DynamicModuleLoadBalancer::lifetimeCallbacks() {
  return {};
}

absl::optional<Upstream::SelectedPoolAndConnection>
DynamicModuleLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext*,
                                                    const Upstream::Host&, std::vector<uint8_t>&) {
  return absl::nullopt;
}

bool DynamicModuleLoadBalancer::setHostData(uint32_t priority, size_t index, uintptr_t data) {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return false;
  }
  if (data == 0) {
    per_host_data_.erase({priority, index});
  } else {
    per_host_data_[{priority, index}] = data;
  }
  return true;
}

bool DynamicModuleLoadBalancer::getHostData(uint32_t priority, size_t index,
                                            uintptr_t* data) const {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return false;
  }
  auto it = per_host_data_.find({priority, index});
  if (it != per_host_data_.end()) {
    *data = it->second;
  } else {
    *data = 0;
  }
  return true;
}

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
