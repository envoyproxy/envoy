//! Formatter support for dynamic modules.
//!
//! This module provides traits and types for implementing custom `%COMMAND%` operators for access
//! logs and header formatting as dynamic modules. The recommended entry point is the `formatter:`
//! arm of [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_FORMATTER_CONFIG_FUNCTION`] and lets a single module dispatch by `formatter_name`.

use crate::{abi, bytes_to_module_buffer, EnvoyBuffer};
use std::cell::RefCell;
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

pub use crate::access_log::AccessLogType;

/// Trait that the dynamic module implements to provide a command parser configuration.
///
/// A single configuration may recognize multiple commands. [`FormatterConfig::parse`] is called
/// once per command token at configuration time on the main thread. The configuration is shared by
/// every provider it produces, so it must be `Send + Sync`.
pub trait FormatterConfig: Send + Sync {
  /// Parse a single command token and return a provider for it.
  ///
  /// `command` is the text before the first parenthesis or colon, `command_arg` is the argument
  /// (empty when none is provided) and `max_length` is the optional truncation length. Returning
  /// `None` lets other command parsers handle the token.
  fn parse(
    &self,
    command: &str,
    command_arg: &str,
    max_length: Option<usize>,
  ) -> Option<Box<dyn FormatterProvider>>;
}

/// Provider that produces the substitution value for a single parsed command.
///
/// Providers are invoked concurrently on worker threads, so they must be read-only and `Send +
/// Sync`.
pub trait FormatterProvider: Send + Sync {
  /// Produce the value for the command using the formatting context.
  ///
  /// Returning `None` makes Envoy substitute the default empty value for the token.
  fn format(&self, ctx: &FormatterContext) -> Option<String>;
}

/// Read-only context for accessing request and response state during a format operation.
///
/// A context is valid only for the duration of a single [`FormatterProvider::format`] call. The
/// returned buffers borrow Envoy-owned memory and must not outlive the call.
pub struct FormatterContext {
  envoy_ptr: *mut c_void,
}

