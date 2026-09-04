//! Route provider support for dynamic modules.
//!
//! This module provides traits and types for selecting the route template for a request and
//! applying per-request overrides onto it. The entry point is the `route_provider:` arm of
//! [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_ROUTE_PROVIDER_CONFIG_FUNCTION`] and lets a single module dispatch by
//! `provider_name`.

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

/// How a header setter mutates a request or response header.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HeaderMutation {
  /// Append the value to the header, keeping any existing values.
  Append,
  /// Overwrite the header with the value, replacing any existing values.
  Overwrite,
  /// Remove the header. The value is ignored.
  Remove,
}

impl HeaderMutation {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_provider_header_mutation {
    match self {
      Self::Append => abi::envoy_dynamic_module_type_route_provider_header_mutation::Append,
      Self::Overwrite => abi::envoy_dynamic_module_type_route_provider_header_mutation::Overwrite,
      Self::Remove => abi::envoy_dynamic_module_type_route_provider_header_mutation::Remove,
    }
  }
}

/// Context for a single route selection.
///
/// It provides read access to the request headers and the stream info, and the setters that record
/// the selection. A context is valid only for the duration of a single
/// [`RouteProviderConfig::on_select`] call and must not be stored.
pub struct RouteProviderContext {
  envoy_ptr: *mut c_void,
}

impl RouteProviderContext {
  /// Create a new RouteProviderContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the selection context Envoy passed to
  /// [`envoy_dynamic_module_on_route_provider_select`], and the returned value must not outlive
  /// that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get the number of request headers.
  pub fn get_request_headers_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_get_request_headers_size(self.envoy_ptr)
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
      abi::envoy_dynamic_module_callback_route_provider_get_request_headers(
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
      abi::envoy_dynamic_module_callback_route_provider_get_request_header_value(
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
      abi::envoy_dynamic_module_callback_route_provider_get_attribute_string(
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
      abi::envoy_dynamic_module_callback_route_provider_get_attribute_int(
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
      abi::envoy_dynamic_module_callback_route_provider_get_attribute_bool(
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

  /// Get the random value Envoy generated for route resolution.
  ///
  /// The value is stable for the lifetime of the request, so it can be used to split traffic by
  /// percentage without re-deriving a hash.
  pub fn random_value(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_route_provider_get_random_value(self.envoy_ptr) }
  }

  /// Select which route template the request resolves to by index.
  ///
  /// The index refers to the route templates of the route provider configuration, in order. The
  /// module must select a route for the setter overrides below to apply. Returns `false` when the
  /// index is out of range, in which case nothing is selected.
  #[must_use]
  pub fn select_route(&mut self, index: usize) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_route_provider_select_route(self.envoy_ptr, index) }
  }

  /// Set the upstream cluster for the request, replacing the cluster of the selected route template.
  ///
  /// When the cluster does not exist, the request is failed with the cluster-not-found response
  /// code of the selected route.
  pub fn set_cluster_name(&mut self, cluster_name: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_cluster_name(
        self.envoy_ptr,
        crate::str_to_module_buffer(cluster_name),
      )
    }
  }

  /// Set the route timeout for the request.
  ///
  /// A zero duration disables the timeout. Sub-millisecond precision is truncated and durations
  /// that do not fit the ABI representation are clamped.
  pub fn set_timeout(&mut self, timeout: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_timeout(
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
      abi::envoy_dynamic_module_callback_route_provider_set_idle_timeout(
        self.envoy_ptr,
        duration_to_abi_millis(idle_timeout),
      )
    }
  }

  /// Set the maximum stream duration for the request.
  ///
  /// A zero duration disables the maximum stream duration. Sub-millisecond precision is truncated
  /// and durations that do not fit the ABI representation are clamped.
  pub fn set_max_stream_duration(&mut self, max_stream_duration: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_max_stream_duration(
        self.envoy_ptr,
        duration_to_abi_millis(max_stream_duration),
      )
    }
  }

