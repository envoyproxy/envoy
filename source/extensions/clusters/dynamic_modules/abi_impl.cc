#include "source/common/network/utility.h"
#include "source/extensions/clusters/dynamic_modules/abi.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {
namespace {

DynamicModuleCluster* getCluster(envoy_dynamic_module_type_cluster_envoy_ptr ptr) {
  return static_cast<DynamicModuleCluster*>(ptr);
}

Upstream::Host* getHost(envoy_dynamic_module_type_host_envoy_ptr ptr) {
  return static_cast<Upstream::Host*>(ptr);
}

Upstream::LoadBalancerContext* getContext(envoy_dynamic_module_type_lb_context_envoy_ptr ptr) {
  return static_cast<Upstream::LoadBalancerContext*>(ptr);
}

Upstream::ClusterManager*
getClusterManager(envoy_dynamic_module_type_cluster_manager_envoy_ptr ptr) {
  return static_cast<Upstream::ClusterManager*>(ptr);
}

Upstream::ThreadLocalCluster*
getThreadLocalCluster(envoy_dynamic_module_type_thread_local_cluster_envoy_ptr ptr) {
  return static_cast<Upstream::ThreadLocalCluster*>(ptr);
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

using namespace Envoy::Extensions::Clusters::DynamicModules;

size_t envoy_dynamic_module_callback_cluster_get_name(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (cluster_envoy_ptr == nullptr || result_buffer == nullptr) {
    return 0;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  const auto& name = cluster->info()->name();
  result_buffer->ptr = name.data();
  result_buffer->length = name.size();
  return name.size();
}

envoy_dynamic_module_type_cluster_error envoy_dynamic_module_callback_cluster_add_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer address, uint32_t weight,
    envoy_dynamic_module_type_host_envoy_ptr* result_host) {
  if (cluster_envoy_ptr == nullptr) {
    return envoy_dynamic_module_type_cluster_error_InvalidAddress;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::string addr_str(address.ptr, address.length);
  Envoy::Upstream::HostSharedPtr host;
  auto result = cluster->addHost(addr_str, weight, &host);
  if (result == envoy_dynamic_module_type_cluster_error_Ok && result_host != nullptr &&
      host != nullptr) {
    *result_host = const_cast<Envoy::Upstream::Host*>(host.get());
  }
  return result;
}

envoy_dynamic_module_type_cluster_error envoy_dynamic_module_callback_cluster_remove_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host) {
  if (cluster_envoy_ptr == nullptr || host == nullptr) {
    return envoy_dynamic_module_type_cluster_error_HostNotFound;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  auto* host_ptr = getHost(host);

  // Find the shared_ptr for this host.
  auto hosts = cluster->getHosts();
  for (const auto& info : hosts) {
    if (info.host == host_ptr) {
      // We need to get the shared_ptr from the priority set.
      const auto& priority_set = cluster->prioritySet();
      for (const auto& host_set : priority_set.hostSetsPerPriority()) {
        for (const auto& h : host_set->hosts()) {
          if (h.get() == host_ptr) {
            return cluster->removeHost(h);
          }
        }
      }
    }
  }
  return envoy_dynamic_module_type_cluster_error_HostNotFound;
}

size_t envoy_dynamic_module_callback_cluster_get_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_info* hosts, size_t max_hosts) {
  if (cluster_envoy_ptr == nullptr) {
    return 0;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  auto host_infos = cluster->getHosts();

  if (hosts == nullptr) {
    return host_infos.size();
  }

  size_t count = std::min(host_infos.size(), max_hosts);
  for (size_t i = 0; i < count; i++) {
    hosts[i] = host_infos[i];
  }
  return count;
}

envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_callback_cluster_get_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer address) {
  if (cluster_envoy_ptr == nullptr) {
    return nullptr;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::string addr_str(address.ptr, address.length);
  auto host = cluster->getHostByAddress(addr_str);
  if (host == nullptr) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(host.get());
}

envoy_dynamic_module_type_cluster_error envoy_dynamic_module_callback_cluster_host_set_weight(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host, uint32_t weight) {
  if (cluster_envoy_ptr == nullptr || host == nullptr) {
    return envoy_dynamic_module_type_cluster_error_HostNotFound;
  }
  auto* cluster = getCluster(cluster_envoy_ptr);
  auto* host_ptr = getHost(host);

  // Find the shared_ptr for this host.
  const auto& priority_set = cluster->prioritySet();
  for (const auto& host_set : priority_set.hostSetsPerPriority()) {
    for (const auto& h : host_set->hosts()) {
      if (h.get() == host_ptr) {
        return cluster->setHostWeight(h, weight);
      }
    }
  }
  return envoy_dynamic_module_type_cluster_error_HostNotFound;
}

size_t envoy_dynamic_module_callback_cluster_host_get_address(
    envoy_dynamic_module_type_host_envoy_ptr host,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (host == nullptr || result_buffer == nullptr) {
    return 0;
  }
  auto* host_ptr = getHost(host);
  const auto& address = host_ptr->address()->asString();
  result_buffer->ptr = address.data();
  result_buffer->length = address.size();
  return address.size();
}

envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_cluster_host_get_health(
    envoy_dynamic_module_type_host_envoy_ptr host) {
  if (host == nullptr) {
    return envoy_dynamic_module_type_host_health_Unknown;
  }
  auto* host_ptr = getHost(host);
  switch (host_ptr->coarseHealth()) {
  case Envoy::Upstream::Host::Health::Healthy:
    return envoy_dynamic_module_type_host_health_Healthy;
  case Envoy::Upstream::Host::Health::Unhealthy:
    return envoy_dynamic_module_type_host_health_Unhealthy;
  case Envoy::Upstream::Host::Health::Degraded:
    return envoy_dynamic_module_type_host_health_Degraded;
  }
  return envoy_dynamic_module_type_host_health_Unknown; // LCOV_EXCL_LINE
}

uint32_t envoy_dynamic_module_callback_cluster_host_get_weight(
    envoy_dynamic_module_type_host_envoy_ptr host) {
  if (host == nullptr) {
    return 0;
  }
  return getHost(host)->weight();
}

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  if (cluster_envoy_ptr == nullptr) {
    return;
  }
  getCluster(cluster_envoy_ptr)->preInitComplete();
}

bool envoy_dynamic_module_callback_lb_context_get_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint64_t* result) {
  if (context == nullptr || result == nullptr) {
    return false;
  }
  auto* ctx = getContext(context);
  auto hash_key = ctx->computeHashKey();
  if (!hash_key.has_value()) {
    return false;
  }
  *result = hash_key.value();
  return true;
}

size_t envoy_dynamic_module_callback_lb_context_get_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_envoy_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (context == nullptr || result_buffer == nullptr) {
    return 0;
  }
  auto* ctx = getContext(context);
  const auto* headers = ctx->downstreamHeaders();
  if (headers == nullptr) {
    return 0;
  }
  auto key_view = absl::string_view(key.ptr, key.length);
  auto result = headers->get(Envoy::Http::LowerCaseString(std::string(key_view)));
  if (result.empty()) {
    return 0;
  }
  const auto& value = result[0]->value().getStringView();
  result_buffer->ptr = value.data();
  result_buffer->length = value.size();
  return value.size();
}