impl FormatterContext {
  /// Create a new FormatterContext. Used internally by the macro.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
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
      abi::envoy_dynamic_module_callback_formatter_get_attribute_string(
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
      abi::envoy_dynamic_module_callback_formatter_get_attribute_int(
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
      abi::envoy_dynamic_module_callback_formatter_get_attribute_bool(
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

  /// Get a request header value by key.
  ///
  /// For headers with multiple values, use [`get_header_value`](Self::get_header_value) to access
  /// subsequent values.
  pub fn get_request_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
      key,
      0,
    )
  }

  /// Get a response header value by key.
  pub fn get_response_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
      key,
      0,
    )
  }

  /// Get a response trailer value by key.
  pub fn get_response_trailer(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
      key,
      0,
    )
  }

  /// Get a header value by type, key, and value index.
  ///
  /// The supported header types are `RequestHeader`, `ResponseHeader`, and `ResponseTrailer`. Use
  /// `index` to select among multiple values for the same key.
  pub fn get_header_value(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
    key: &str,
    index: usize,
  ) -> Option<EnvoyBuffer<'_>> {
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_formatter_get_header_value(
        self.envoy_ptr,
        header_type,
        key_buf,
        &mut result,
        index,
        ptr::null_mut(),
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the number of headers of the specified type.
  ///
  /// The supported header types are `RequestHeader`, `ResponseHeader`, and `ResponseTrailer`.
  pub fn get_headers_count(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_formatter_get_headers_size(self.envoy_ptr, header_type)
    }
  }

  /// Get all headers of the specified type as key-value `EnvoyBuffer` pairs.
  ///
  /// The supported header types are `RequestHeader`, `ResponseHeader`, and `ResponseTrailer`.
  /// Returns an empty vector if the header map is not available.
  pub fn get_all_headers(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let count = self.get_headers_count(header_type);
    if count == 0 {
      return Vec::new();
    }

    let mut raw: Vec<abi::envoy_dynamic_module_type_envoy_http_header> = Vec::with_capacity(count);
    let success = unsafe {
      abi::envoy_dynamic_module_callback_formatter_get_headers(
        self.envoy_ptr,
        header_type,
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

  /// Get a value from dynamic metadata.
  ///
  /// `filter_name` is the filter namespace (e.g. "envoy.filters.http.dynamic_module") and `path` is
  /// the key within the namespace, which may be a dotted path into nested values. Only string
  /// values are returned.
  pub fn get_dynamic_metadata(&self, filter_name: &str, path: &str) -> Option<EnvoyBuffer<'_>> {
    let filter_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: filter_name.as_ptr() as *const _,
      length: filter_name.len(),
    };
    let path_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: path.as_ptr() as *const _,
      length: path.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_formatter_get_dynamic_metadata(
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

  /// Get the local reply body (if this was a local response).
  pub fn local_reply_body(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_formatter_get_local_reply_body(self.envoy_ptr, &mut result)
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the access log type of the formatting context.
  ///
  /// This is `NotSet` when the formatter is not used for access logging.
  pub fn access_log_type(&self) -> AccessLogType {
    let abi_type =
      unsafe { abi::envoy_dynamic_module_callback_formatter_get_access_log_type(self.envoy_ptr) };
    AccessLogType::from_abi(abi_type)
  }

  /// Get the HTTP response code.
  pub fn response_code(&self) -> Option<u32> {
    self
      .get_attribute_int(abi::envoy_dynamic_module_type_attribute_id::ResponseCode)
      .map(|v| v as u32)
  }

  /// Get the request protocol (e.g. "HTTP/1.1", "HTTP/2").
  pub fn protocol(&self) -> Option<EnvoyBuffer<'_>> {
    self.get_attribute_string(abi::envoy_dynamic_module_type_attribute_id::RequestProtocol)
  }

  /// Get the request ID (stream ID).
  pub fn request_id(&self) -> Option<EnvoyBuffer<'_>> {
    self.get_attribute_string(abi::envoy_dynamic_module_type_attribute_id::RequestId)
  }
}

// The format hook returns a value to Envoy in a module-owned buffer. The buffer must stay valid
// until the next call into the module on the same thread, so it is held in thread-local storage.
thread_local! {
  static FORMAT_BUFFER: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_formatter_config_new(
  _formatter_config_envoy_ptr: abi::envoy_dynamic_module_type_formatter_config_envoy_ptr,
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

    envoy_dynamic_module_on_formatter_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_FORMATTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_FORMATTER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_formatter_config_new", panic);
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_formatter_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory; this function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_formatter_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewFormatterConfigFunction,
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
pub unsafe extern "C" fn envoy_dynamic_module_on_formatter_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, FormatterConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_formatter_config_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_formatter_parse(
  config_ptr: *const c_void,
  command: abi::envoy_dynamic_module_type_envoy_buffer,
  command_arg: abi::envoy_dynamic_module_type_envoy_buffer,
  has_max_length: bool,
  max_length: usize,
) -> *const c_void {
  catch_unwind(AssertUnwindSafe(|| {
    let config = &*(config_ptr as *const Box<dyn FormatterConfig>);
    // SAFETY: `command` and `command_arg` are format-string tokens (UTF-8 by contract). The
    // helpers tolerate empty inputs and substitute `U+FFFD` for malformed UTF-8.
    let command_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(command.ptr as *const u8, command.length) };
    let command_arg_str = unsafe {
      crate::ffi_helpers::str_lossy_from_raw(command_arg.ptr as *const u8, command_arg.length)
    };
    let max_length = if has_max_length {
      Some(max_length)
    } else {
      None
    };
    match config.parse(command_str.as_ref(), command_arg_str.as_ref(), max_length) {
      Some(provider) => crate::wrap_into_c_void_ptr!(provider),
      None => ptr::null(),
    }
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_formatter_parse", panic);
    ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_formatter_provider_destroy(
  provider_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(provider_ptr, FormatterProvider);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_formatter_provider_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_formatter_format(
  provider_ptr: *const c_void,
  formatter_context_envoy_ptr: *mut c_void,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let provider = &*(provider_ptr as *const Box<dyn FormatterProvider>);
    let ctx = FormatterContext::new(formatter_context_envoy_ptr);
    match provider.format(&ctx) {
      Some(value) => {
        FORMAT_BUFFER.with(|cell| {
          let mut buf = cell.borrow_mut();
          buf.clear();
          buf.extend_from_slice(value.as_bytes());
          unsafe {
            *result = bytes_to_module_buffer(buf.as_slice());
          }
        });
        true
      },
      None => false,
    }
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_formatter_format", panic);
    false
  })
}
