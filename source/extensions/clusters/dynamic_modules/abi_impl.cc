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

envoy_dynamic_module_type_cluster_host_envoy_ptr envoy_dynamic_module_callback_cluster_add_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t weight) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::string address_str(address.ptr, address.length);
  auto host = cluster->addHost(address_str, weight);
  if (host == nullptr) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(host.get());
}

bool envoy_dynamic_module_callback_cluster_remove_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr host_envoy_ptr) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  auto host = cluster->findHost(host_envoy_ptr);
  if (host == nullptr) {
    return false;
  }
  return cluster->removeHost(host);
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
