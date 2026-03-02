// NOLINT(namespace-envoy)

// This file provides host-side implementations for the cluster dynamic module ABI callbacks.

#include "source/common/common/assert.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

namespace {

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleCluster*
getCluster(envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleCluster*>(
      cluster_envoy_ptr);
}

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleLoadBalancer*
getLb(envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleLoadBalancer*>(
      lb_envoy_ptr);
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_cluster_add_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    const envoy_dynamic_module_type_module_buffer* addresses, const uint32_t* weights, size_t count,
    envoy_dynamic_module_type_cluster_host_envoy_ptr* result_host_ptrs) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::vector<std::string> address_strings;
  address_strings.reserve(count);
  std::vector<uint32_t> weight_vec(weights, weights + count);
  for (size_t i = 0; i < count; ++i) {
    address_strings.emplace_back(addresses[i].ptr, addresses[i].length);
  }
  std::vector<Envoy::Upstream::HostSharedPtr> result_hosts;
  if (!cluster->addHosts(address_strings, weight_vec, result_hosts)) {
    return false;
  }
  for (size_t i = 0; i < result_hosts.size(); ++i) {
    result_host_ptrs[i] = const_cast<Envoy::Upstream::Host*>(result_hosts[i].get());
  }
  return true;
}

size_t envoy_dynamic_module_callback_cluster_remove_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    const envoy_dynamic_module_type_cluster_host_envoy_ptr* host_envoy_ptrs, size_t count) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::vector<Envoy::Upstream::HostSharedPtr> hosts;
  hosts.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    hosts.emplace_back(cluster->findHost(host_envoy_ptrs[i]));
  }
  return cluster->removeHosts(hosts);
}

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  getCluster(cluster_envoy_ptr)->preInitComplete();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  auto* lb = getLb(lb_envoy_ptr);
  const auto& priority_set = lb->prioritySet();
  if (priority >= priority_set.hostSetsPerPriority().size()) {
    return 0;
  }
  return priority_set.hostSetsPerPriority()[priority]->healthyHosts().size();
}

envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  auto* lb = getLb(lb_envoy_ptr);
  const auto& priority_set = lb->prioritySet();
  if (priority >= priority_set.hostSetsPerPriority().size()) {
    return nullptr;
  }
  const auto& healthy_hosts = priority_set.hostSetsPerPriority()[priority]->healthyHosts();
  if (index >= healthy_hosts.size()) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(healthy_hosts[index].get());
}

} // extern "C"
