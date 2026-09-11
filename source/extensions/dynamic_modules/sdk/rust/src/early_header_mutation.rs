//! Early header mutation support for dynamic modules.
//!
//! This module provides traits and types for rewriting request headers before routing, tracing or
//! any filter processing, as an `envoy.http.early_header_mutation` extension. The entry point is
//! the `early_header_mutation:` arm of [`crate::declare_all_init_functions!`], which registers a
//! factory through [`crate::NEW_EARLY_HEADER_MUTATION_CONFIG_FUNCTION`] and lets a single module
//! dispatch by `early_header_mutation_name`.
//!
//! The configuration object is created once on the main thread and is shared by every worker
//! thread. [`EarlyHeaderMutationConfig::mutate`] is called concurrently on worker threads against
//! that single object, so it takes `&self` and the trait requires `Send + Sync`.

use crate::{abi, EnvoyBuffer};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

/// Trait that the dynamic module implements to rewrite request headers before routing.
///
/// The configuration is created once at configuration time on the main thread and is shared by
/// every request handled by every worker thread, so it must be `Send + Sync` and must not rely on
/// unsynchronized interior mutability.
pub trait EarlyHeaderMutationConfig: Send + Sync {
  /// Mutate the request headers for a single request.
  ///
  /// This is called before routing, tracing, request ID generation and any filter processing,
  /// concurrently on every worker thread against the one shared configuration object.
  ///
  /// The return value is **not** a success or failure indication. Returning `true` lets Envoy
  /// continue to the next early header mutation extension in the configured chain; returning
  /// `false` stops the chain so that no later extension runs. Mutations already applied to `ctx`
  /// are kept in either case. When this is the last or only extension in the chain, the return
  /// value has no effect.
  fn mutate(&self, ctx: &mut EarlyHeaderMutationContext) -> bool;
}

/// Context for a single early header mutation.
///
/// It provides read and write access to the request headers, plus read-only access to the stream
/// info. A context is valid only for the duration of a single
/// [`EarlyHeaderMutationConfig::mutate`] call and must not be stored.
///
/// The read accessors borrow `&self` and return [`EnvoyBuffer`]s pointing at Envoy-owned memory,
/// while the mutating accessors take `&mut self`. The borrow checker therefore prevents holding a
/// header buffer across a mutation that could invalidate it.
pub struct EarlyHeaderMutationContext {
  envoy_ptr: *mut c_void,
}

impl EarlyHeaderMutationContext {
  /// Create a new EarlyHeaderMutationContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the per-request pointer Envoy passed to
  /// [`envoy_dynamic_module_on_early_header_mutation_mutate`], and the returned value must not
  /// outlive that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  // ---------------------------------------------------------------------------
  // Request headers - read
  // ---------------------------------------------------------------------------

