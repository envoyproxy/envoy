//! HTTP/1 header formatter support for dynamic modules.
//!
//! This module provides traits and types for deciding the casing of header keys written on the
//! wire, as an `envoy.http.stateful_header_formatters` extension. The entry point is the
//! `header_formatter:` arm of [`crate::declare_all_init_functions!`], which registers a factory
//! through [`crate::NEW_HEADER_FORMATTER_CONFIG_FUNCTION`] and lets a single module dispatch by
//! `header_formatter_name`.
//!
//! There are two levels of object. The configuration is created once on the main thread and is
//! shared by every worker thread, so [`HeaderFormatterConfig`] requires `Send + Sync`. From it
//! Envoy creates one [`HeaderFormatter`] per HTTP/1 message, used only by the worker thread
//! decoding that message, which is why the formatter needs neither `Send` nor `Sync` and can take
//! `&mut self`.
//!
//! Unlike most extension points there are almost no Envoy callbacks here: header formatting
//! happens in the codec, below the filter chain, so there is no stream info, dynamic metadata or
//! filter state to read. The module only sees header keys, plus the logging exposed by
//! [`HeaderFormatterHandle`], which is where future formatter-scoped callbacks will live.

use crate::{abi, bytes_to_module_buffer, ffi_export};
use std::ffi::c_void;

/// Trait that the dynamic module implements to produce header formatters.
///
/// One configuration is created at configuration time on the main thread and is shared by every
/// worker thread, so it must be `Send + Sync`. [`HeaderFormatterConfig::create`] is called
/// concurrently, once per HTTP/1 message.
pub trait HeaderFormatterConfig: Send + Sync {
  /// Create the formatter for a single HTTP/1 message.
  ///
  /// Returning `None` makes Envoy use its default header casing for that message rather than
  /// failing it.
  fn create(&self) -> Option<Box<dyn HeaderFormatter>>;
}

/// Host handle passed to the [`HeaderFormatter`] hooks for a single HTTP/1 message.
///
/// It wraps the Envoy-side pointer to the formatter. Header formatting runs in the codec, below
/// the filter chain, so only logging is exposed today - the same logging the `envoy_log_*` macros
/// provide, reachable here without them. A handle is valid only for the duration of the call it is
/// passed to and must not be stored.
pub struct HeaderFormatterHandle {
  // Nothing is exposed through it yet; it is kept so that formatter-scoped callbacks can be added
  // without changing the handle's shape.
  #[allow(dead_code)]
  envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_envoy_ptr,
}

impl HeaderFormatterHandle {
  /// Create a new HeaderFormatterHandle. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the per-message pointer Envoy passed to the formatter hook, and the
  /// returned value must not outlive that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_envoy_ptr) -> Self {
    Self { envoy_ptr }
  }

  /// Log a message through Envoy's logging subsystem.
  ///
  /// `#[track_caller]` makes the reported source location the module call site rather than a
  /// location inside the SDK, matching what the `envoy_log_*` macros report.
  #[track_caller]
  pub fn log(&self, level: abi::envoy_dynamic_module_type_log_level, message: &str) {
    let location = std::panic::Location::caller();
    let source_file = location.file();
    // SAFETY: the logging callbacks are module-wide FFI calls provided by the Envoy host, and both
    // buffers are only read for the duration of the call.
    unsafe {
      abi::envoy_dynamic_module_callback_log_v2(
        level,
        bytes_to_module_buffer(message.as_bytes()),
        bytes_to_module_buffer(source_file.as_bytes()),
        location.line(),
      );
    }
  }

  /// Get the current effective log level of Envoy's logger.
  pub fn get_log_level(&self) -> abi::envoy_dynamic_module_type_log_level {
    crate::get_log_level()
  }

  /// Check whether the given log level is enabled, to skip work whose only purpose is a log line.
  pub fn is_log_level_enabled(&self, level: abi::envoy_dynamic_module_type_log_level) -> bool {
    crate::is_log_enabled(level)
  }
}

/// Formatter for the header keys of a single HTTP/1 message.
///
/// Every method is called by the single worker thread that owns the message, so a formatter may
/// keep mutable state without synchronization. That state is what makes the extension "stateful":
/// keys seen by [`HeaderFormatter::process_key`] while decoding can be replayed by
/// [`HeaderFormatter::format`] when encoding on the same connection.
pub trait HeaderFormatter {
  /// Called for each header key the codec receives, with the casing the peer used.
  ///
  /// Headers that Envoy itself adds never reach this method; `format` is still called for them.
  /// The default implementation ignores the key, which is what a stateless casing policy wants.
  ///
  /// Neither `key` nor `handle` may be retained beyond this call.
  fn process_key(&mut self, _key: &str, _handle: &HeaderFormatterHandle) {}

