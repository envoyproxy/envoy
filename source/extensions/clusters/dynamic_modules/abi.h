#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header, so we can't apply clang-tidy to it.
// NOLINTBEGIN

// This is a pure C header file that defines the ABI for cluster dynamic modules in Envoy.
//
// This header extends the base dynamic modules ABI with cluster-specific types, event hooks,
// and callbacks. It must be included after the base abi.h header.
//
// Like the base ABI, compatibility is guaranteed by an exact version match. Envoy checks the
// hash of this header file separately from the base ABI to ensure cluster modules are built
// against the same version.

#include "source/extensions/dynamic_modules/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// ----------------------- Cluster Dynamic Module Types -----------------------
// =============================================================================

/**
 * envoy_dynamic_module_type_cluster_config_envoy_ptr is a raw pointer to the
 * DynamicModuleClusterConfig class in Envoy. This is passed to the module when creating
 * a new in-module cluster configuration.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_cluster_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_config_module_ptr is a pointer to an in-module cluster
 * configuration corresponding to an Envoy cluster configuration. The config is responsible for
 * creating new cluster instances.
 *
 * This has 1:1 correspondence with the DynamicModuleClusterConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_cluster_config_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_cluster_config_module_ptr;

/**
 * envoy_dynamic_module_type_cluster_envoy_ptr is a raw pointer to the DynamicModuleCluster
 * class in Envoy. This is passed to the module when creating a new cluster instance and used
 * to access cluster-scoped operations such as adding/removing hosts.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the cluster is
 * destroyed, i.e. envoy_dynamic_module_on_cluster_destroy is called.
 */
typedef void* envoy_dynamic_module_type_cluster_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_module_ptr is a pointer to an in-module cluster instance
 * corresponding to an Envoy cluster. The cluster is responsible for managing hosts and
 * creating load balancers.
 *
 * This has 1:1 correspondence with the DynamicModuleCluster class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_cluster_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_cluster_module_ptr;

/**
 * envoy_dynamic_module_type_load_balancer_envoy_ptr is a raw pointer to the
 * DynamicModuleLoadBalancer class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_load_balancer_envoy_ptr;

/**
 * envoy_dynamic_module_type_load_balancer_module_ptr is a pointer to an in-module load balancer
 * instance corresponding to an Envoy load balancer. The load balancer is responsible for
 * selecting hosts for requests.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_load_balancer_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_load_balancer_module_ptr;

/**
 * envoy_dynamic_module_type_host_envoy_ptr is a raw pointer to a Host in Envoy.
 * Hosts represent upstream endpoints in a cluster.
 *
 * OWNERSHIP: Envoy owns the pointer. The lifetime depends on the cluster managing the host.
 */
typedef void* envoy_dynamic_module_type_host_envoy_ptr;

/**
 * envoy_dynamic_module_type_lb_context_envoy_ptr is a pointer to LoadBalancerContext.
 * This context provides information about the request for load balancing decisions.
 *
 * OWNERSHIP: Envoy owns the pointer. Valid only during the choose_host call.
 */
typedef void* envoy_dynamic_module_type_lb_context_envoy_ptr;

/**
 * envoy_dynamic_module_type_host_health represents the health status of a host.
 */
typedef enum envoy_dynamic_module_type_host_health {
  envoy_dynamic_module_type_host_health_Healthy,
  envoy_dynamic_module_type_host_health_Degraded,
  envoy_dynamic_module_type_host_health_Unhealthy,
} envoy_dynamic_module_type_host_health;

/**
 * envoy_dynamic_module_type_cluster_result represents the result of cluster operations.
 */
typedef enum envoy_dynamic_module_type_cluster_result {
  envoy_dynamic_module_type_cluster_result_Success,
  envoy_dynamic_module_type_cluster_result_InvalidAddress,
  envoy_dynamic_module_type_cluster_result_MaxHostsReached,
  envoy_dynamic_module_type_cluster_result_HostNotFound,
  envoy_dynamic_module_type_cluster_result_Error,
} envoy_dynamic_module_type_cluster_result;

/**
 * envoy_dynamic_module_type_host_info represents information about a host.
 * This struct is used for passing host data to/from modules.
 */
typedef struct envoy_dynamic_module_type_host_info {
  envoy_dynamic_module_type_buffer_envoy_ptr address;
  size_t address_length;
  uint32_t port;
  uint32_t weight;
  envoy_dynamic_module_type_host_health health;
} envoy_dynamic_module_type_host_info;

