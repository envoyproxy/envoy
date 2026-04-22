// NOLINT(namespace-envoy)

// This file provides host-side implementations for the cluster dynamic module ABI callbacks.

#include "source/common/common/assert.h"
#include "source/common/http/message_impl.h"
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

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig*
getConfig(envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig*>(
      config_envoy_ptr);
}

Envoy::Upstream::LoadBalancerContext*
getContext(envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr) {
  return static_cast<Envoy::Upstream::LoadBalancerContext*>(context_envoy_ptr);
}

// Helper to look up a metadata value by filter name and key for a host in the cluster priority set.
const Envoy::Protobuf::Value*
getClusterHostMetadataValue(envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
                            uint32_t priority, size_t index,
                            envoy_dynamic_module_type_module_buffer filter_name,
                            envoy_dynamic_module_type_module_buffer key) {
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return nullptr;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return nullptr;
  }
  const auto& metadata = hosts[index]->metadata();
  if (metadata == nullptr) {
    return nullptr;
  }
  const auto& filter_metadata = metadata->filter_metadata();
  absl::string_view filter_name_view(filter_name.ptr, filter_name.length);
  auto filter_it = filter_metadata.find(filter_name_view);
  if (filter_it == filter_metadata.end()) {
    return nullptr;
  }
  absl::string_view key_view(key.ptr, key.length);
  auto field_it = filter_it->second.fields().find(key_view);
  if (field_it == filter_it->second.fields().end()) {
    return nullptr;
  }
  return &field_it->second;
}

Envoy::Stats::StatNameTagVector buildTagsForClusterMetric(
    Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig& config,
    const Envoy::Stats::StatNameVec& label_names,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length) {
  ASSERT(label_values_length == label_names.size());
  Envoy::Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = config.stat_name_pool_.add(label_value_view);
    tags.push_back(Envoy::Stats::StatNameTag(label_names[i], label_value));
  }
  return tags;
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_cluster_add_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr, uint32_t priority,
    const envoy_dynamic_module_type_module_buffer* addresses, const uint32_t* weights,
    const envoy_dynamic_module_type_module_buffer* regions,
    const envoy_dynamic_module_type_module_buffer* zones,
    const envoy_dynamic_module_type_module_buffer* sub_zones,
    const envoy_dynamic_module_type_module_buffer* metadata_pairs, size_t metadata_pairs_per_host,
    size_t count, envoy_dynamic_module_type_cluster_host_envoy_ptr* result_host_ptrs) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::vector<std::string> address_strings;
  address_strings.reserve(count);
  std::vector<uint32_t> weight_vec(weights, weights + count);
  std::vector<std::string> region_strings;
  region_strings.reserve(count);
  std::vector<std::string> zone_strings;
  zone_strings.reserve(count);
  std::vector<std::string> sub_zone_strings;
  sub_zone_strings.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    address_strings.emplace_back(addresses[i].ptr, addresses[i].length);
    region_strings.emplace_back(regions[i].ptr, regions[i].length);
    zone_strings.emplace_back(zones[i].ptr, zones[i].length);
    sub_zone_strings.emplace_back(sub_zones[i].ptr, sub_zones[i].length);
  }

  // Parse metadata triples: each host has metadata_pairs_per_host triples of
  // (filter_name, key, value), laid out contiguously.
  std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> metadata_vec;
  if (metadata_pairs != nullptr && metadata_pairs_per_host > 0) {
    metadata_vec.resize(count);
    for (size_t i = 0; i < count; ++i) {
      metadata_vec[i].reserve(metadata_pairs_per_host);
      for (size_t j = 0; j < metadata_pairs_per_host; ++j) {
        size_t base = (i * metadata_pairs_per_host + j) * 3;
        std::string filter_name(metadata_pairs[base].ptr, metadata_pairs[base].length);
        std::string key(metadata_pairs[base + 1].ptr, metadata_pairs[base + 1].length);
        std::string value(metadata_pairs[base + 2].ptr, metadata_pairs[base + 2].length);
        metadata_vec[i].emplace_back(std::move(filter_name), std::move(key), std::move(value));
      }
    }
  }

  std::vector<Envoy::Upstream::HostSharedPtr> result_hosts;
  if (!cluster->addHosts(address_strings, weight_vec, region_strings, zone_strings,
                         sub_zone_strings, metadata_vec, result_hosts, priority)) {
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

bool envoy_dynamic_module_callback_cluster_update_host_health(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr host_envoy_ptr,
    envoy_dynamic_module_type_host_health health_status) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  auto host = cluster->findHost(host_envoy_ptr);
  return cluster->updateHostHealth(std::move(host), health_status);
}

envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_find_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::string address_str(address.ptr, address.length);
  auto host = cluster->findHostByAddress(address_str);
  if (host == nullptr) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(host.get());
}

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  getCluster(cluster_envoy_ptr)->preInitComplete();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->healthyHosts().size();
}

envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  if (lb_envoy_ptr == nullptr) {
    return nullptr;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return nullptr;
  }
  const auto& healthy_hosts = host_sets[priority]->healthyHosts();
  if (index >= healthy_hosts.size()) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(healthy_hosts[index].get());
}

// =============================================================================
// Cluster LB Host Information Callbacks
// =============================================================================

void envoy_dynamic_module_callback_cluster_lb_get_cluster_name(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return;
  }
  const auto& name = getLb(lb_envoy_ptr)->handle()->cluster()->info()->observabilityName();
  result->ptr = name.data();
  result->length = name.size();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->hosts().size();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->degradedHosts().size();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  return getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority().size();
}

bool envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return false;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& healthy_hosts = host_sets[priority]->healthyHosts();
  if (index >= healthy_hosts.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& address_str = healthy_hosts[index]->address()->asStringView();
  result->ptr = address_str.data();
  result->length = address_str.size();
  return true;
}

uint32_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  const auto& healthy_hosts = host_sets[priority]->healthyHosts();
  if (index >= healthy_hosts.size()) {
    return 0;
  }
  return healthy_hosts[index]->weight();
}

envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_cluster_lb_get_host_health(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  if (lb_envoy_ptr == nullptr) {
    return envoy_dynamic_module_type_host_health_Unhealthy;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return envoy_dynamic_module_type_host_health_Unhealthy;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return envoy_dynamic_module_type_host_health_Unhealthy;
  }
  switch (hosts[index]->coarseHealth()) {
  case Envoy::Upstream::Host::Health::Unhealthy:
    return envoy_dynamic_module_type_host_health_Unhealthy;
  case Envoy::Upstream::Host::Health::Degraded:
    return envoy_dynamic_module_type_host_health_Degraded;
  case Envoy::Upstream::Host::Health::Healthy:
    return envoy_dynamic_module_type_host_health_Healthy;
  }
  return envoy_dynamic_module_type_host_health_Unhealthy;
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address,
    envoy_dynamic_module_type_host_health* result) {
  if (result == nullptr) {
    return false;
  }
  *result = envoy_dynamic_module_type_host_health_Unhealthy;

  if (lb_envoy_ptr == nullptr || address.ptr == nullptr) {
    return false;
  }
  const auto host_map = getLb(lb_envoy_ptr)->prioritySet().crossPriorityHostMap();
  if (host_map == nullptr) {
    return false;
  }
  std::string address_str(address.ptr, address.length);
  const auto it = host_map->find(address_str);
  if (it == host_map->end()) {
    return false;
  }
  switch (it->second->coarseHealth()) {
  case Envoy::Upstream::Host::Health::Unhealthy:
    *result = envoy_dynamic_module_type_host_health_Unhealthy;
    break;
  case Envoy::Upstream::Host::Health::Degraded:
    *result = envoy_dynamic_module_type_host_health_Degraded;
    break;
  case Envoy::Upstream::Host::Health::Healthy:
    *result = envoy_dynamic_module_type_host_health_Healthy;
    break;
  }
  return true;
}

envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_find_host_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address) {
  if (lb_envoy_ptr == nullptr || address.ptr == nullptr) {
    return nullptr;
  }
  const auto host_map = getLb(lb_envoy_ptr)->prioritySet().crossPriorityHostMap();
  if (host_map == nullptr) {
    return nullptr;
  }
  std::string address_str(address.ptr, address.length);
  const auto it = host_map->find(address_str);
  if (it == host_map->end()) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(it->second.get());
}

envoy_dynamic_module_type_cluster_host_envoy_ptr envoy_dynamic_module_callback_cluster_lb_get_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  if (lb_envoy_ptr == nullptr) {
    return nullptr;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return nullptr;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(hosts[index].get());
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return false;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& address_str = hosts[index]->address()->asStringView();
  result->ptr = address_str.data();
  result->length = address_str.size();
  return true;
}

