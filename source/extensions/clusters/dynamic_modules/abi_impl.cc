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

Envoy::Upstream::LoadBalancerContext*
getContext(envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr) {
  return static_cast<Envoy::Upstream::LoadBalancerContext*>(context_envoy_ptr);
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
  auto override_host = getContext(context_envoy_ptr)->overrideHostToSelect();
  if (!override_host.has_value()) {
    return false;
  }
  auto host_address = override_host.value().first;
  address->ptr = const_cast<char*>(host_address.data());
  address->length = host_address.size();
  *strict = override_host.value().second;
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

} // extern "C"
