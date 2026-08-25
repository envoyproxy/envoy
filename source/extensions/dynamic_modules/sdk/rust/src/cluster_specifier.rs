//! Cluster specifier support for dynamic modules.
//!
//! This module provides traits and types for selecting the upstream cluster and replacing route
//! entry properties per request. The entry point is the `cluster_specifier:` arm of
//! [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_CLUSTER_SPECIFIER_CONFIG_FUNCTION`] and lets a single module dispatch by
//! `specifier_name`.

use crate::{abi, EnvoyBuffer};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::time::Duration;

/// The priority of the upstream resources used for a request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResourcePriority {
  /// The default priority resources of the cluster.
  Default,
  /// The high priority resources of the cluster.
  High,
}

impl ResourcePriority {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_resource_priority {
    match self {
      Self::Default => abi::envoy_dynamic_module_type_resource_priority::Default,
      Self::High => abi::envoy_dynamic_module_type_resource_priority::High,
    }
  }
}

/// Context for a single cluster selection.
///
/// It provides read access to the request headers and the stream info, and the setters that record
/// the selection. A context is valid only for the duration of a single
/// [`ClusterSpecifierConfig::on_select`] call and must not be stored. Properties that are not set
/// fall back to the matched route when that call returns `true`.
pub struct ClusterSpecifierContext {
  envoy_ptr: *mut c_void,
}

