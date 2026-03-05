use crate::{
  abi,
  drop_wrapped_c_void_ptr,
  wrap_into_c_void_ptr,
  NEW_LOAD_BALANCER_CONFIG_FUNCTION,
};
use mockall::*;

/// Trait for interacting with the Envoy load balancer and its context.
///
/// This trait provides access to both cluster/host information and request context.
/// The cluster/host methods are always available, while the context methods are only
/// valid during the [`LoadBalancer::choose_host`] callback.
#[automock]
pub trait EnvoyLoadBalancer {
  /// Returns the cluster name.
  fn get_cluster_name(&self) -> String;

  /// Returns the number of all hosts at a given priority.
  fn get_hosts_count(&self, priority: u32) -> usize;

  /// Returns the number of healthy hosts at a given priority.
  fn get_healthy_hosts_count(&self, priority: u32) -> usize;

  /// Returns the number of degraded hosts at a given priority.
  fn get_degraded_hosts_count(&self, priority: u32) -> usize;

  /// Returns the number of priority levels.
  fn get_priority_set_size(&self) -> usize;

  /// Returns the address of a healthy host by index at a given priority.
  fn get_healthy_host_address(&self, priority: u32, index: usize) -> Option<String>;

  /// Returns the weight of a healthy host by index at a given priority.
  fn get_healthy_host_weight(&self, priority: u32, index: usize) -> u32;

  /// Returns the health status of a host by index within all hosts at a given priority.
  fn get_host_health(
    &self,
    priority: u32,
    index: usize,
  ) -> abi::envoy_dynamic_module_type_host_health;

  /// Looks up a host by its address string across all priorities and returns its health status.
  /// This provides O(1) lookup by address using the cross-priority host map, instead of requiring
  /// iteration through all hosts by index.
  ///
  /// The address must match the format "ip:port" (e.g., "10.0.0.1:8080").
  fn get_host_health_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_host_health>;

  /// Returns the address of a host by index within all hosts at a given priority.
  fn get_host_address(&self, priority: u32, index: usize) -> Option<String>;

  /// Returns the weight of a host by index within all hosts at a given priority.
  fn get_host_weight(&self, priority: u32, index: usize) -> u32;

  /// Returns the number of active requests for a host by index within all hosts at a given
  /// priority. This is essential for implementing least-request, peak EWMA, and similar algorithms.
  fn get_host_active_requests(&self, priority: u32, index: usize) -> u64;

  /// Returns the number of active connections for a host by index within all hosts at a given
  /// priority. This is useful for connection-aware load balancing decisions.
  fn get_host_active_connections(&self, priority: u32, index: usize) -> u64;

  /// Returns the locality information (region, zone, sub_zone) for a host by index within all
  /// hosts at a given priority. This enables zone-aware and locality-aware load balancing.
  fn get_host_locality(&self, priority: u32, index: usize) -> Option<(String, String, String)>;

  /// Stores an opaque value on a host identified by priority and index. This data is stored per
  /// load balancer instance (per worker thread) and can be used for per-host state such as moving
  /// averages or request tracking. Use 0 to clear the data.
  fn set_host_data(&self, priority: u32, index: usize, data: usize) -> bool;

  /// Retrieves a previously stored opaque value for a host. Returns `None` if the host was not
  /// found. Returns `Some(0)` if the host exists but no data was stored.
  fn get_host_data(&self, priority: u32, index: usize) -> Option<usize>;