  /// Get the number of request headers.
  pub fn get_headers_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_headers_size(self.envoy_ptr)
    }
  }

  /// Get all request headers as key-value [`EnvoyBuffer`] pairs.
  ///
  /// Returns an empty vector when there are no headers.
  pub fn get_all_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let count = self.get_headers_count();
    if count == 0 {
      return Vec::new();
    }

    let mut raw: Vec<abi::envoy_dynamic_module_type_envoy_http_header> = Vec::with_capacity(count);
    let success = unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_headers(
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
  pub fn get_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value(key, 0).map(|(value, _)| value)
  }

  /// Get the request header value with the given key at the given value index.
  ///
  /// Since a header key can have multiple values, `index` selects a specific one. Returns
  /// `Some((value, total_count))` where `total_count` is the number of values for the key, or
  /// `None` when the header does not exist at the given index.
  pub fn get_header_value(&self, key: &str, index: usize) -> Option<(EnvoyBuffer<'_>, usize)> {
    let key_buf = crate::str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let mut total_count: usize = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_header_value(
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

  // ---------------------------------------------------------------------------
  // Request headers - mutate
  // ---------------------------------------------------------------------------

  /// Set the request header with the given key, replacing any existing values.
  ///
  /// Returns `false` when the key is empty.
  ///
  /// Note that this only sets the header on the underlying Envoy object. The connection manager
  /// and the subsequent filters may still rewrite or drop it, so `true` does not guarantee that
  /// the header reaches the upstream.
  pub fn set_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_set_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::bytes_to_module_buffer(value),
      )
    }
  }

  /// Add a value to the request header with the given key, preserving existing values.
  ///
  /// Returns `false` when the key is empty.
  pub fn add_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_add_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::bytes_to_module_buffer(value),
      )
    }
  }

  /// Remove all values of the request header with the given key.
  ///
  /// Returns `false` when the key is empty. Removing a key that does not exist is successful.
  pub fn remove_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_remove_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
      )
    }
  }

  // ---------------------------------------------------------------------------
  // Stream info (read-only)
  // ---------------------------------------------------------------------------

  /// Get the value of the attribute with the given ID as a string.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`. Early
  /// header mutation runs before routing and before the upstream request, so the route, response
  /// and upstream attributes are not populated yet.
  pub fn get_attribute_string(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
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
      abi::envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
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
      abi::envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool(
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
      abi::envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
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

  /// Get a number value from dynamic metadata.
  ///
  /// The arguments are the same as [`Self::get_dynamic_metadata`]. Only number values are returned.
  pub fn get_dynamic_metadata_number(&self, filter_name: &str, path: &str) -> Option<f64> {
    let filter_buf = crate::str_to_module_buffer(filter_name);
    let path_buf = crate::str_to_module_buffer(path);
    let mut result: f64 = 0.0;
    if unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
        self.envoy_ptr,
        filter_buf,
        path_buf,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get a boolean value from dynamic metadata.
  ///
  /// The arguments are the same as [`Self::get_dynamic_metadata`]. Only boolean values are
  /// returned.
  pub fn get_dynamic_metadata_bool(&self, filter_name: &str, path: &str) -> Option<bool> {
    let filter_buf = crate::str_to_module_buffer(filter_name);
    let path_buf = crate::str_to_module_buffer(path);
    let mut result: bool = false;
    if unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool(
        self.envoy_ptr,
        filter_buf,
        path_buf,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get the bytes value of the filter state with the given key.
  ///
  /// Only values stored as string accessors, which is what the dynamic module filter state setters
  /// create, are readable. Returns `None` when the key does not exist or holds another type.
  pub fn get_filter_state_bytes(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    let key_buf = crate::str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
        self.envoy_ptr,
        key_buf,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_early_header_mutation_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr,
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

    envoy_dynamic_module_on_early_header_mutation_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_EARLY_HEADER_MUTATION_CONFIG_FUNCTION
        .get()
        .expect("NEW_EARLY_HEADER_MUTATION_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_early_header_mutation_config_new",
      panic,
    );
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_early_header_mutation_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory; this function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_early_header_mutation_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewEarlyHeaderMutationConfigFunction,
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
pub unsafe extern "C" fn envoy_dynamic_module_on_early_header_mutation_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, EarlyHeaderMutationConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_early_header_mutation_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_early_header_mutation_mutate(
  config_ptr: abi::envoy_dynamic_module_type_early_header_mutation_config_module_ptr,
  envoy_ptr: abi::envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    // The configuration is shared by all worker threads and is only ever borrowed immutably here,
    // which is why `EarlyHeaderMutationConfig` requires `Send + Sync`.
    let config = &*(config_ptr as *const Box<dyn EarlyHeaderMutationConfig>);
    let mut ctx = unsafe { EarlyHeaderMutationContext::new(envoy_ptr) };
    config.mutate(&mut ctx)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_early_header_mutation_mutate",
      panic,
    );
    // The return value selects chain continuation, not success. A panicking module must not
    // silently disable the extensions configured after it, so continue the chain.
    true
  })
}