  /// Set the upstream resource priority for the request.
  pub fn set_priority(&mut self, priority: ResourcePriority) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_priority(
        self.envoy_ptr,
        priority.to_abi(),
      )
    }
  }

  /// Set the request body buffer limit in bytes for the request.
  ///
  /// Envoy only raises the buffer limit of the stream, so a limit below the one already in effect
  /// does not shrink it, but it does cap the body buffered for retries.
  pub fn set_request_body_buffer_limit(&mut self, limit_bytes: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_request_body_buffer_limit(
        self.envoy_ptr,
        limit_bytes,
      )
    }
  }

  /// Select one of the route action overrides declared in the route provider configuration.
  ///
  /// The selected override replaces the retry policy, the hash policy, the subset load balancing
  /// metadata match criteria, the request mirroring policies and the rate limit policy of the
  /// selected route template, for the properties that it sets. Returns `false` when there is no
  /// override with the given name, in which case the call changes nothing.
  #[must_use]
  pub fn set_route_action_override(&mut self, name: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_action_override(
        self.envoy_ptr,
        crate::str_to_module_buffer(name),
      )
    }
  }

  /// Select one of the per-route configuration overrides declared in the route provider
  /// configuration.
  ///
  /// The selected override replaces the per-filter configuration and the route-level tracing of the
  /// selected route template, for the properties that it sets. Returns `false` when there is no
  /// override with the given name, in which case the call changes nothing.
  #[must_use]
  pub fn set_per_route_config_override(&mut self, name: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_per_route_config_override(
        self.envoy_ptr,
        crate::str_to_module_buffer(name),
      )
    }
  }

  /// Set a number-typed route metadata value under the given namespace and key.
  ///
  /// The entry is layered onto the metadata of the selected route template, so consumers that read
  /// route metadata, such as the rate limit `ROUTE_ENTRY` action or the `METADATA(ROUTE)`
  /// formatter, observe it. An existing entry with the same namespace and key is overwritten.
  pub fn set_route_metadata_number(&mut self, namespace: &str, key: &str, value: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_metadata_number(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        value,
      )
    }
  }

  /// Set a string-typed route metadata value under the given namespace and key.
  ///
  /// It behaves like [`Self::set_route_metadata_number`] but stores a string.
  pub fn set_route_metadata_string(&mut self, namespace: &str, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_metadata_string(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
      )
    }
  }

  /// Set a bool-typed route metadata value under the given namespace and key.
  ///
  /// It behaves like [`Self::set_route_metadata_number`] but stores a bool.
  pub fn set_route_metadata_bool(&mut self, namespace: &str, key: &str, value: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_metadata_bool(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        value,
      )
    }
  }

  /// Merge a serialized `google.protobuf.Struct` into the route metadata under the given namespace.
  ///
  /// Existing entries with the same key are overwritten, while the others stay in effect. A buffer
  /// that does not parse as a `google.protobuf.Struct` is a no-op.
  pub fn set_route_metadata_struct(&mut self, namespace: &str, serialized_struct: &[u8]) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_metadata_struct(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::bytes_to_module_buffer(serialized_struct),
      )
    }
  }

  /// Set the typed route metadata under the given namespace from a `google.protobuf.Any`, replacing
  /// an existing entry. A typed metadata factory registered for the namespace parses it out of
  /// `typedMetadata`. A buffer that does not parse as a `google.protobuf.Any` is a no-op.
  pub fn set_route_typed_metadata(&mut self, namespace: &str, serialized_any: &[u8]) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_route_typed_metadata(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::bytes_to_module_buffer(serialized_any),
      )
    }
  }

  /// Record a mutation applied to the request headers before they are forwarded upstream, on top of
  /// the mutations of the selected route template. The value is ignored for [`HeaderMutation::Remove`].
  pub fn set_request_header(&mut self, key: &str, value: &str, mutation: HeaderMutation) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_request_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
        mutation.to_abi(),
      )
    }
  }

  /// Record a mutation applied to the response headers before they are forwarded downstream, on top
  /// of the mutations of the selected route template. The value is ignored for
  /// [`HeaderMutation::Remove`].
  pub fn set_response_header(&mut self, key: &str, value: &str, mutation: HeaderMutation) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_response_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
        mutation.to_abi(),
      )
    }
  }

  /// Replace the request path header verbatim, after the path rewrite of the selected route
  /// template. The module supplies the full path, including the query string if one is wanted.
  pub fn set_path(&mut self, path: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_path(
        self.envoy_ptr,
        crate::str_to_module_buffer(path),
      )
    }
  }

  /// Resolve the request to a direct response with the given status code and body, instead of
  /// routing to an upstream cluster. Returns `false` when the status code is outside the range
  /// [200, 600), in which case the call changes nothing.
  #[must_use]
  pub fn set_direct_response(&mut self, status_code: u32, body: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_direct_response(
        self.envoy_ptr,
        status_code,
        crate::str_to_module_buffer(body),
      )
    }
  }

  /// Resolve the request to a redirect to the given location, instead of routing to an upstream
  /// cluster. Returns `false` when the status code is not a 3xx code, in which case the call
  /// changes nothing.
  #[must_use]
  pub fn set_redirect(&mut self, status_code: u32, location: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_provider_set_redirect(
        self.envoy_ptr,
        status_code,
        crate::str_to_module_buffer(location),
      )
    }
  }
}

/// Convert a duration to the millisecond count the ABI takes, saturating instead of wrapping.
fn duration_to_abi_millis(duration: Duration) -> u64 {
  u64::try_from(duration.as_millis()).unwrap_or(u64::MAX)
}

/// Trait that the dynamic module implements to select the route template for a request.
///
/// The configuration is created once at configuration time on the main thread and is shared by
/// every request routed through the route provider, so it must be `Send + Sync` and read-only
/// during selection.
pub trait RouteProviderConfig: Send + Sync {
  /// Select the route template and the per-request overrides for a request.
  ///
  /// This is called while the route is being resolved. The module reads the request, selects a
  /// route template with [`RouteProviderContext::select_route`] and records the overrides.
  ///
  /// Returning `false` means the module has no route for the request, in which case Envoy resolves
  /// no route.
  fn on_select(&self, ctx: &mut RouteProviderContext) -> bool;
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_provider_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_route_provider_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
  route_template_count: usize,
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

    envoy_dynamic_module_on_route_provider_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      route_template_count,
      crate::NEW_ROUTE_PROVIDER_CONFIG_FUNCTION
        .get()
        .expect("NEW_ROUTE_PROVIDER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_route_provider_config_new", panic);
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_route_provider_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_route_provider_config_new_impl(
  name: &str,
  config: &[u8],
  route_template_count: usize,
  new_fn: &crate::NewRouteProviderConfigFunction,
) -> *const c_void {
  match new_fn(name, config, route_template_count) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_provider_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, RouteProviderConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_route_provider_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_provider_select(
  config_ptr: abi::envoy_dynamic_module_type_route_provider_config_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_route_provider_context_envoy_ptr,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let config = &*(config_ptr as *const Box<dyn RouteProviderConfig>);
    let mut ctx = unsafe { RouteProviderContext::new(context_envoy_ptr) };
    config.on_select(&mut ctx)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_route_provider_select", panic);
    // A panic during selection must not look like a decision, so fail closed.
    false
  })
}