/**
 * Health transition types for host health changes.
 */
typedef enum {
  envoy_dynamic_module_type_health_transition_Unchanged = 0,
  envoy_dynamic_module_type_health_transition_Changed = 1,
  envoy_dynamic_module_type_health_transition_ChangePending = 2,
} envoy_dynamic_module_type_health_transition;

/**
 * Opaque pointer to an external ThreadLocalCluster in Envoy.
 * This represents another cluster that can be used for cross-cluster host selection.
 */
typedef const void* envoy_dynamic_module_type_thread_local_cluster_envoy_ptr;

// =============================================================================
// ----------------------- Cluster Event Hooks ---------------------------------
// =============================================================================

/**
 * envoy_dynamic_module_on_cluster_program_init is called by the main thread when a cluster
 * module is loaded. The function returns the cluster ABI version of the dynamic module.
 * If null is returned, the module will be considered incompatible.
 *
 * This is separate from envoy_dynamic_module_on_program_init which checks the base ABI version.
 * Cluster modules must implement both functions.
 *
 * @return the cluster ABI version string, or null on error.
 */
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_cluster_program_init(void);

/**
 * envoy_dynamic_module_on_cluster_config_new is called when a new cluster configuration is created.
 * The module should allocate and return its own cluster configuration object.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleClusterConfig object in Envoy.
 * @param name is the cluster name from the configuration.
 * @param name_length is the length of the cluster name.
 * @param config is the configuration data passed from the protobuf.
 * @param config_length is the length of the configuration data.
 * @return the module's cluster config pointer, or nullptr on failure.
 */
envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_buffer_module_ptr config, size_t config_length);

/**
 * envoy_dynamic_module_on_cluster_config_destroy is called when a cluster configuration is
 * destroyed. The module should deallocate its cluster configuration object.
 *
 * @param config_module_ptr is the pointer to the in-module cluster configuration.
 */
void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_new is called to create a new cluster instance.
 * The module should allocate and return its own cluster object.
 *
 * @param config_module_ptr is the pointer to the in-module cluster configuration.
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @return the module's cluster pointer, or nullptr on failure.
 */
envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr);

/**
 * envoy_dynamic_module_on_cluster_destroy is called when a cluster instance is destroyed.
 * The module should deallocate its cluster object.
 *
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_init is called during cluster initialization (startPreInit).
 * The module should perform any necessary initialization and may add initial hosts.
 * The module must call envoy_dynamic_module_callback_cluster_pre_init_complete when initialization
 * is complete.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_cleanup is called periodically for cleanup.
 * The module may remove stale hosts or perform other maintenance tasks.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_cleanup(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_load_balancer_new is called to create a new load balancer instance
 * for a worker thread. Each worker thread gets its own load balancer instance.
 *
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param lb_envoy_ptr is the pointer to the DynamicModuleLoadBalancer object in Envoy.
 * @return the module's load balancer pointer, or nullptr on failure.
 */
envoy_dynamic_module_type_load_balancer_module_ptr envoy_dynamic_module_on_load_balancer_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_load_balancer_envoy_ptr lb_envoy_ptr);

/**
 * envoy_dynamic_module_on_load_balancer_destroy is called when a load balancer instance is
 * destroyed. The module should deallocate its load balancer object.
 *
 * @param lb_module_ptr is the pointer to the in-module load balancer.
 */
void envoy_dynamic_module_on_load_balancer_destroy(
    envoy_dynamic_module_type_load_balancer_module_ptr lb_module_ptr);

/**
 * envoy_dynamic_module_on_load_balancer_choose_host is called to choose a host for a request.
 * This is the core load balancing function called by Envoy for each request.
 *
 * @param lb_envoy_ptr is the pointer to the DynamicModuleLoadBalancer object in Envoy.
 * @param lb_module_ptr is the pointer to the in-module load balancer.
 * @param context is the LoadBalancerContext pointer with request information.
 * @return the selected host pointer, or nullptr if no host is available.
 */
envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_on_load_balancer_choose_host(
    envoy_dynamic_module_type_load_balancer_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_load_balancer_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context);

/**
 * envoy_dynamic_module_on_host_set_change is called when the set of hosts in the cluster changes.
 * This is invoked after hosts are added or removed from the priority set.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param hosts_added is an array of host pointers that were added.
 * @param hosts_added_count is the number of hosts added.
 * @param hosts_removed is an array of host pointers that were removed.
 * @param hosts_removed_count is the number of hosts removed.
 */