uint32_t envoy_dynamic_module_callback_cluster_lb_get_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return 0;
  }
  return hosts[index]->weight();
}

uint64_t envoy_dynamic_module_callback_cluster_lb_get_host_stat(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_host_stat stat) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return 0;
  }
  const auto& host_stats = hosts[index]->stats();
  switch (stat) {
  case envoy_dynamic_module_type_host_stat_CxConnectFail:
    return host_stats.cx_connect_fail_.value();
  case envoy_dynamic_module_type_host_stat_CxTotal:
    return host_stats.cx_total_.value();
  case envoy_dynamic_module_type_host_stat_RqError:
    return host_stats.rq_error_.value();
  case envoy_dynamic_module_type_host_stat_RqSuccess:
    return host_stats.rq_success_.value();
  case envoy_dynamic_module_type_host_stat_RqTimeout:
    return host_stats.rq_timeout_.value();
  case envoy_dynamic_module_type_host_stat_RqTotal:
    return host_stats.rq_total_.value();
  case envoy_dynamic_module_type_host_stat_CxActive:
    return host_stats.cx_active_.value();
  case envoy_dynamic_module_type_host_stat_RqActive:
    return host_stats.rq_active_.value();
  }
  return 0;
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_locality(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* region, envoy_dynamic_module_type_envoy_buffer* zone,
    envoy_dynamic_module_type_envoy_buffer* sub_zone) {
  if (lb_envoy_ptr == nullptr) {
    return false;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return false;
  }
  const auto& locality = hosts[index]->locality();
  if (region != nullptr) {
    region->ptr = locality.region().data();
    region->length = locality.region().size();
  }
  if (zone != nullptr) {
    zone->ptr = locality.zone().data();
    zone->length = locality.zone().size();
  }
  if (sub_zone != nullptr) {
    sub_zone->ptr = locality.sub_zone().data();
    sub_zone->length = locality.sub_zone().size();
  }
  return true;
}

bool envoy_dynamic_module_callback_cluster_lb_set_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t data) {
  if (lb_envoy_ptr == nullptr) {
    return false;
  }
  return getLb(lb_envoy_ptr)->setHostData(priority, index, data);
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t* data) {
  if (lb_envoy_ptr == nullptr || data == nullptr) {
    if (data != nullptr) {
      *data = 0;
    }
    return false;
  }
  return getLb(lb_envoy_ptr)->getHostData(priority, index, data);
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return false;
  }
  const auto* value = getClusterHostMetadataValue(lb_envoy_ptr, priority, index, filter_name, key);
  if (value == nullptr || !value->has_string_value()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& str = value->string_value();
  result->ptr = str.data();
  result->length = str.size();
  return true;
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, double* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    return false;
  }
  const auto* value = getClusterHostMetadataValue(lb_envoy_ptr, priority, index, filter_name, key);
  if (value == nullptr || !value->has_number_value()) {
    return false;
  }
  *result = value->number_value();
  return true;
}

bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, bool* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    return false;
  }
  const auto* value = getClusterHostMetadataValue(lb_envoy_ptr, priority, index, filter_name, key);
  if (value == nullptr || !value->has_bool_value()) {
    return false;
  }
  *result = value->bool_value();
  return true;
}

size_t envoy_dynamic_module_callback_cluster_lb_get_locality_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->healthyHostsPerLocality().get().size();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  const auto& localities = host_sets[priority]->healthyHostsPerLocality().get();
  if (locality_index >= localities.size()) {
    return 0;
  }
  return localities[locality_index].size();
}

bool envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index, size_t host_index, envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return false;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& localities = host_sets[priority]->healthyHostsPerLocality().get();
  if (locality_index >= localities.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& hosts = localities[locality_index];
  if (host_index >= hosts.size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& address_str = hosts[host_index]->address()->asStringView();
  result->ptr = address_str.data();
  result->length = address_str.size();
  return true;
}

uint32_t envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  const auto weights = host_sets[priority]->localityWeights();
  if (weights == nullptr || locality_index >= weights->size()) {
    return 0;
  }
  return (*weights)[locality_index];
}

bool envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr, uint64_t* hash_out) {
  if (context_envoy_ptr == nullptr || hash_out == nullptr) {
    return false;
  }
  auto hash = getContext(context_envoy_ptr)->computeHashKey();
  if (hash.has_value()) {
    *hash_out = hash.value();
    return true;
  }
  return false;
}

