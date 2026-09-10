//! Matcher string data input support for dynamic modules.
//!
//! This module provides traits and types for implementing a matcher data input as a dynamic module.
//! The module extracts a value from the HTTP request and response during match evaluation, and
//! downstream map matchers dispatch on that value. This lets a module select one of many matches
//! with a single evaluation and without clearing the route cache.

use crate::abi;
use std::ffi::c_void;
use std::ptr;

/// Read-only context for accessing HTTP matching data during a data input get evaluation.
///
/// This is passed to [`MatcherDataInput::get`] to provide access to HTTP headers. The context is
/// only valid during the get callback and must not be stored.
pub struct DataInputContext {
  envoy_ptr: *mut c_void,
}

impl DataInputContext {
  /// Create a new DataInputContext. Used internally by the macro.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get a request header value by key.
  ///
  /// Returns the header value as bytes, or `None` if the header is not present.
  pub fn get_request_header(&self, key: &str) -> Option<&[u8]> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
      key,
    )
  }

  /// Get a response header value by key.
  ///
  /// Returns the header value as bytes, or `None` if the header is not present.
  pub fn get_response_header(&self, key: &str) -> Option<&[u8]> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
      key,
    )
  }

  /// Get a response trailer value by key.
  ///
  /// Returns the trailer value as bytes, or `None` if the trailer is not present.
  pub fn get_response_trailer(&self, key: &str) -> Option<&[u8]> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
      key,
    )
  }

  /// Get a header value by type and key.
  fn get_header_value(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
    key: &str,
  ) -> Option<&[u8]> {
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_matcher_data_input_get_header_value(
        self.envoy_ptr,
        header_type,
        key_buf,
        &mut result,
        0,
        ptr::null_mut(),
      )
    } {
      unsafe {
        Some(crate::ffi_helpers::slice_from_raw_or_empty(
          result.ptr as *const u8,
          result.length,
        ))
      }
    } else {
      None
    }
  }

  /// Provide the value extracted for this evaluation. Used internally by the macro.
  #[doc(hidden)]
  pub fn set_result(&self, value: &[u8]) {
    let buffer = abi::envoy_dynamic_module_type_module_buffer {
      ptr: value.as_ptr() as *const _,
      length: value.len(),
    };
    unsafe {
      abi::envoy_dynamic_module_callback_matcher_data_input_set_result(self.envoy_ptr, buffer);
    }
  }
}

/// Trait that the dynamic module must implement to provide a matcher data input.
///
/// The implementation is created once during configuration loading and shared across all get
/// evaluations. It must be `Send + Sync` since evaluation can happen on any worker thread.
pub trait MatcherDataInput: Sized + Send + Sync + 'static {
  /// Create a new data input configuration from the provided name and config bytes.
  ///
  /// # Arguments
  /// * `name` - The data input configuration name from the proto config.
  /// * `config` - The raw configuration bytes from the proto config.
  ///
  /// # Returns
  /// The data input configuration on success, or an error string on failure.
  fn new(name: &str, config: &[u8]) -> Result<Self, String>;

  /// Extract the value used for matching.
  ///
  /// This is called on worker threads during match evaluation. The `ctx` provides access to HTTP
  /// headers. Returning `None` means the input has no value, which downstream map matchers treat as
  /// no match.
  fn get(&self, ctx: &DataInputContext) -> Option<Vec<u8>>;
}

/// Macro to declare matcher data input entry points.
///
/// This macro generates the required C ABI functions that Envoy calls to interact with the data
/// input implementation.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::declare_matcher_data_input;
/// use envoy_proxy_dynamic_modules_rust_sdk::matcher_data_input::*;
///
/// struct MyDataInput {
///   header: String,
/// }
///
/// impl MatcherDataInput for MyDataInput {
///   fn new(_name: &str, config: &[u8]) -> Result<Self, String> {
///     Ok(Self {
///       header: String::from_utf8_lossy(config).to_string(),
///     })
///   }
///
///   fn get(&self, ctx: &DataInputContext) -> Option<Vec<u8>> {
///     ctx.get_request_header(&self.header).map(|v| v.to_vec())
///   }
/// }
///
/// declare_matcher_data_input!(MyDataInput);
/// ```
#[macro_export]
macro_rules! declare_matcher_data_input {
  ($config_type:ty) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_data_input_config_new(
      _config_envoy_ptr: *mut ::std::ffi::c_void,
      name: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      config: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> *const ::std::ffi::c_void {
      ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        // Safe under `(nullptr, 0)` via `ffi_helpers`. The data input name is used as a registry
        // lookup key by user `MatcherDataInput::new` implementations, so invalid UTF-8 must map to
        // the empty string rather than being rewritten with `U+FFFD` substitutions. The latter
        // would silently route invalid bytes to a different registry entry.
        let name_slice = unsafe {
          $crate::ffi_helpers::slice_from_raw_or_empty(name.ptr as *const u8, name.length)
        };
        let name_str: &str = ::std::str::from_utf8(name_slice).unwrap_or("");
        let config_bytes = unsafe {
          $crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
        };

        match <$config_type as $crate::matcher_data_input::MatcherDataInput>::new(
          name_str,
          config_bytes,
        ) {
          Ok(c) => Box::into_raw(Box::new(c)) as *const ::std::ffi::c_void,
          Err(_) => ::std::ptr::null(),
        }
      }))
      .unwrap_or_else(|panic| {
        $crate::log_ffi_panic(
          "envoy_dynamic_module_on_matcher_data_input_config_new",
          panic,
        );
        ::std::ptr::null()
      })
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_data_input_config_destroy(
      config_ptr: *const ::std::ffi::c_void,
    ) {
      let _ = ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(config_ptr as *mut $config_type));
      }))
      .map_err(|panic| {
        $crate::log_ffi_panic(
          "envoy_dynamic_module_on_matcher_data_input_config_destroy",
          panic,
        );
      });
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_data_input_get(
      config_ptr: *const ::std::ffi::c_void,
      data_input_envoy_ptr: *mut ::std::ffi::c_void,
    ) {
      let _ = ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        let config = unsafe { &*(config_ptr as *const $config_type) };
        let ctx = $crate::matcher_data_input::DataInputContext::new(data_input_envoy_ptr);
        // A panic or a `None` result leaves the value unset, so the input has no value.
        if let Some(value) =
          <$config_type as $crate::matcher_data_input::MatcherDataInput>::get(config, &ctx)
        {
          ctx.set_result(&value);
        }
      }))
      .map_err(|panic| {
        $crate::log_ffi_panic("envoy_dynamic_module_on_matcher_data_input_get", panic);
      });
    }
  };
}