impl ClusterSpecifierContext {
  /// Create a new ClusterSpecifierContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the selection context Envoy passed to
  /// [`envoy_dynamic_module_on_cluster_specifier_select`], and the returned value must not outlive
  /// that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get the number of request headers.
  pub fn get_request_headers_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_request_headers_size(self.envoy_ptr)
    }
  }

  /// Get all request headers as key-value [`EnvoyBuffer`] pairs.
  ///
  /// Returns an empty vector when there are no headers.
  pub fn get_all_request_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let count = self.get_request_headers_count();
    if count == 0 {
      return Vec::new();
    }
    let mut raw: Vec<abi::envoy_dynamic_module_type_envoy_http_header> = Vec::with_capacity(count);
    let success = unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_request_headers(
        self.envoy_ptr,
        raw.as_mut_ptr(),
      )
    };
    if !success {
      return Vec::new();
    }
    unsafe {
      raw.set_len(count);
    }
    raw
      .iter()
      .map(|h| {
        (
          unsafe { EnvoyBuffer::new_from_raw(h.key_ptr as *const _, h.key_length) },
          unsafe { EnvoyBuffer::new_from_raw(h.value_ptr as *const _, h.value_length) },
        )
      })
      .collect()
  }

  /// Get the first value of the request header with the given key.
  pub fn get_request_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self
      .get_request_header_value(key, 0)
      .map(|(value, _)| value)
  }

  /// Get the request header value with the given key at the given value index.
  ///
  /// Since a header key can have multiple values, the `index` parameter selects a specific value.
  /// Returns `Some((value, total_count))` where `total_count` is the number of values for the key,
  /// or `None` if the header was not found at the given index.
  pub fn get_request_header_value(
    &self,
    key: &str,
    index: usize,
  ) -> Option<(EnvoyBuffer<'_>, usize)> {
    let key_buf = crate::str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let mut total_count: usize = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_request_header_value(
        self.envoy_ptr,
        key_buf,
        &mut result,
        index,
        &mut total_count,
      )
    } {
      Some((
        unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) },
        total_count,
      ))
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as a string.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_string(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_attribute_string(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as an integer.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<u64> {
    let mut result: u64 = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_attribute_int(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as a boolean.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_bool(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<bool> {
    let mut result: bool = false;
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_attribute_bool(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get a value from dynamic metadata.
  ///
  /// `filter_name` is the filter namespace (e.g. "envoy.filters.http.dynamic_module") and `path` is
  /// the key within the namespace, which may be a dotted path into nested values. Only string
  /// values are returned.
  pub fn get_dynamic_metadata(&self, filter_name: &str, path: &str) -> Option<EnvoyBuffer<'_>> {
    let filter_buf = crate::str_to_module_buffer(filter_name);
    let path_buf = crate::str_to_module_buffer(path);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata(
        self.envoy_ptr,
        filter_buf,
        path_buf,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the name of the matched route.
  ///
  /// This returns `None` when the route has no name.
  pub fn route_name(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_get_route_name(
        self.envoy_ptr,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the random value Envoy generated for cluster selection.
  ///
  /// The value is stable for the lifetime of the request, including across refreshes, so it can be
  /// used to split traffic by percentage without re-deriving a hash.
  pub fn random_value(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_cluster_specifier_get_random_value(self.envoy_ptr) }
  }

  /// Set the upstream cluster for the request.
  ///
  /// When the cluster does not exist, the request is failed with the cluster-not-found response
  /// code of the matched route.
  pub fn set_cluster_name(&mut self, cluster_name: &str) {
    let name_buf = crate::str_to_module_buffer(cluster_name);
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_cluster_name(
        self.envoy_ptr,
        name_buf,
      )
    }
  }

  /// Set the route timeout for the request.
  ///
  /// A zero duration disables the timeout. Sub-millisecond precision is truncated and durations
  /// that do not fit the ABI representation are clamped.
  pub fn set_timeout(&mut self, timeout: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_timeout(
        self.envoy_ptr,
        duration_to_abi_millis(timeout),
      )
    }
  }

  /// Set the stream idle timeout for the request.
  ///
  /// A zero duration disables the idle timeout rather than deferring to the connection manager
  /// timeout. Sub-millisecond precision is truncated and durations that do not fit the ABI
  /// representation are clamped.
  pub fn set_idle_timeout(&mut self, idle_timeout: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_idle_timeout(
        self.envoy_ptr,
        duration_to_abi_millis(idle_timeout),
      )
    }
  }

  /// Set the request body buffer limit in bytes for the request.
  ///
  /// Zero disables body buffering rather than removing the limit, and `u64::MAX` means that no
  /// limit is configured. Envoy only raises the buffer limit of the stream, so a limit below the
  /// one already in effect does not shrink it, but it does cap the body buffered for retries.
  pub fn set_request_body_buffer_limit(&mut self, limit_bytes: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_request_body_buffer_limit(
        self.envoy_ptr,
        limit_bytes,
      )
    }
  }

  /// Set the upstream resource priority for the request.
  pub fn set_priority(&mut self, priority: ResourcePriority) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_priority(
        self.envoy_ptr,
        priority.to_abi(),
      )
    }
  }

  /// Select one of the route action overrides declared in the cluster specifier configuration.
  ///
  /// The selected override replaces the retry policy, the subset load balancing metadata match
  /// criteria and the request mirroring policies of the matched route, for the properties that it
  /// sets. Returns `false` when there is no override with the given name, in which case the call
  /// changes nothing, so the properties of the matched route stay in effect unless an earlier call
  /// already selected an override.
  #[must_use]
  pub fn set_route_action_override(&mut self, name: &str) -> bool {
    let name_buf = crate::str_to_module_buffer(name);
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_specifier_set_route_action_override(
        self.envoy_ptr,
        name_buf,
      )
    }
  }
}

/// Convert a duration to the millisecond count the ABI takes, saturating instead of wrapping.
fn duration_to_abi_millis(duration: Duration) -> u64 {
  u64::try_from(duration.as_millis()).unwrap_or(u64::MAX)
}

/// Trait that the dynamic module implements to select the upstream cluster for a request.
///
/// The configuration is created once at configuration time on the main thread and is shared by
/// every request routed through the cluster specifier, so it must be `Send + Sync` and read-only
/// during selection.
pub trait ClusterSpecifierConfig: Send + Sync {
  /// Select the cluster and the route entry properties for a request.
  ///
  /// This is called while the route is being resolved, and again whenever a filter refreshes the
  /// route cluster or a retry re-selects the cluster, so it must be able to reach a decision from
  /// the request headers and the stream info alone.
  ///
  /// Returning `false` discards everything set on `ctx` and leaves the previous decision, if any,
  /// in effect. There is no previous decision on the first call, so the properties of the matched
  /// route are used.
  fn on_select(&self, ctx: &mut ClusterSpecifierContext) -> bool;
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_specifier_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> *const c_void {
  catch_unwind(AssertUnwindSafe(|| {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes.
    // The helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed
    // UTF-8 rather than triggering UB.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    envoy_dynamic_module_on_cluster_specifier_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_CLUSTER_SPECIFIER_CONFIG_FUNCTION
        .get()
        .expect("NEW_CLUSTER_SPECIFIER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_cluster_specifier_config_new",
      panic,
    );
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_cluster_specifier_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_cluster_specifier_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewClusterSpecifierConfigFunction,
) -> *const c_void {
  match new_fn(name, config) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_specifier_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, ClusterSpecifierConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_cluster_specifier_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_specifier_select(
  config_ptr: abi::envoy_dynamic_module_type_cluster_specifier_config_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let config = &*(config_ptr as *const Box<dyn ClusterSpecifierConfig>);
    let mut ctx = unsafe { ClusterSpecifierContext::new(context_envoy_ptr) };
    config.on_select(&mut ctx)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_cluster_specifier_select", panic);
    // A panic during selection must not look like a decision, so fail closed.
    false
  })
}
