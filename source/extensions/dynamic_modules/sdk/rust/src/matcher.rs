//! Input matcher support for dynamic modules.
//!
//! This module provides traits and types for implementing custom input matchers as dynamic modules.
//! A matcher evaluates HTTP request/response data and returns a boolean match result.

use crate::abi;
use std::ffi::c_void;
use std::ptr;

/// Read-only context for accessing HTTP matching data during match evaluation.
///
/// This is passed to [`MatcherConfig::on_matcher_match`] to provide access to HTTP headers.
/// The context is only valid during the match callback and must not be stored.
pub struct MatchContext {
  envoy_ptr: *mut c_void,
}

impl MatchContext {
  /// Create a new MatchContext. Used internally by the macro.
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

  /// Get the number of headers in the specified header map.
  pub fn get_headers_size(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_matcher_get_headers_size(self.envoy_ptr, header_type)
    }
  }

  /// Get all headers from the specified header map as key-value pairs.
  ///
  /// Returns a vector of `(key, value)` byte slices, or `None` if the header map
  /// is not available.
  pub fn get_all_headers(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> Option<Vec<(&[u8], &[u8])>> {
    let size = self.get_headers_size(header_type);
    if size == 0 {
      return None;
    }

    let mut headers = vec![
      abi::envoy_dynamic_module_type_envoy_http_header {
        key_ptr: ptr::null_mut(),
        key_length: 0,
        value_ptr: ptr::null_mut(),
        value_length: 0,
      };
      size
    ];

    let result = unsafe {
      abi::envoy_dynamic_module_callback_matcher_get_headers(
        self.envoy_ptr,
        header_type,
        headers.as_mut_ptr(),
      )
    };

    if !result {
      return None;
    }

    Some(
      headers
        .iter()
        .map(|h| unsafe {
          (
            crate::ffi_helpers::slice_from_raw_or_empty(h.key_ptr as *const u8, h.key_length),
            crate::ffi_helpers::slice_from_raw_or_empty(h.value_ptr as *const u8, h.value_length),
          )
        })
        .collect(),
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
      abi::envoy_dynamic_module_callback_matcher_get_header_value(
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
}

/// Trait that the dynamic module must implement to provide matcher configuration and logic.
///
/// The implementation is created once during configuration loading and shared across all
/// match evaluations. It must be `Send + Sync` since match evaluation can happen on any
/// worker thread.
pub trait MatcherConfig: Sized + Send + Sync + 'static {
  /// Create a new matcher configuration from the provided name and config bytes.
  ///
  /// # Arguments
  /// * `name` - The matcher configuration name from the proto config.
  /// * `config` - The raw configuration bytes from the proto config.
  ///
  /// # Returns
  /// The matcher configuration on success, or an error string on failure.
  fn new(name: &str, config: &[u8]) -> Result<Self, String>;

  /// Evaluate whether the input matches.
  ///
  /// This is called on worker threads during match evaluation. The `ctx` provides
  /// access to HTTP headers and other matching data. The context is only valid during
  /// this call and must not be stored.
  ///
  /// # Returns
  /// `true` if the input matches, `false` otherwise.
  fn on_matcher_match(&self, ctx: &MatchContext) -> bool;
}

/// Macro to declare matcher entry points.
///
/// This macro generates the required C ABI functions that Envoy calls to interact with
/// the matcher implementation.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::declare_matcher;
/// use envoy_proxy_dynamic_modules_rust_sdk::matcher::*;
///
/// struct MyMatcherConfig {
///   expected_value: String,
/// }
///
/// impl MatcherConfig for MyMatcherConfig {
///   fn new(_name: &str, config: &[u8]) -> Result<Self, String> {
///     Ok(Self {
///       expected_value: String::from_utf8_lossy(config).to_string(),
///     })
///   }
///
///   fn on_matcher_match(&self, ctx: &MatchContext) -> bool {
///     if let Some(value) = ctx.get_request_header("x-match-header") {
///       value == self.expected_value.as_bytes()
///     } else {
///       false
///     }
///   }
/// }
///
/// declare_matcher!(MyMatcherConfig);
/// ```
#[macro_export]
macro_rules! declare_matcher {
  ($config_type:ty) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_config_new(
      _config_envoy_ptr: *mut ::std::ffi::c_void,
      name: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      config: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> *const ::std::ffi::c_void {
      ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        // Safe under `(nullptr, 0)` via `ffi_helpers`. The matcher name is used as a registry
        // lookup key by user `MatcherConfig::new` implementations, so invalid UTF-8 must map to
        // the empty string rather than being rewritten with `U+FFFD` substitutions; the latter
        // would silently route invalid bytes to a different registry entry.
        let name_slice = unsafe {
          $crate::ffi_helpers::slice_from_raw_or_empty(name.ptr as *const u8, name.length)
        };
        let name_str: &str = ::std::str::from_utf8(name_slice).unwrap_or("");
        let config_bytes = unsafe {
          $crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
        };

        match <$config_type as $crate::matcher::MatcherConfig>::new(name_str, config_bytes) {
          Ok(c) => Box::into_raw(Box::new(c)) as *const ::std::ffi::c_void,
          Err(_) => ::std::ptr::null(),
        }
      }))
      .unwrap_or_else(|panic| {
        $crate::log_ffi_panic("envoy_dynamic_module_on_matcher_config_new", panic);
        ::std::ptr::null()
      })
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_config_destroy(
      config_ptr: *const ::std::ffi::c_void,
    ) {
      let _ = ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(config_ptr as *mut $config_type));
      }))
      .map_err(|panic| {
        $crate::log_ffi_panic("envoy_dynamic_module_on_matcher_config_destroy", panic);
      });
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_matcher_match(
      config_ptr: *const ::std::ffi::c_void,
      matcher_input_envoy_ptr: *mut ::std::ffi::c_void,
    ) -> bool {
      ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        let config = unsafe { &*(config_ptr as *const $config_type) };
        let ctx = $crate::matcher::MatchContext::new(matcher_input_envoy_ptr);
        config.on_matcher_match(&ctx)
      }))
      .unwrap_or_else(|panic| {
        $crate::log_ffi_panic("envoy_dynamic_module_on_matcher_match", panic);
        // Fail-closed: a panic during match evaluation must not look like "matched".
        false
      })
    }
  };
}