void envoy_dynamic_module_on_host_set_change(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_host_envoy_ptr* hosts_added, size_t hosts_added_count,
    envoy_dynamic_module_type_host_envoy_ptr* hosts_removed, size_t hosts_removed_count);

/**
 * envoy_dynamic_module_on_host_health_change is called when a host's health check status changes.
 * This allows the module to react to health check results.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param host is the host whose health changed.
 * @param transition indicates the type of health transition.
 * @param current_health is the current health status after the check.
 */
void envoy_dynamic_module_on_host_health_change(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host,
    envoy_dynamic_module_type_health_transition transition,
    envoy_dynamic_module_type_host_health current_health);

// =============================================================================
// ----------------------- Cluster Callbacks -----------------------------------
// =============================================================================

/**
 * envoy_dynamic_module_callback_cluster_get_name retrieves the cluster name.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param name_out is set to point to the cluster name buffer.
 * @return the length of the cluster name.
 */
size_t envoy_dynamic_module_callback_cluster_get_name(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr* name_out);

/**
 * envoy_dynamic_module_callback_cluster_add_host adds a new host to the cluster dynamically.
 * This can be called from the init or cleanup callbacks, or from load balancer choose_host
 * for dynamic discovery patterns.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param address is the IP address string.
 * @param address_length is the length of the address string.
 * @param port is the port number.
 * @param weight is the load balancing weight (1-128).
 * @param host_out is set to point to the created host.
 * @return the result status.
 */
envoy_dynamic_module_type_cluster_result envoy_dynamic_module_callback_cluster_add_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr address, size_t address_length, uint32_t port,
    uint32_t weight, envoy_dynamic_module_type_host_envoy_ptr* host_out);

/**
 * envoy_dynamic_module_callback_cluster_remove_host removes a host from the cluster.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param host is the host pointer to remove.
 * @return the result status.
 */
envoy_dynamic_module_type_cluster_result envoy_dynamic_module_callback_cluster_remove_host(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_envoy_ptr host);

/**
 * envoy_dynamic_module_callback_cluster_get_hosts retrieves all current hosts in the cluster.
 * The module must free the returned hosts_out array when done.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param hosts_out is set to point to an array of host info structures.
 * @param hosts_count_out is set to the number of hosts.
 */
void envoy_dynamic_module_callback_cluster_get_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_host_info** hosts_out, size_t* hosts_count_out);

/**
 * envoy_dynamic_module_callback_cluster_get_host_by_address retrieves a host by its address.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param address is the IP address string.
 * @param address_length is the length of the address string.
 * @param port is the port number.
 * @return the host pointer, or nullptr if not found.
 */
envoy_dynamic_module_type_host_envoy_ptr envoy_dynamic_module_callback_cluster_get_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr address, size_t address_length, uint32_t port);

/**
 * envoy_dynamic_module_callback_host_set_weight updates a host's load balancing weight.
 *
 * @param host is the host pointer.
 * @param weight is the new weight (1-128).
 */
void envoy_dynamic_module_callback_host_set_weight(envoy_dynamic_module_type_host_envoy_ptr host,
                                                   uint32_t weight);

/**
 * envoy_dynamic_module_callback_host_get_address retrieves a host's address and port.
 *
 * @param host is the host pointer.
 * @param address_out is set to point to the address buffer.
 * @param port_out is set to the port number.
 * @return the length of the address string.
 */
size_t envoy_dynamic_module_callback_host_get_address(
    envoy_dynamic_module_type_host_envoy_ptr host,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_host_get_health retrieves a host's health status.
 *
 * @param host is the host pointer.
 * @return the host health status.
 */
envoy_dynamic_module_type_host_health
envoy_dynamic_module_callback_host_get_health(envoy_dynamic_module_type_host_envoy_ptr host);

/**
 * envoy_dynamic_module_callback_host_get_weight retrieves a host's current weight.
 *
 * @param host is the host pointer.
 * @return the host weight.
 */
uint32_t
envoy_dynamic_module_callback_host_get_weight(envoy_dynamic_module_type_host_envoy_ptr host);

/**
 * envoy_dynamic_module_callback_cluster_pre_init_complete signals that cluster initialization
 * is complete. The module must call this from envoy_dynamic_module_on_cluster_init when ready.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 */
void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr);