size_t envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr) {
  if (context_envoy_ptr == nullptr) {
    return 0;
  }
  const auto* headers = getContext(context_envoy_ptr)->downstreamHeaders();
  if (headers == nullptr) {
    return 0;
  }
  return headers->size();
}

bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (context_envoy_ptr == nullptr || result_headers == nullptr) {
    return false;
  }
  const auto* headers = getContext(context_envoy_ptr)->downstreamHeaders();
  if (headers == nullptr) {
    return false;
  }
  size_t i = 0;
  headers->iterate([&i, &result_headers](
                       const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t index, size_t* optional_size) {
  if (context_envoy_ptr == nullptr || result_buffer == nullptr) {
    if (result_buffer != nullptr) {
      *result_buffer = {.ptr = nullptr, .length = 0};
    }
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  const auto* headers = getContext(context_envoy_ptr)->downstreamHeaders();
  if (headers == nullptr) {
    *result_buffer = {.ptr = nullptr, .length = 0};
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  const auto values = headers->get(Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }
  if (index >= values.size()) {
    *result_buffer = {.ptr = nullptr, .length = 0};
    return false;
  }
  const auto value = values[index]->value().getStringView();
  *result_buffer = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

uint32_t envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr) {
  if (context_envoy_ptr == nullptr) {
    return 0;
  }
  return getContext(context_envoy_ptr)->hostSelectionRetryCount();
}

bool envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr, uint32_t priority,
    size_t index) {
  if (lb_envoy_ptr == nullptr || context_envoy_ptr == nullptr) {
    return false;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->healthyHosts();
  if (index >= hosts.size()) {
    return false;
  }
  return getContext(context_envoy_ptr)->shouldSelectAnotherHost(*hosts[index]);
}

bool envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address, bool* strict) {
  if (context_envoy_ptr == nullptr || address == nullptr || strict == nullptr) {
    return false;
  }
  Envoy::OptRef<const Envoy::Upstream::LoadBalancerContext::OverrideHost> override_host =
      getContext(context_envoy_ptr)->overrideHostToSelect();
  if (!override_host.has_value()) {
    return false;
  }
  const std::string& host_address = override_host->host;
  address->ptr = const_cast<char*>(host_address.data());
  address->length = host_address.size();
  *strict = override_host->strict;
  return true;
}

bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (context_envoy_ptr == nullptr || result_buffer == nullptr) {
    return false;
  }
  const auto* connection = getContext(context_envoy_ptr)->downstreamConnection();
  if (connection == nullptr) {
    return false;
  }
  auto sni = connection->requestedServerName();
  if (sni.empty()) {
    return false;
  }
  result_buffer->ptr = const_cast<char*>(sni.data());
  result_buffer->length = sni.size();
  return true;
}

envoy_dynamic_module_type_cluster_scheduler_module_ptr
envoy_dynamic_module_callback_cluster_scheduler_new(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  return Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler::create(
      getCluster(cluster_envoy_ptr));
}

void envoy_dynamic_module_callback_cluster_scheduler_delete(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler*>(
      scheduler_module_ptr);
}

void envoy_dynamic_module_callback_cluster_scheduler_commit(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler =
      static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler*>(
          scheduler_module_ptr);
  scheduler->commit(event_id);
}

// =============================================================================
// Metrics Callbacks
// =============================================================================

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Counter& c =
        Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
    *counter_id_ptr = config->addCounter({c});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *counter_id_ptr = config->addCounterVec({main_stat_name, label_names_vec});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto counter = config->getCounterById(id);
    if (!counter.has_value()) {
      if (config->getCounterVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    counter->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto counter = config->getCounterVecById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != counter->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForClusterMetric(*config, counter->getLabelNames(), label_values,
                                        label_values_length);
  counter->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_define_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Gauge::ImportMode import_mode = Envoy::Stats::Gauge::ImportMode::Accumulate;

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Gauge& g = Envoy::Stats::Utility::gaugeFromStatNames(
        *config->stats_scope_, {main_stat_name}, import_mode);
    *gauge_id_ptr = config->addGauge({g});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = config->addGaugeVec({main_stat_name, label_names_vec, import_mode});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_set_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->set(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->set(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_decrement_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->sub(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->sub(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_histogram(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Histogram::Unit unit = Envoy::Stats::Histogram::Unit::Unspecified;

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Histogram& h = Envoy::Stats::Utility::histogramFromStatNames(
        *config->stats_scope_, {main_stat_name}, unit);
    *histogram_id_ptr = config->addHistogram({h});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *histogram_id_ptr = config->addHistogramVec({main_stat_name, label_names_vec, unit});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_record_histogram_value(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto histogram = config->getHistogramById(id);
    if (!histogram.has_value()) {
      if (config->getHistogramVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    histogram->recordValue(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto histogram = config->getHistogramVecById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != histogram->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForClusterMetric(*config, histogram->getLabelNames(), label_values,
                                        label_values_length);
  histogram->recordValue(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

void envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr host,
    envoy_dynamic_module_type_module_buffer details) {
  using Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterHandleSharedPtr;
  using Envoy::Extensions::Clusters::DynamicModules::DynamicModuleLoadBalancer;
  auto* lb_raw = static_cast<DynamicModuleLoadBalancer*>(lb_envoy_ptr);

  // Copy the details string on the calling thread. The pointer is not valid after we return.
  std::string details_str;
  if (details.ptr != nullptr && details.length > 0) {
    details_str.assign(details.ptr, details.length);
  }

  // The module may invoke this callback on any thread and race the load balancer destructor.
  // Validate the raw pointer against the live registry and snapshot the state we need under the
  // registry lock.
  std::shared_ptr<std::atomic<bool>> cancelled;
  Envoy::Event::Dispatcher* dispatcher = nullptr;
  DynamicModuleClusterHandleSharedPtr handle;
  const bool found = DynamicModuleLoadBalancer::withActiveInstance(
      lb_raw, [&](const DynamicModuleLoadBalancer& lb) {
        cancelled = lb.activeAsyncCancelled();
        dispatcher = lb.activeAsyncDispatcher();
        handle = lb.handle();
      });
  if (!found) {
    return;
  }

  if (dispatcher != nullptr) {
    // Post to the worker thread. The handle keeps the cluster alive until the callback runs.
    dispatcher->post([context_envoy_ptr, host, details_str = std::move(details_str),
                      cancelled = std::move(cancelled), handle = std::move(handle)]() {
      if (cancelled != nullptr && cancelled->load(std::memory_order_acquire)) {
        return;
      }
      auto* context = getContext(context_envoy_ptr);
      Envoy::Upstream::HostConstSharedPtr host_shared;
      if (host != nullptr) {
        host_shared = handle->cluster()->findHost(host);
      }
      context->onAsyncHostSelection(std::move(host_shared), std::string(details_str));
    });
  } else {
    // No worker dispatcher. Complete inline on the calling thread.
    auto* context = getContext(context_envoy_ptr);
    Envoy::Upstream::HostConstSharedPtr host_shared;
    if (host != nullptr) {
      host_shared = handle->cluster()->findHost(host);
    }
    context->onAsyncHostSelection(std::move(host_shared), std::move(details_str));
  }
}

// =============================================================================
// HTTP Callout Callback
// =============================================================================

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_cluster_http_callout(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto* cluster = getCluster(cluster_envoy_ptr);

  Envoy::Http::RequestHeaderMapPtr header_map = Envoy::Http::RequestHeaderMapImpl::create();
  for (size_t i = 0; i < headers_size; ++i) {
    header_map->addCopy(
        Envoy::Http::LowerCaseString(std::string(headers[i].key_ptr, headers[i].key_length)),
        std::string(headers[i].value_ptr, headers[i].value_length));
  }

  if (header_map->Path() == nullptr || header_map->Method() == nullptr ||
      header_map->Host() == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }

  Envoy::Http::RequestMessagePtr message =
      std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(header_map));

  if (body.length > 0 && body.ptr != nullptr) {
    message->body().add(absl::string_view(body.ptr, body.length));
  }

  return cluster->sendHttpCallout(callout_id_out,
                                  absl::string_view(cluster_name.ptr, cluster_name.length),
                                  std::move(message), timeout_milliseconds);
}

bool envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, size_t index, bool is_added,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return false;
  }
  const auto* hosts =
      is_added ? getLb(lb_envoy_ptr)->hostsAdded() : getLb(lb_envoy_ptr)->hostsRemoved();
  if (hosts == nullptr || index >= hosts->size()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  const auto& address_str = (*hosts)[index]->address()->asStringView();
  result->ptr = address_str.data();
  result->length = address_str.size();
  return true;
}

} // extern "C"
