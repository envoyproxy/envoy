#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

DynamicModuleLoadBalancer* getLb(envoy_dynamic_module_type_lb_envoy_ptr ptr) {
  return static_cast<DynamicModuleLoadBalancer*>(ptr);
}

Upstream::LoadBalancerContext* getContext(envoy_dynamic_module_type_lb_context_envoy_ptr ptr) {
  return static_cast<Upstream::LoadBalancerContext*>(ptr);
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy

using namespace Envoy::Extensions::LoadBalancingPolicies::DynamicModules;

extern "C" {

void envoy_dynamic_module_callback_lb_get_cluster_name(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (lb_envoy_ptr == nullptr || result == nullptr) {
    if (result != nullptr) {
      result->ptr = nullptr;
      result->length = 0;
    }
    return;
  }
  const auto& name = getLb(lb_envoy_ptr)->clusterName();
  result->ptr = name.data();
  result->length = name.size();
}

size_t envoy_dynamic_module_callback_lb_get_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->hosts().size();
}

size_t envoy_dynamic_module_callback_lb_get_healthy_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->healthyHosts().size();
}

size_t envoy_dynamic_module_callback_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  const auto& host_sets = getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return 0;
  }
  return host_sets[priority]->degradedHosts().size();
}

size_t envoy_dynamic_module_callback_lb_get_priority_set_size(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  if (lb_envoy_ptr == nullptr) {
    return 0;
  }
  return getLb(lb_envoy_ptr)->prioritySet().hostSetsPerPriority().size();
}

bool envoy_dynamic_module_callback_lb_get_healthy_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
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

uint32_t envoy_dynamic_module_callback_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
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

envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_lb_get_host_health(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
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

bool envoy_dynamic_module_callback_lb_get_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
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

uint32_t envoy_dynamic_module_callback_lb_get_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
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

uint64_t envoy_dynamic_module_callback_lb_get_host_active_requests(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
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
  return hosts[index]->stats().rq_active_.value();
}

uint64_t envoy_dynamic_module_callback_lb_get_host_active_connections(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
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
  return hosts[index]->stats().cx_active_.value();
}

bool envoy_dynamic_module_callback_lb_get_host_locality(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
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

bool envoy_dynamic_module_callback_lb_context_compute_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint64_t* hash_out) {
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

size_t envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr) {
  if (context_envoy_ptr == nullptr) {
    return 0;
  }
  const auto* headers = getContext(context_envoy_ptr)->downstreamHeaders();
  if (headers == nullptr) {
    return 0;
  }
  return headers->size();
}

bool envoy_dynamic_module_callback_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr,
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

bool envoy_dynamic_module_callback_lb_context_get_downstream_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr,
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

} // extern "C"