  /// Returns the string metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// string.
  fn get_host_metadata_string(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<String>;

  /// Returns the number metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// number.
  fn get_host_metadata_number(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<f64>;

  /// Returns the bool metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// bool.
  fn get_host_metadata_bool(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<bool>;

  /// Returns the number of locality buckets for the healthy hosts at a given priority.
  fn get_locality_count(&self, priority: u32) -> usize;

  /// Returns the number of healthy hosts in a specific locality bucket at a given priority.
  fn get_locality_host_count(&self, priority: u32, locality_index: usize) -> usize;

  /// Returns the address of a host within a specific locality bucket at a given priority.
  fn get_locality_host_address(
    &self,
    priority: u32,
    locality_index: usize,
    host_index: usize,
  ) -> Option<String>;

  /// Returns the weight of a locality bucket at a given priority.
  fn get_locality_weight(&self, priority: u32, locality_index: usize) -> u32;

  // -------------------------------------------------------------------------
  // Context methods are only valid during choose_host callback.
  // -------------------------------------------------------------------------

  /// Returns whether the context is available.
  /// Context methods will return default values if this returns false.
  fn has_context(&self) -> bool;

  /// Computes a hash key for consistent hashing from the request context.
  /// Only valid during choose_host callback.
  fn context_compute_hash_key(&self) -> Option<u64>;

  /// Returns the number of downstream request headers.
  /// Only valid during choose_host callback.
  fn context_get_downstream_headers_size(&self) -> usize;

  /// Returns all downstream request headers as a vector of (key, value) pairs.
  /// Only valid during choose_host callback.
  fn context_get_downstream_headers(&self) -> Option<Vec<(String, String)>>;

  /// Returns a downstream request header value by key and index.
  /// Since a header can have multiple values, the index is used to get the specific value.
  /// Returns the value and optionally the total number of values for the key.
  /// Only valid during choose_host callback.
  fn context_get_downstream_header(&self, key: &str, index: usize) -> Option<(String, usize)>;

  /// Returns the maximum number of times host selection should be retried if the chosen host
  /// is rejected by [`context_should_select_another_host`]. Built-in load balancers use this
  /// value as the upper bound of a retry loop during host selection.
  /// Only valid during choose_host callback.
  fn context_get_host_selection_retry_count(&self) -> u32;

  /// Checks whether the load balancer should reject the given host and retry selection.
  /// This is used during retries to avoid selecting hosts that were already attempted.
  /// The host is identified by priority and index within all hosts at that priority.
  /// Only valid during choose_host callback.
  fn context_should_select_another_host(&self, priority: u32, index: usize) -> bool;
}

/// Implementation of EnvoyLoadBalancer that calls into the Envoy ABI.
pub struct EnvoyLoadBalancerImpl {
  lb_ptr: abi::envoy_dynamic_module_type_lb_envoy_ptr,
  context_ptr: abi::envoy_dynamic_module_type_lb_context_envoy_ptr,
}

impl EnvoyLoadBalancerImpl {
  /// Creates a new EnvoyLoadBalancerImpl with both LB and context pointers.
  pub fn new(
    lb_ptr: abi::envoy_dynamic_module_type_lb_envoy_ptr,
    context_ptr: abi::envoy_dynamic_module_type_lb_context_envoy_ptr,
  ) -> Self {
    Self {
      lb_ptr,
      context_ptr,
    }
  }
}

impl EnvoyLoadBalancer for EnvoyLoadBalancerImpl {
  fn get_cluster_name(&self) -> String {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_cluster_name(self.lb_ptr, &mut result);
      if !result.ptr.is_null() && result.length > 0 {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const _,
          result.length,
        ))
        .to_string()
      } else {
        String::new()
      }
    }
  }

