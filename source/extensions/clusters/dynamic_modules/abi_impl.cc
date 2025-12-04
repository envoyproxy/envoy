#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "source/common/network/utility.h"
#include "source/extensions/dynamic_modules/abi.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

extern "C" {

// =============================================================================
// Cluster Callback Implementations
// =============================================================================

size_t envoy_dynamic_module_callback_cluster_get_name(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* name_out) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);
  const std::string& name = cluster->info()->name();
  *name_out = const_cast<char*>(name.data());
  return name.size();
}

envoy_dynamic_module_type_cluster_result envoy_dynamic_module_callback_cluster_add_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr address, size_t address_length, uint32_t port,
    uint32_t weight, envoy_dynamic_module_type_host_envoy_ptr* host_out) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);

  const std::string address_str(address, address_length);
  auto result = cluster->addHost(address_str, port, weight);

  if (!result.ok()) {
    if (result.status().code() == absl::StatusCode::kInvalidArgument) {
      return envoy_dynamic_module_type_cluster_result_InvalidAddress;
    } else if (result.status().code() == absl::StatusCode::kResourceExhausted) {
      return envoy_dynamic_module_type_cluster_result_MaxHostsReached;
    }
    return envoy_dynamic_module_type_cluster_result_Error;
  }

  *host_out = result.value().get();
  return envoy_dynamic_module_type_cluster_result_Success;
}

envoy_dynamic_module_type_cluster_result envoy_dynamic_module_callback_cluster_remove_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);
  auto* host_ptr = static_cast<Upstream::Host*>(host);

  // Create a shared_ptr that doesn't own the host (cluster owns it).
  Upstream::HostSharedPtr host_shared(host_ptr, [](Upstream::Host*) {});
  cluster->removeHost(host_shared);

  return envoy_dynamic_module_type_cluster_result_Success;
}

void envoy_dynamic_module_callback_cluster_get_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_info** hosts_out, size_t* hosts_count_out) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);

  auto hosts = cluster->getHosts();
  *hosts_count_out = hosts.size();

  if (hosts.empty()) {
    *hosts_out = nullptr;
    return;
  }

  // Allocate memory for the host info array.
  // The module is responsible for freeing this memory.
  auto* host_info_array =
      static_cast<envoy_dynamic_module_type_host_info*>(malloc(hosts.size() * sizeof(envoy_dynamic_module_type_host_info)));

  for (size_t i = 0; i < hosts.size(); ++i) {
    host_info_array[i] = hosts[i].second;
  }

  *hosts_out = host_info_array;
}

envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_cluster_get_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr address, size_t address_length, uint32_t port) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);

  const std::string address_str(address, address_length);
  auto host = cluster->getHostByAddress(address_str, port);

  if (host == nullptr) {
    return nullptr;
  }

  return const_cast<Upstream::Host*>(host.get());
}

void envoy_dynamic_module_callback_host_set_weight(envoy_dynamic_module_type_host_envoy_ptr host,
                                                    uint32_t weight) {
  auto* host_ptr = static_cast<Upstream::Host*>(host);
  host_ptr->weight(weight);
}

size_t envoy_dynamic_module_callback_host_get_address(
    envoy_dynamic_module_type_host_envoy_ptr host,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out) {
  auto* host_ptr = static_cast<Upstream::Host*>(host);

  const std::string& address = host_ptr->address()->ip()->addressAsString();
  *address_out = const_cast<char*>(address.data());
  *port_out = host_ptr->address()->ip()->port();

  return address.size();
}

envoy_dynamic_module_type_host_health
envoy_dynamic_module_callback_host_get_health(envoy_dynamic_module_type_host_envoy_ptr host) {
  auto* host_ptr = static_cast<Upstream::Host*>(host);

  switch (host_ptr->coarseHealth()) {
  case Upstream::Host::Health::Healthy:
    return envoy_dynamic_module_type_host_health_Healthy;
  case Upstream::Host::Health::Degraded:
    return envoy_dynamic_module_type_host_health_Degraded;
  case Upstream::Host::Health::Unhealthy:
    return envoy_dynamic_module_type_host_health_Unhealthy;
  }

  return envoy_dynamic_module_type_host_health_Unhealthy;
}

uint32_t
envoy_dynamic_module_callback_host_get_weight(envoy_dynamic_module_type_host_envoy_ptr host) {
  auto* host_ptr = static_cast<Upstream::Host*>(host);
  return host_ptr->weight();
}

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);
  cluster->onPreInitComplete();
}

