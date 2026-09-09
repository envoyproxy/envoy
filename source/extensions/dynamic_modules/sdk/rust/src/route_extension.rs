//! Route extension support for dynamic modules.
//!
//! This module provides the trait and types for customizing the route used for a request. The entry
//! point is the `route_extension:` arm of [`crate::declare_all_init_functions!`], which registers a
//! factory through [`crate::NEW_ROUTE_EXTENSION_CONFIG_FUNCTION`] and lets a single module dispatch
//! by `extension_name`.

use crate::{abi, EnvoyBuffer};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

/// The decision a route extension returns for a request. It selects how Envoy uses the route the
/// extension received.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteExtensionDecision {
  /// Use the received route unchanged.
  Keep,
  /// Use the received route with the overrides recorded during the hook.
  Override,
  /// Use no route, so the request is handled as if nothing had matched.
  Drop,
}

impl RouteExtensionDecision {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_extension_decision {
    match self {
      Self::Keep => abi::envoy_dynamic_module_type_route_extension_decision::Keep,
      Self::Override => abi::envoy_dynamic_module_type_route_extension_decision::Override,
      Self::Drop => abi::envoy_dynamic_module_type_route_extension_decision::Drop,
    }
  }
}

/// Context for a single route customization.
///
/// It provides read access to the request headers and the setters that record the overrides. A
/// context is valid only for the duration of a single [`RouteExtensionConfig::on_route`] call and
/// must not be stored. Overrides take effect only when that call returns
/// [`RouteExtensionDecision::Override`].
pub struct RouteExtensionContext {
  envoy_ptr: *mut c_void,
}

impl RouteExtensionContext {
  /// Create a new RouteExtensionContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the customization context Envoy passed to
  /// [`envoy_dynamic_module_on_route_extension_on_route`], and the returned value must not outlive
  /// that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get the number of request headers.
  pub fn get_request_headers_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_route_extension_get_request_headers_size(self.envoy_ptr)
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
      abi::envoy_dynamic_module_callback_route_extension_get_request_headers(
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
      abi::envoy_dynamic_module_callback_route_extension_get_request_header_value(
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

  /// Get the stable random value Envoy generated for the request.
  pub fn get_random_value(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_route_extension_get_random_value(self.envoy_ptr) }
  }

  /// Record the upstream cluster the request should use. It takes effect only when the hook returns
  /// [`RouteExtensionDecision::Override`].
  pub fn set_cluster_name(&mut self, cluster_name: &str) {
    let name_buf = crate::str_to_module_buffer(cluster_name);
    unsafe {
      abi::envoy_dynamic_module_callback_route_extension_set_cluster_name(self.envoy_ptr, name_buf)
    }
  }

  /// Select a named route action override declared in the configuration. It replaces the retry
  /// policy, the metadata match criteria, the request mirroring policies and the hash policy of the
  /// matched route with the ones the override builds, for the properties that it sets. It takes
  /// effect only when the hook returns [`RouteExtensionDecision::Override`].
  ///
  /// Returns `false` when there is no override with the given name, in which case the properties of
  /// the matched route stay in effect unless an earlier call already selected an override.
  #[must_use]
  pub fn set_route_action_override(&mut self, name: &str) -> bool {
    let name_buf = crate::str_to_module_buffer(name);
    unsafe {
      abi::envoy_dynamic_module_callback_route_extension_set_route_action_override(
        self.envoy_ptr,
        name_buf,
      )
    }
  }
}

/// The in-module configuration of a route extension. A single configuration is shared by every
/// request, so it must be `Send + Sync` and hold no per request state.
pub trait RouteExtensionConfig: Send + Sync {
  /// Customize the route for a request. The extension reads the request from `ctx`, records any
  /// overrides, and returns the decision that tells Envoy how to use the route.
  fn on_route(&self, ctx: &mut RouteExtensionContext) -> RouteExtensionDecision;
}

/// The FFI entry point Envoy calls to create a new in-module route extension configuration.
///
/// # Safety
///
/// This is called by Envoy on the main thread. The buffers are owned by Envoy and are valid only
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_extension_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_route_extension_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> *const c_void {
  catch_unwind(AssertUnwindSafe(|| {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes. The
    // helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed UTF-8
    // rather than triggering UB.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    envoy_dynamic_module_on_route_extension_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_ROUTE_EXTENSION_CONFIG_FUNCTION
        .get()
        .expect("NEW_ROUTE_EXTENSION_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_route_extension_config_new", panic);
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_route_extension_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option` to pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_route_extension_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewRouteExtensionConfigFunction,
) -> *const c_void {
  match new_fn(name, config) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

/// The FFI entry point Envoy calls to destroy an in-module route extension configuration.
///
/// # Safety
///
/// `config_ptr` must be a pointer returned by
/// [`envoy_dynamic_module_on_route_extension_config_new`] and not previously destroyed.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_extension_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, RouteExtensionConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_route_extension_config_destroy",
      panic,
    );
  });
}

/// The FFI entry point Envoy calls while resolving the route for a request.
///
/// # Safety
///
/// `config_ptr` must be a live configuration and `context_envoy_ptr` must be the context Envoy
/// passed for the current call.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_route_extension_on_route(
  config_ptr: abi::envoy_dynamic_module_type_route_extension_config_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_route_extension_context_envoy_ptr,
) -> abi::envoy_dynamic_module_type_route_extension_decision {
  catch_unwind(AssertUnwindSafe(|| {
    let config = &*(config_ptr as *const Box<dyn RouteExtensionConfig>);
    let mut ctx = unsafe { RouteExtensionContext::new(context_envoy_ptr) };
    config.on_route(&mut ctx).to_abi()
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_route_extension_on_route", panic);
    // A panic while routing must not drop or change the route, so fail closed by keeping it.
    abi::envoy_dynamic_module_type_route_extension_decision::Keep
  })
}
