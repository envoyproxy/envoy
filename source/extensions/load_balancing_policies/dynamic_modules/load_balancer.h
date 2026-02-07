#pragma once

#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

/**
 * A load balancer implementation that delegates host selection to a dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer,
                                  public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleLoadBalancer(DynamicModuleLbConfigSharedPtr config,
                            const Upstream::PrioritySet& priority_set,
                            const std::string& cluster_name);
  ~DynamicModuleLoadBalancer() override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

  // Accessors for callbacks.
  const std::string& clusterName() const { return cluster_name_; }
  const Upstream::PrioritySet& prioritySet() const { return priority_set_; }

private:
  DynamicModuleLbConfigSharedPtr config_;
  const Upstream::PrioritySet& priority_set_;
  std::string cluster_name_;
  envoy_dynamic_module_type_lb_module_ptr in_module_lb_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