bool envoy_dynamic_module_callback_lb_context_get_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint64_t* hash_out) {
  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);

  if (lb_context == nullptr) {
    return false;
  }

  auto hash_key = lb_context->computeHashKey();
  if (!hash_key.has_value()) {
    return false;
  }

  *hash_out = hash_key.value();
  return true;
}

size_t envoy_dynamic_module_callback_lb_context_get_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* value_out) {
  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);

  if (lb_context == nullptr) {
    return 0;
  }

  const Http::RequestHeaderMap* headers = lb_context->downstreamHeaders();
  if (headers == nullptr) {
    return 0;
  }

  const Http::LowerCaseString key_str(std::string(key, key_length));
  const auto entry = headers->get(key_str);

  if (entry.empty()) {
    return 0;
  }

  const absl::string_view value = entry[0]->value().getStringView();
  *value_out = const_cast<char*>(value.data());
  return value.size();
}

size_t envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, bool* strict_out) {
  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);

  if (lb_context == nullptr) {
    return 0;
  }

  const auto override_host = lb_context->overrideHostToSelect();
  if (!override_host.has_value()) {
    return 0;
  }

  const auto& [address, strict] = override_host.value();
  *address_out = const_cast<char*>(address.data());
  *strict_out = strict;

  return address.size();
}

bool envoy_dynamic_module_callback_lb_context_get_attempt_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint32_t* attempt_out) {
  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);

  if (lb_context == nullptr) {
    return false;
  }

  StreamInfo::StreamInfo* stream_info = lb_context->requestStreamInfo();
  if (stream_info == nullptr) {
    return false;
  }

  auto attempt_count = stream_info->attemptCount();
  if (!attempt_count.has_value()) {
    return false;
  }

  *attempt_out = attempt_count.value();
  return true;
}

bool envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint64_t* connection_id_out) {
  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);

  if (lb_context == nullptr) {
    return false;
  }

  const Network::Connection* connection = lb_context->downstreamConnection();
  if (connection == nullptr) {
    return false;
  }

  *connection_id_out = connection->id();
  return true;
}

// =============================================================================
// Cluster Manager Callback Implementations
// =============================================================================

envoy_dynamic_module_type_thread_local_cluster_envoy_ptr
envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr cluster_name, size_t cluster_name_length) {
  auto* cluster = static_cast<DynamicModuleCluster*>(cluster_envoy_ptr);

  const std::string name(cluster_name, cluster_name_length);
  Upstream::ThreadLocalCluster* tl_cluster = cluster->clusterManager().getThreadLocalCluster(name);

  return static_cast<envoy_dynamic_module_type_thread_local_cluster_envoy_ptr>(tl_cluster);
}

envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_thread_local_cluster_choose_host(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  auto* tl_cluster = const_cast<Upstream::ThreadLocalCluster*>(
      static_cast<const Upstream::ThreadLocalCluster*>(thread_local_cluster));

  if (tl_cluster == nullptr) {
    return nullptr;
  }

  auto* lb_context = static_cast<Upstream::LoadBalancerContext*>(context);
  auto response = tl_cluster->chooseHost(lb_context);

  if (response.host == nullptr) {
    return nullptr;
  }

  // Return the raw pointer. The host is owned by the cluster.
  return const_cast<Upstream::Host*>(response.host.get());
}

size_t envoy_dynamic_module_callback_thread_local_cluster_get_name(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_buffer_envoy_ptr* name_out) {
  auto* tl_cluster = const_cast<Upstream::ThreadLocalCluster*>(
      static_cast<const Upstream::ThreadLocalCluster*>(thread_local_cluster));

  if (tl_cluster == nullptr) {
    return 0;
  }

  const std::string& name = tl_cluster->info()->name();
  *name_out = const_cast<char*>(name.data());
  return name.size();
}

size_t envoy_dynamic_module_callback_thread_local_cluster_host_count(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster) {
  auto* tl_cluster = const_cast<Upstream::ThreadLocalCluster*>(
      static_cast<const Upstream::ThreadLocalCluster*>(thread_local_cluster));

  if (tl_cluster == nullptr) {
    return 0;
  }

  size_t count = 0;
  for (const auto& host_set : tl_cluster->prioritySet().hostSetsPerPriority()) {
    count += host_set->hosts().size();
  }
  return count;
}

} // extern "C"

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