size_t envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (context == nullptr || result_buffer == nullptr) {
    return 0;
  }
  auto* ctx = getContext(context);
  const auto& override_host = ctx->overrideHostToSelect();
  if (!override_host.has_value() || override_host->first.empty()) {
    return 0;
  }
  result_buffer->ptr = override_host->first.data();
  result_buffer->length = override_host->first.size();
  return override_host->first.size();
}

uint32_t envoy_dynamic_module_callback_lb_context_get_attempt_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  if (context == nullptr) {
    return 0;
  }
  return getContext(context)->hostSelectionRetryCount();
}

uint64_t envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  if (context == nullptr) {
    return 0;
  }
  auto* ctx = getContext(context);
  const auto* connection = ctx->downstreamConnection();
  if (connection == nullptr) {
    return 0;
  }
  return connection->id();
}

envoy_dynamic_module_type_thread_local_cluster_envoy_ptr
envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
    envoy_dynamic_module_type_cluster_manager_envoy_ptr cluster_manager_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer cluster_name) {
  if (cluster_manager_envoy_ptr == nullptr) {
    return nullptr;
  }
  auto* cm = getClusterManager(cluster_manager_envoy_ptr);
  std::string name(cluster_name.ptr, cluster_name.length);
  auto* tlc = cm->getThreadLocalCluster(name);
  return tlc;
}

envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_thread_local_cluster_choose_host(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_lb_context_envoy_ptr context) {
  if (thread_local_cluster == nullptr) {
    return nullptr;
  }
  auto* tlc = getThreadLocalCluster(thread_local_cluster);
  auto response = tlc->loadBalancer().chooseHost(getContext(context));
  if (response.host == nullptr) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(response.host.get());
}

size_t envoy_dynamic_module_callback_thread_local_cluster_get_name(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (thread_local_cluster == nullptr || result_buffer == nullptr) {
    return 0;
  }
  auto* tlc = getThreadLocalCluster(thread_local_cluster);
  const auto& name = tlc->info()->name();
  result_buffer->ptr = name.data();
  result_buffer->length = name.size();
  return name.size();
}

size_t envoy_dynamic_module_callback_thread_local_cluster_host_count(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster) {
  if (thread_local_cluster == nullptr) {
    return 0;
  }
  auto* tlc = getThreadLocalCluster(thread_local_cluster);
  size_t count = 0;
  for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
    count += host_set->hosts().size();
  }
  return count;
}
