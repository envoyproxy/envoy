use crate::{
  abi,
  drop_wrapped_c_void_ptr,
  str_to_module_buffer,
  wrap_into_c_void_ptr,
  NEW_CLUSTER_CONFIG_FUNCTION,
};
use mockall::*;

/// The module-side cluster configuration.
///
/// This trait must be implemented by the module to handle cluster configuration.
/// The object is created when the corresponding Envoy cluster configuration is loaded, and
/// it is dropped when the corresponding Envoy cluster configuration is destroyed.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait ClusterConfig: Send + Sync {
  /// Create a new cluster instance.
  ///
  /// This is called when a new cluster is created from this configuration.
  /// The `envoy_cluster` provides access to Envoy's cluster operations such as
  /// adding/removing hosts.
  fn new_cluster(&self, envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster>;
}

/// The module-side cluster instance.
///
/// This trait must be implemented by the module to handle cluster lifecycle events.
/// The object is created per cluster and is responsible for host discovery.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait Cluster: Send + Sync {
  /// Called when cluster initialization begins.
  ///
  /// The module should perform initial host discovery (e.g., add hosts via
  /// [`EnvoyCluster::add_hosts`]) and then call [`EnvoyCluster::pre_init_complete`]
  /// to signal that the initial set of hosts is ready.
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster);

  /// Create a new load balancer instance for a worker thread.
  ///
  /// Each worker thread gets its own load balancer instance. The `envoy_lb`
  /// provides thread-local access to the cluster's host set.
  fn new_load_balancer(&self, envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb>;
}

/// The module-side load balancer instance.
///
/// This trait must be implemented by the module to select hosts for requests.
/// One instance is created per worker thread.
pub trait ClusterLb: Send {
  /// Select a host for a request.
  ///
  /// Returns the raw host pointer obtained from [`EnvoyClusterLoadBalancer::get_healthy_host`],
  /// or `None` if no host is available.
  fn choose_host(
    &mut self,
    context: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;
}

/// Envoy-side cluster operations available to the module.
#[automock]
pub trait EnvoyCluster: Send + Sync {
  /// Add multiple hosts to the cluster in a single batch operation.
  ///
  /// Each address must be in `ip:port` format (e.g., `127.0.0.1:8080`).
  /// Each weight must be between 1 and 128. The `addresses` and `weights` slices must have the
  /// same length.
  ///
  /// This triggers only one priority set update regardless of how many hosts are added, avoiding
  /// the overhead of updating the priority set per host.
  ///
  /// Returns the host pointers if all hosts were added successfully, or `None` if any host failed
  /// (e.g., invalid address or weight). On failure, no hosts are added.
  fn add_hosts(
    &self,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>>;

  /// Remove multiple hosts from the cluster in a single batch operation.
  ///
  /// The host pointers must have been returned by a previous [`EnvoyCluster::add_hosts`] call.
  ///
  /// This triggers only one priority set update regardless of how many hosts are removed.
  ///
  /// Returns the number of hosts that were successfully removed. Hosts not found in the cluster
  /// are skipped.
  fn remove_hosts(&self, hosts: &[abi::envoy_dynamic_module_type_cluster_host_envoy_ptr]) -> usize;

  /// Signal that the cluster's initial host discovery is complete.
  ///
  /// This must be called during or after [`Cluster::on_init`] to allow Envoy to start
  /// routing traffic to this cluster.
  fn pre_init_complete(&self);
}

/// Envoy-side load balancer operations available to the module.
#[automock]
pub trait EnvoyClusterLoadBalancer: Send {
  /// Get the number of healthy hosts at the given priority level.
  fn get_healthy_host_count(&self, priority: u32) -> usize;

  /// Get a healthy host by index at the given priority level.
  ///
  /// Returns the host pointer, or `None` if the index is out of bounds.
  fn get_healthy_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;
}

// Implementations

struct EnvoyClusterImpl {
  raw: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
}

unsafe impl Send for EnvoyClusterImpl {}
unsafe impl Sync for EnvoyClusterImpl {}

impl EnvoyClusterImpl {
  fn new(raw: abi::envoy_dynamic_module_type_cluster_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyCluster for EnvoyClusterImpl {
  fn add_hosts(
    &self,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>> {
    let count = addresses.len();
    let address_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> =
      addresses.iter().map(|a| str_to_module_buffer(a)).collect();
    let mut result_ptrs: Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> =
      vec![std::ptr::null_mut(); count];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_cluster_add_hosts(
        self.raw,
        address_buffers.as_ptr(),
        weights.as_ptr(),
        count,
        result_ptrs.as_mut_ptr(),
      )
    };
    if success {
      Some(result_ptrs)
    } else {
      None
    }
  }

  fn remove_hosts(&self, hosts: &[abi::envoy_dynamic_module_type_cluster_host_envoy_ptr]) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_remove_hosts(self.raw, hosts.as_ptr(), hosts.len())
    }
  }

  fn pre_init_complete(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_pre_init_complete(self.raw);
    }
  }
}

struct EnvoyClusterLoadBalancerImpl {
  raw: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
}

unsafe impl Send for EnvoyClusterLoadBalancerImpl {}

impl EnvoyClusterLoadBalancerImpl {
  fn new(raw: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyClusterLoadBalancer for EnvoyClusterLoadBalancerImpl {
  fn get_healthy_host_count(&self, priority: u32) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(self.raw, priority)
    }
  }

  fn get_healthy_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> {
    let host = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host(self.raw, priority, index)
    };
    if host.is_null() {
      None
    } else {
      Some(host)
    }
  }
}

// Cluster Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_cluster_config_module_ptr {
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const _,
      name.length,
    ))
  };
  let config_slice = unsafe { std::slice::from_raw_parts(config.ptr as *const _, config.length) };
  let new_config_fn = NEW_CLUSTER_CONFIG_FUNCTION
    .get()
    .expect("NEW_CLUSTER_CONFIG_FUNCTION must be set");
  match new_config_fn(name_str, config_slice) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_cluster_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_module_ptr, ClusterConfig);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_new(
  config_module_ptr: abi::envoy_dynamic_module_type_cluster_config_module_ptr,
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_module_ptr {
  let config = config_module_ptr as *const *const dyn ClusterConfig;
  let config = &**config;
  let envoy_cluster = EnvoyClusterImpl::new(cluster_envoy_ptr);
  let cluster = config.new_cluster(&envoy_cluster);
  wrap_into_c_void_ptr!(cluster)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_init(
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = unsafe { &mut *cluster };
  let envoy_cluster = EnvoyClusterImpl::new(cluster_envoy_ptr);
  cluster.on_init(&envoy_cluster);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_destroy(
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  drop_wrapped_c_void_ptr!(cluster_module_ptr, Cluster);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_new(
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_lb_module_ptr {
  let cluster = cluster_module_ptr as *const *const dyn Cluster;
  let cluster = &**cluster;
  let envoy_lb = EnvoyClusterLoadBalancerImpl::new(lb_envoy_ptr);
  let lb = cluster.new_load_balancer(&envoy_lb);
  wrap_into_c_void_ptr!(lb)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_destroy(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
) {
  drop_wrapped_c_void_ptr!(lb_module_ptr, ClusterLb);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_lb_choose_host(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  let lb = lb_module_ptr as *mut Box<dyn ClusterLb>;
  let lb = unsafe { &mut *lb };
  match lb.choose_host(context_envoy_ptr) {
    Some(host) => host,
    None => std::ptr::null_mut(),
  }
}
