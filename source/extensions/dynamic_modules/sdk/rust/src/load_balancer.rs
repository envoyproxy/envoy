use crate::{
  abi, drop_wrapped_c_void_ptr, str_to_module_buffer, strs_to_module_buffers, wrap_into_c_void_ptr,
  EnvoyCounterId, EnvoyCounterVecId, EnvoyGaugeId, EnvoyGaugeVecId, EnvoyHistogramId,
  EnvoyHistogramVecId, NEW_LOAD_BALANCER_CONFIG_FUNCTION,
};
use mockall::*;
use std::sync::Arc;

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

  /// Returns the value of a per-host stat. This provides access to host-level counters and gauges
  /// such as total connections, request errors, active requests, and active connections.
  fn get_host_stat(
    &self,
    priority: u32,
    index: usize,
    stat: abi::envoy_dynamic_module_type_host_stat,
  ) -> u64;

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
  // Member update methods are only valid during on_host_membership_update callback.
  // -------------------------------------------------------------------------

  /// Returns the address of an added or removed host during on_host_membership_update.
  /// If `is_added` is true, returns the address of the added host at the given index.
  /// If `is_added` is false, returns the address of the removed host at the given index.
  /// Only valid during on_host_membership_update callback.
  fn get_member_update_host_address(&self, index: usize, is_added: bool) -> Option<String>;

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

  /// Returns the override host address and strict mode flag from the context. Override host
  /// allows upstream filters to direct the load balancer to prefer a specific host by address.
  /// Returns `Some((address, strict))` if an override host is set, `None` otherwise. When
  /// `strict` is true, the load balancer should return no host if the override is not valid.
  /// Only valid during choose_host callback.
  fn context_get_override_host(&self) -> Option<(String, bool)>;
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

  fn get_host_stat(
    &self,
    priority: u32,
    index: usize,
    stat: abi::envoy_dynamic_module_type_host_stat,
  ) -> u64 {
    unsafe {
      abi::envoy_dynamic_module_callback_lb_get_host_stat(self.lb_ptr, priority, index, stat)
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

  fn get_member_update_host_address(&self, index: usize, is_added: bool) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_get_member_update_host_address(
        self.lb_ptr,
        index,
        is_added,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const _,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
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

  fn context_get_override_host(&self) -> Option<(String, bool)> {
    if self.context_ptr.is_null() {
      return None;
    }
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut strict = false;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_lb_context_get_override_host(
        self.context_ptr,
        &mut address,
        &mut strict,
      )
    };
    if found {
      unsafe {
        Some((
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            address.ptr as *const _,
            address.length,
          ))
          .to_string(),
          strict,
        ))
      }
    } else {
      None
    }
  }
}

/// Trait for defining and recording custom metrics for load balancer modules.
///
/// An implementation of this trait is passed to the user's config creation function for defining
/// metrics. It can also be stored by the user and used at runtime (e.g., during host selection)
/// to record metric values. The raw pointer is safe to store and use from any thread because the
/// underlying C++ `DynamicModuleLbConfig` is thread-safe for metric operations.
#[automock]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyLbConfig: Send + Sync {
  // -------------------------------------------------------------------------
  // Define metrics (call during config creation).
  // -------------------------------------------------------------------------

  /// Define a new counter with the given name and no labels.
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec with the given name and label names.
  fn define_counter_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge with the given name and no labels.
  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec with the given name and label names.
  fn define_gauge_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram with the given name and no labels.
  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec with the given name and label names.
  fn define_histogram_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result>;

  // -------------------------------------------------------------------------
  // Record metrics (call at runtime, e.g., during choose_host).
  // -------------------------------------------------------------------------

  /// Increment a previously defined counter by the given value.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increment a previously defined counter vec by the given value with label values.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge vec with label values.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge by the given value.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge vec by the given value with label values.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge by the given value.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge vec by the given value with label values.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in a previously defined histogram.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in a previously defined histogram vec with label values.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;
}

/// Implementation of [`EnvoyLbConfig`] that calls into the Envoy ABI.
pub struct EnvoyLbConfigImpl {
  raw: abi::envoy_dynamic_module_type_lb_config_envoy_ptr,
}

// The raw pointer references C++ DynamicModuleLbConfig which is safe for metric operations
// from any thread.
unsafe impl Send for EnvoyLbConfigImpl {}
unsafe impl Sync for EnvoyLbConfigImpl {}

/// Converts an ABI metrics result to a Rust Result for recording operations.
fn metric_result_to_rust(
  res: abi::envoy_dynamic_module_type_metrics_result,
) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
  if res == abi::envoy_dynamic_module_type_metrics_result::Success {
    Ok(())
  } else {
    Err(res)
  }
}

impl EnvoyLbConfig for EnvoyLbConfigImpl {
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_counter_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_gauge_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn define_histogram_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_increment_counter(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_increment_counter(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_set_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_set_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_increment_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_increment_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_decrement_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_decrement_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_record_histogram_value(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_lb_config_record_histogram_value(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
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

  /// Called when the set of hosts in the cluster changes (hosts added or removed).
  ///
  /// The `envoy_lb` provides access to cluster/host information. During this callback,
  /// [`EnvoyLoadBalancer::get_member_update_host_address`] can be used to get the addresses
  /// of the added or removed hosts.
  ///
  /// The default implementation is a no-op.
  fn on_host_membership_update(
    &mut self,
    _envoy_lb: &dyn EnvoyLoadBalancer,
    _num_hosts_added: usize,
    _num_hosts_removed: usize,
  ) {
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_config_new(
  lb_config_envoy_ptr: abi::envoy_dynamic_module_type_lb_config_envoy_ptr,
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
  let envoy_lb_config: Arc<dyn EnvoyLbConfig> = Arc::new(EnvoyLbConfigImpl {
    raw: lb_config_envoy_ptr,
  });
  match new_config_fn(name_str, config_slice, envoy_lb_config) {
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
pub unsafe extern "C" fn envoy_dynamic_module_on_lb_on_host_membership_update(
  lb_envoy_ptr: abi::envoy_dynamic_module_type_lb_envoy_ptr,
  lb_module_ptr: abi::envoy_dynamic_module_type_lb_module_ptr,
  num_hosts_added: usize,
  num_hosts_removed: usize,
) {
  let envoy_lb = EnvoyLoadBalancerImpl::new(lb_envoy_ptr, std::ptr::null_mut());
  let lb = {
    let raw = lb_module_ptr as *mut *mut dyn LoadBalancer;
    &mut **raw
  };
  lb.on_host_membership_update(&envoy_lb, num_hosts_added, num_hosts_removed);
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