  fn get_hosts_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_hosts_count(self.lb_ptr, priority) }
  }

  fn get_healthy_hosts_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_healthy_hosts_count(self.lb_ptr, priority) }
  }

  fn get_degraded_hosts_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_degraded_hosts_count(self.lb_ptr, priority) }
  }

  fn get_priority_set_size(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_priority_set_size(self.lb_ptr) }
  }

  fn get_healthy_host_address(&self, priority: u32, index: usize) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_healthy_host_address(
        self.lb_ptr,
        priority,
        index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      unsafe {
        Some(
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            result.ptr as *const _,
            result.length,
          ))
          .to_string(),
        )
      }
    } else {
      None
    }
  }

  fn get_healthy_host_weight(&self, priority: u32, index: usize) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_healthy_host_weight(self.lb_ptr, priority, index)
    }
  }

  fn get_host_health(
    &self,
    priority: u32,
    index: usize,
  ) -> abi::envoy_dynamic_module_type_host_health {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_host_health(self.lb_ptr, priority, index) }
  }

  fn get_host_health_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_host_health> {
    let address_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: address.as_ptr() as *const _,
      length: address.len(),
    };
    let mut health: abi::envoy_dynamic_module_type_host_health =
      abi::envoy_dynamic_module_type_host_health::Unhealthy;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_health_by_address(
        self.lb_ptr,
        address_buf,
        &mut health,
      )
    };
    if found {
      Some(health)
    } else {
      None
    }
  }

  fn get_host_address(&self, priority: u32, index: usize) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_address(
        self.lb_ptr,
        priority,
        index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      unsafe {
        Some(
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            result.ptr as *const _,
            result.length,
          ))
          .to_string(),
        )
      }
    } else {
      None
    }
  }

  fn get_host_weight(&self, priority: u32, index: usize) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_host_weight(self.lb_ptr, priority, index) }
  }

  fn get_host_active_requests(&self, priority: u32, index: usize) -> u64 {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_active_requests(self.lb_ptr, priority, index)
    }
  }

  fn get_host_active_connections(&self, priority: u32, index: usize) -> u64 {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_active_connections(
        self.lb_ptr,
        priority,
        index,
      )
    }
  }

  fn get_host_locality(&self, priority: u32, index: usize) -> Option<(String, String, String)> {
    let mut region = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut zone = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut sub_zone = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_locality(
        self.lb_ptr,
        priority,
        index,
        &mut region,
        &mut zone,
        &mut sub_zone,
      )
    };
    if !found {
      return None;
    }
    unsafe {
      let region_str = if !region.ptr.is_null() && region.length > 0 {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          region.ptr as *const _,
          region.length,
        ))
        .to_string()
      } else {
        String::new()
      };
      let zone_str = if !zone.ptr.is_null() && zone.length > 0 {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          zone.ptr as *const _,
          zone.length,
        ))
        .to_string()
      } else {
        String::new()
      };
      let sub_zone_str = if !sub_zone.ptr.is_null() && sub_zone.length > 0 {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          sub_zone.ptr as *const _,
          sub_zone.length,
        ))
        .to_string()
      } else {
        String::new()
      };
      Some((region_str, zone_str, sub_zone_str))
    }
  }

  fn set_host_data(&self, priority: u32, index: usize, data: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_set_host_data(self.lb_ptr, priority, index, data)
    }
  }

  fn get_host_data(&self, priority: u32, index: usize) -> Option<usize> {
    let mut data: usize = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_data(self.lb_ptr, priority, index, &mut data)
    };
    if found {
      Some(data)
    } else {
      None
    }
  }

  fn get_host_metadata_string(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<String> {
    let filter_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: filter_name.as_ptr() as *const _,
      length: filter_name.len(),
    };
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_metadata_string(
        self.lb_ptr,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      unsafe {
        Some(
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            result.ptr as *const _,
            result.length,
          ))
          .to_string(),
        )
      }
    } else {
      None
    }
  }

  fn get_host_metadata_number(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<f64> {
    let filter_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: filter_name.as_ptr() as *const _,
      length: filter_name.len(),
    };
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result: f64 = 0.0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_metadata_number(
        self.lb_ptr,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found {
      Some(result)
    } else {
      None
    }
  }

  fn get_host_metadata_bool(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<bool> {
    let filter_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: filter_name.as_ptr() as *const _,
      length: filter_name.len(),
    };
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result: bool = false;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_metadata_bool(
        self.lb_ptr,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found {
      Some(result)
    } else {
      None
    }
  }

  fn get_locality_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_lb_get_locality_count(self.lb_ptr, priority) }
  }

  fn get_locality_host_count(&self, priority: u32, locality_index: usize) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_locality_host_count(
        self.lb_ptr,
        priority,
        locality_index,
      )
    }
  }

  fn get_locality_host_address(
    &self,
    priority: u32,
    locality_index: usize,
    host_index: usize,
  ) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_locality_host_address(
        self.lb_ptr,
        priority,
        locality_index,
        host_index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      unsafe {
        Some(
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            result.ptr as *const _,
            result.length,
          ))
          .to_string(),
        )
      }
    } else {
      None
    }
  }

  fn get_locality_weight(&self, priority: u32, locality_index: usize) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_locality_weight(
        self.lb_ptr,
        priority,
        locality_index,
      )
    }
  }

  fn has_context(&self) -> bool {
    !self.context_ptr.is_null()
  }

  fn context_compute_hash_key(&self) -> Option<u64> {
    if self.context_ptr.is_null() {
      return None;
    }
    let mut hash: u64 = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_context_compute_hash_key(self.context_ptr, &mut hash)
    };
    if found {
      Some(hash)
    } else {
      None
    }
  }

  fn context_get_downstream_headers_size(&self) -> usize {
    if self.context_ptr.is_null() {
      return 0;
    }
    unsafe {
      abi::envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(self.context_ptr)
    }
  }

  fn context_get_downstream_headers(&self) -> Option<Vec<(String, String)>> {
    if self.context_ptr.is_null() {
      return None;
    }
    let size = self.context_get_downstream_headers_size();
    if size == 0 {
      return Some(Vec::new());
    }
    let mut headers = vec![
      abi::envoy_dynamic_module_type_envoy_http_header {
        key_ptr: std::ptr::null(),
        key_length: 0,
        value_ptr: std::ptr::null(),
        value_length: 0,
      };
      size
    ];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_lb_context_get_downstream_headers(
        self.context_ptr,
        headers.as_mut_ptr(),
      )
    };
    if !success {
      return None;
    }
    Some(
      headers
        .iter()
        .map(|h| unsafe {
          (
            std::str::from_utf8_unchecked(std::slice::from_raw_parts(
              h.key_ptr as *const _,
              h.key_length,
            ))
            .to_string(),
            std::str::from_utf8_unchecked(std::slice::from_raw_parts(
              h.value_ptr as *const _,
              h.value_length,
            ))
            .to_string(),
          )
        })
        .collect(),
    )
  }

  fn context_get_downstream_header(&self, key: &str, index: usize) -> Option<(String, usize)> {
    if self.context_ptr.is_null() {
      return None;
    }
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut count: usize = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_context_get_downstream_header(
        self.context_ptr,
        key_buf,
        &mut result,
        index,
        &mut count,
      )
    };
    if found {
      unsafe {
        Some((
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            result.ptr as *const _,
            result.length,
          ))
          .to_string(),
          count,
        ))
      }
    } else {
      None
    }
  }

  fn context_get_host_selection_retry_count(&self) -> u32 {
    if self.context_ptr.is_null() {
      return 0;
    }
    unsafe {
      abi::envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(self.context_ptr)
    }
  }

  fn context_should_select_another_host(&self, priority: u32, index: usize) -> bool {
    if self.context_ptr.is_null() || self.lb_ptr.is_null() {
      return false;
    }
    unsafe {
      abi::envoy_dynamic_module_callback_lb_context_should_select_another_host(
        self.lb_ptr,
        self.context_ptr,
        priority,
        index,
      )
    }
  }
}