  /// Called for each header key Envoy is about to serialize, in its internal lower-cased form.
  ///
  /// Return `None` to leave the key unchanged. A formatter that only rewrites some keys should
  /// return `None` for the rest rather than echoing them back.
  ///
  /// The returned view borrows from `self`, so a formatter that computes a casing keeps it in a
  /// field and returns a reference to that - which is why this takes `&mut self`. Envoy copies the
  /// bytes only after this call has returned, so they must outlive it; storage owned by the
  /// formatter satisfies that by construction.
  ///
  /// Neither `key` nor `handle` may be retained beyond this call.
  fn format(&mut self, key: &str, handle: &HeaderFormatterHandle) -> Option<&str>;
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_config_new(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> *const c_void {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes.
    // The helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed
    // UTF-8 rather than triggering UB.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    envoy_dynamic_module_on_header_formatter_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_HEADER_FORMATTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_HEADER_FORMATTER_CONFIG_FUNCTION must be set"),
    )
  }
  on_panic = std::ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_header_formatter_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory; this function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_header_formatter_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewHeaderFormatterConfigFunction,
) -> *const c_void {
  match new_fn(name, config) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_config_destroy(config_ptr: *const c_void) {
    crate::drop_wrapped_c_void_ptr!(config_ptr, HeaderFormatterConfig);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_new(
    config_ptr: abi::envoy_dynamic_module_type_header_formatter_config_module_ptr,
    // Header formatting exposes no formatter-scoped callbacks, so the Envoy formatter pointer is
    // not retained.
    _formatter_envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_envoy_ptr,
  ) -> *const c_void {
    // The configuration is shared by all worker threads and is only ever borrowed immutably here,
    // which is why `HeaderFormatterConfig` requires `Send + Sync`.
    let config = &*(config_ptr as *const Box<dyn HeaderFormatterConfig>);
    match config.create() {
      Some(formatter) => crate::wrap_into_c_void_ptr!(formatter),
      None => std::ptr::null(),
    }
  }
  // A null formatter makes Envoy fall back to the default header casing for this message, which
  // is the safest outcome for a panicking module.
  on_panic = std::ptr::null()
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_destroy(formatter_ptr: *const c_void) {
    crate::drop_wrapped_c_void_ptr!(formatter_ptr, HeaderFormatter);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_process_key(
    formatter_envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_envoy_ptr,
    formatter_ptr: abi::envoy_dynamic_module_type_header_formatter_module_ptr,
    key: abi::envoy_dynamic_module_type_envoy_buffer,
  ) {
    // SAFETY: Envoy calls the hooks of one formatter instance from a single thread and never
    // reentrantly, so this is the only live reference to the box for the duration of the call.
    let formatter = &mut *(formatter_ptr as *mut Box<dyn HeaderFormatter>);
    let key_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(key.ptr as *const u8, key.length) };
    let handle = unsafe { HeaderFormatterHandle::new(formatter_envoy_ptr) };
    formatter.process_key(key_str.as_ref(), &handle);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_header_formatter_format(
    formatter_envoy_ptr: abi::envoy_dynamic_module_type_header_formatter_envoy_ptr,
    formatter_ptr: abi::envoy_dynamic_module_type_header_formatter_module_ptr,
    key: abi::envoy_dynamic_module_type_envoy_buffer,
    result: *mut abi::envoy_dynamic_module_type_module_buffer,
  ) -> bool {
    // SAFETY: Envoy calls the hooks of one formatter instance from a single thread and never
    // reentrantly, so this is the only live reference to the box for the duration of the call.
    let formatter = &mut *(formatter_ptr as *mut Box<dyn HeaderFormatter>);
    let key_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(key.ptr as *const u8, key.length) };
    let handle = unsafe { HeaderFormatterHandle::new(formatter_envoy_ptr) };
    match formatter.format(key_str.as_ref(), &handle) {
      Some(value) => {
        // The view points into storage owned by the formatter, which outlives this call and is
        // only overwritten by the next one - exactly the lifetime the ABI asks for, so there is
        // nothing for the SDK to copy.
        unsafe {
          *result = bytes_to_module_buffer(value.as_bytes());
        }
        true
      },
      None => false,
    }
  }
  // Leaving the key unchanged keeps the message serializable after a panic.
  on_panic = false
}