/**
 * envoy_dynamic_module_callback_lb_context_get_hash_key retrieves the hash key from the
 * LoadBalancerContext for consistent hashing load balancing.
 *
 * @param context is the LoadBalancerContext pointer.
 * @param hash_out is set to the hash key value if available.
 * @return true if a hash key is available, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint64_t* hash_out);

/**
 * envoy_dynamic_module_callback_lb_context_get_header retrieves a request header value from the
 * LoadBalancerContext. This allows header-based load balancing.
 *
 * @param context is the LoadBalancerContext pointer.
 * @param key is the header name.
 * @param key_length is the length of the header name.
 * @param value_out is set to point to the header value buffer.
 * @return the length of the header value, or 0 if the header is not found.
 */
size_t envoy_dynamic_module_callback_lb_context_get_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* value_out);

/**
 * envoy_dynamic_module_callback_lb_context_get_override_host retrieves the override host from the
 * LoadBalancerContext for sticky sessions or explicit host routing.
 *
 * @param context is the LoadBalancerContext pointer.
 * @param address_out is set to point to the override host address buffer.
 * @param strict_out is set to whether strict routing is required.
 * @return the length of the address, or 0 if no override host is set.
 */
size_t envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr context,
    envoy_dynamic_module_type_buffer_envoy_ptr* address_out, bool* strict_out);

/**
 * envoy_dynamic_module_callback_lb_context_get_attempt_count retrieves the current request
 * attempt number from the LoadBalancerContext. This is useful for implementing retry progression
 * patterns like composite clusters where different retry attempts target different upstream
 * clusters.
 *
 * @param context is the LoadBalancerContext pointer.
 * @param attempt_out is set to the attempt count (1 for initial request, 2 for first retry, etc.).
 * @return true if the attempt count is available, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_attempt_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint32_t* attempt_out);

/**
 * envoy_dynamic_module_callback_lb_context_get_downstream_connection_id retrieves the downstream
 * connection ID from the LoadBalancerContext. This can be used for connection-based affinity.
 *
 * @param context is the LoadBalancerContext pointer.
 * @param connection_id_out is set to the connection ID.
 * @return true if the connection ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
    envoy_dynamic_module_type_lb_context_envoy_ptr context, uint64_t* connection_id_out);

// =============================================================================
// ----------------------- Cluster Manager Callbacks ---------------------------
// =============================================================================

/**
 * envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster retrieves a thread-local
 * cluster by name from the cluster manager. This enables cross-cluster patterns like composite
 * clusters where host selection can be delegated to other named clusters.
 *
 * @param cluster_envoy_ptr is the pointer to the DynamicModuleCluster object in Envoy.
 * @param cluster_name is the name of the target cluster.
 * @param cluster_name_length is the length of the cluster name.
 * @return a pointer to the ThreadLocalCluster, or nullptr if not found.
 */
envoy_dynamic_module_type_thread_local_cluster_envoy_ptr
envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr cluster_name, size_t cluster_name_length);

/**
 * envoy_dynamic_module_callback_thread_local_cluster_choose_host selects a host from another
 * cluster using its load balancer. This is the core mechanism for implementing composite clusters
 * and retry progression patterns.
 *
 * @param thread_local_cluster is the pointer to the target ThreadLocalCluster.
 * @param context is the LoadBalancerContext for the current request.
 * @return the selected host pointer, or nullptr if no host is available.
 */
envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_thread_local_cluster_choose_host(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_lb_context_envoy_ptr context);

/**
 * envoy_dynamic_module_callback_thread_local_cluster_get_name retrieves the name of a
 * ThreadLocalCluster.
 *
 * @param thread_local_cluster is the pointer to the ThreadLocalCluster.
 * @param name_out is set to point to the cluster name buffer.
 * @return the length of the cluster name.
 */
size_t envoy_dynamic_module_callback_thread_local_cluster_get_name(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster,
    envoy_dynamic_module_type_buffer_envoy_ptr* name_out);

/**
 * envoy_dynamic_module_callback_thread_local_cluster_host_count returns the number of hosts
 * in a ThreadLocalCluster (across all priorities).
 *
 * @param thread_local_cluster is the pointer to the ThreadLocalCluster.
 * @return the total number of hosts.
 */
size_t envoy_dynamic_module_callback_thread_local_cluster_host_count(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr thread_local_cluster);

#ifdef __cplusplus
}
#endif

// NOLINTEND