/// Trait for the load balancer configuration.
///
/// This is created once when the load balancer policy is configured and shared across all
/// worker threads. Implementations must be `Sync` since they are accessed from worker threads.
pub trait LoadBalancerConfig: Sync {
  /// Creates a new load balancer instance for each worker thread.
  ///
  /// This is called once per worker thread when the thread is initialized.
  /// The `envoy_lb` provides access to cluster information (context methods are not available).
  fn new_load_balancer(&self, envoy_lb: &dyn EnvoyLoadBalancer) -> Box<dyn LoadBalancer>;
}

/// Represents the result of a host selection decision, containing the priority level
/// and the host index within the healthy hosts at that priority.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HostSelection {
  /// The priority level of the selected host.
  pub priority: u32,
  /// The index of the selected host within the healthy hosts at the given priority.
  pub index: u32,
}

impl HostSelection {
  /// Creates a new host selection at the given priority and index.
  pub fn new(priority: u32, index: u32) -> Self {
    Self { priority, index }
  }

  /// Creates a new host selection at priority 0 with the given index.
  /// This is a convenience for the common case of single-priority clusters.
  pub fn at_default_priority(index: u32) -> Self {
    Self { priority: 0, index }
  }
}

/// Trait for a load balancer instance.
///
/// All the event hooks are called on the same thread as the one that the [`LoadBalancer`] is
/// created via the [`LoadBalancerConfig::new_load_balancer`] method. In other words, the
/// [`LoadBalancer`] object is thread-local.
pub trait LoadBalancer {
  /// Chooses a host for an upstream request.
  ///
  /// The `envoy_lb` provides access to both cluster/host information and request context.
  /// Context methods (those starting with `context_`) are only valid during this callback.
  ///
  /// Returns a [`HostSelection`] containing the priority and index of the selected host
  /// in the healthy hosts list at that priority, or `None` if no host should be selected
  /// (which will result in no upstream connection).
  fn choose_host(&mut self, envoy_lb: &dyn EnvoyLoadBalancer) -> Option<HostSelection>;
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_config_new(
  _lb_config_envoy_ptr: abi::envoy_dynamic_module_type_lb_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_lb_config_module_ptr {
  let name_str = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
    name.ptr as *const _,
    name.length,
  ));
  let config_slice = std::slice::from_raw_parts(config.ptr as *const _, config.length);
  let new_config_fn = NEW_LOAD_BALANCER_CONFIG_FUNCTION
    .get()
    .expect("NEW_LOAD_BALANCER_CONFIG_FUNCTION must be set");
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
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_lb_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_ptr, LoadBalancerConfig);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_new(
  config_ptr: abi::envoy_dynamic_module_type_lb_config_module_ptr,
  lb_envoy_ptr: abi::envoy_dynamic_module_type_lb_envoy_ptr,
) -> abi::envoy_dynamic_module_type_lb_module_ptr {
  // During new_load_balancer, context is not available.
  let envoy_lb = EnvoyLoadBalancerImpl::new(lb_envoy_ptr, std::ptr::null_mut());
  let lb_config = {
    let raw = config_ptr as *const *const dyn LoadBalancerConfig;
    &**raw
  };
  let lb = lb_config.new_load_balancer(&envoy_lb);
  wrap_into_c_void_ptr!(lb)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_choose_host(
  lb_envoy_ptr: abi::envoy_dynamic_module_type_lb_envoy_ptr,
  lb_module_ptr: abi::envoy_dynamic_module_type_lb_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_lb_context_envoy_ptr,
  result_priority: *mut u32,
  result_index: *mut u32,
) -> bool {
  let envoy_lb = EnvoyLoadBalancerImpl::new(lb_envoy_ptr, context_envoy_ptr);
  let lb = {
    let raw = lb_module_ptr as *mut *mut dyn LoadBalancer;
    &mut **raw
  };
  match lb.choose_host(&envoy_lb) {
    Some(selection) => {
      *result_priority = selection.priority;
      *result_index = selection.index;
      true
    },
    None => false,
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_destroy(
  lb_module_ptr: abi::envoy_dynamic_module_type_lb_module_ptr,
) {
  drop_wrapped_c_void_ptr!(lb_module_ptr, LoadBalancer);
}
