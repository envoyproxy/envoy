//! AI filter support for dynamic modules.
//!
//! An AI filter runs inside the AI Protocol Manager HTTP filter, as an `envoy.http.ai_filters`
//! extension. It sees the request of a declared AI endpoint after Envoy parsed its payload, and may
//! read or modify the payload, read or modify the request headers, read or write filter state, or
//! reject the request with a local reply. The entry point is the `ai_filter:` arm of
//! [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_AI_FILTER_CONFIG_FUNCTION`] and lets a single module dispatch by `filter_name`.
//!
//! The configuration is created once on the main thread and is shared by every worker thread, so
//! [`AiFilterConfig`] requires `Send + Sync`. From it Envoy creates one [`AiFilter`] per stream,
//! used only by the worker thread running that stream.
//!
//! The payload is addressed by JSON pointer (RFC 6901): `""` names the whole document and
//! `"/messages/0/role"` the role of the first message. Values are exchanged as JSON text.

use crate::{abi, ffi_export, EnvoyBuffer};
use std::ffi::c_void;
use std::ptr;

/// Trait that the dynamic module implements to produce AI filters.
///
/// The configuration is shared by every stream on every worker thread, so it must be
/// `Send + Sync`. [`AiFilterConfig::new_ai_filter`] is called concurrently, once per stream.
pub trait AiFilterConfig: Send + Sync {
  /// Create the AI filter of a stream. Returning `None` skips the filter for this stream.
  ///
  /// `envoy_filter` can read the request headers and filter state to decide, but the request
  /// payload is not available yet.
  fn new_ai_filter(&self, envoy_filter: &mut EnvoyAiFilter) -> Option<Box<dyn AiFilter>>;
}

/// The AI filter of a single stream.
pub trait AiFilter {
  /// Called once, when the parsed request reaches the filter.
  ///
  /// To reject the request, call [`EnvoyAiFilter::send_local_reply`] and return; the returned
  /// status is then ignored. Returning `Error` fails the request.
  fn on_request(
    &mut self,
    envoy_filter: &mut EnvoyAiFilter,
  ) -> abi::envoy_dynamic_module_type_on_ai_filter_request_status;
}

/// Why an access to the request payload failed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AiFilterJsonError {
  /// The pointer names no value, or for a write no location whose parent exists.
  NotFound,
  /// The pointer or the value is malformed, or the document root would not be an object.
  InvalidArgument,
  /// The value is, or holds, a string Envoy offloaded to the external buffer. Its parts held in
  /// memory can still be read with a narrower pointer.
  Offloaded,
  /// The payload is only available from [`AiFilter::on_request`].
  Unavailable,
}

fn json_result(
  result: abi::envoy_dynamic_module_type_ai_filter_json_result,
) -> Result<(), AiFilterJsonError> {
  match result {
    abi::envoy_dynamic_module_type_ai_filter_json_result::Ok => Ok(()),
    abi::envoy_dynamic_module_type_ai_filter_json_result::NotFound => {
      Err(AiFilterJsonError::NotFound)
    },
    abi::envoy_dynamic_module_type_ai_filter_json_result::InvalidArgument => {
      Err(AiFilterJsonError::InvalidArgument)
    },
    abi::envoy_dynamic_module_type_ai_filter_json_result::Offloaded => {
      Err(AiFilterJsonError::Offloaded)
    },
    abi::envoy_dynamic_module_type_ai_filter_json_result::Unavailable => {
      Err(AiFilterJsonError::Unavailable)
    },
  }
}

/// The Envoy side of an AI filter, passed to the [`AiFilterConfig`] and [`AiFilter`] hooks. It is
/// valid only for the duration of the hook it is passed to and must not be stored.
///
/// The read accessors borrow `&self` and return [`EnvoyBuffer`]s pointing at Envoy-owned memory,
/// while the mutating accessors take `&mut self`, so the borrow checker prevents holding a buffer
/// across a mutation that could invalidate it.
pub struct EnvoyAiFilter {
  raw: abi::envoy_dynamic_module_type_ai_filter_envoy_ptr,
}

impl EnvoyAiFilter {
  /// Create a new EnvoyAiFilter. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `raw` must be the pointer Envoy passed to the current AI filter hook, and the returned value
  /// must not outlive that call.
  #[doc(hidden)]
  pub unsafe fn new(raw: abi::envoy_dynamic_module_type_ai_filter_envoy_ptr) -> Self {
    Self { raw }
  }

  // ---------------------------------------------------------------------------
  // Request payload
  // ---------------------------------------------------------------------------

  /// Read the value `json_pointer` names in the request payload, as JSON text: `"\"gpt-4o\""` for
  /// a string. The value is copied, since Envoy reuses the buffer for the next read.
  pub fn get_request_json(&self, json_pointer: &str) -> Result<String, AiFilterJsonError> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    json_result(unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_get_request_json(
        self.raw,
        crate::str_to_module_buffer(json_pointer),
        &mut result,
      )
    })?;
    let bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
    };
    Ok(String::from_utf8_lossy(bytes).into_owned())
  }

  /// Write `value`, JSON text, where `json_pointer` names in the request payload: replace a value,
  /// add an object member, or append to an array when the last token is `-`. The empty pointer
  /// replaces the whole document, which must stay an object.
  pub fn set_request_json(
    &mut self,
    json_pointer: &str,
    value: &str,
  ) -> Result<(), AiFilterJsonError> {
    json_result(unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_set_request_json(
        self.raw,
        crate::str_to_module_buffer(json_pointer),
        crate::str_to_module_buffer(value),
      )
    })
  }

  /// Remove the object member or array element `json_pointer` names in the request payload.
  pub fn remove_request_json(&mut self, json_pointer: &str) -> Result<(), AiFilterJsonError> {
    json_result(unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_remove_request_json(
        self.raw,
        crate::str_to_module_buffer(json_pointer),
      )
    })
  }

  /// Get the wire API of the request payload as an `envoy.type.ai.v3.LLMProtocol` value name, such
  /// as `OPENAI_CHAT_COMPLETIONS`. Returns `None` when it is not known, or outside
  /// [`AiFilter::on_request`].
  pub fn get_request_protocol(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_get_request_protocol(self.raw, &mut result)
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  // ---------------------------------------------------------------------------
  // Request headers
  // ---------------------------------------------------------------------------

  /// Get the first value of the request header with the given key.
  pub fn get_request_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self
      .get_request_header_value(key, 0)
      .map(|(value, _)| value)
  }

  /// Get the request header value with the given key at the given value index. Returns
  /// `Some((value, total_count))`, where `total_count` is the number of values for the key, or
  /// `None` when the header does not exist at the index.
  pub fn get_request_header_value(
    &self,
    key: &str,
    index: usize,
  ) -> Option<(EnvoyBuffer<'_>, usize)> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    let mut total_count: usize = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_get_request_header_value(
        self.raw,
        crate::str_to_module_buffer(key),
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

  /// Set the request header with the given key, replacing any existing values. Returns `false`
  /// when the key is empty.
  ///
  /// The request headers are held back until every AI filter has finished, so the header reaches
  /// the upstream. Changing a header the route was selected on does not select another route.
  pub fn set_request_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_set_request_header(
        self.raw,
        crate::str_to_module_buffer(key),
        crate::bytes_to_module_buffer(value),
      )
    }
  }

  /// Remove every value of the request header with the given key. Returns `false` when the key is
  /// empty; removing a key that does not exist succeeds.
  pub fn remove_request_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_remove_request_header(
        self.raw,
        crate::str_to_module_buffer(key),
      )
    }
  }

  // ---------------------------------------------------------------------------
  // Filter state
  // ---------------------------------------------------------------------------

  /// Get the bytes value of the filter state object with the given key. Only objects stored as
  /// string accessors, such as the ones [`Self::set_filter_state_bytes`] creates, are readable.
  pub fn get_filter_state_bytes(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_get_filter_state_bytes(
        self.raw,
        crate::str_to_module_buffer(key),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Store a bytes value as the filter state object with the given key, replacing an object
  /// stored with the same key. Returns `false` when the key is empty.
  pub fn set_filter_state_bytes(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(
        self.raw,
        crate::str_to_module_buffer(key),
        crate::bytes_to_module_buffer(value),
      )
    }
  }

  // ---------------------------------------------------------------------------
  // Local reply
  // ---------------------------------------------------------------------------

  /// Reject the request with a local reply; return from [`AiFilter::on_request`] right after.
  /// Returns `false` outside [`AiFilter::on_request`], once a reply was sent, or when
  /// `status_code` is not between 200 and 599.
  pub fn send_local_reply(&mut self, status_code: u32, body: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_ai_filter_send_local_reply(
        self.raw,
        status_code,
        crate::bytes_to_module_buffer(body),
      )
    }
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_ai_filter_config_new(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_ai_filter_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> *const c_void {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    envoy_dynamic_module_on_ai_filter_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_AI_FILTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_AI_FILTER_CONFIG_FUNCTION must be set"),
    )
  }
  on_panic = ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_ai_filter_config_new`].
pub fn envoy_dynamic_module_on_ai_filter_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &crate::NewAiFilterConfigFunction,
) -> *const c_void {
  match new_fn(name, config) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_ai_filter_config_destroy(config_ptr: *const c_void) {
    crate::drop_wrapped_c_void_ptr!(config_ptr, AiFilterConfig);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_ai_filter_new(
    config_ptr: abi::envoy_dynamic_module_type_ai_filter_config_module_ptr,
    filter_envoy_ptr: abi::envoy_dynamic_module_type_ai_filter_envoy_ptr,
  ) -> *const c_void {
    // The configuration is shared by all worker threads and is only ever borrowed immutably here,
    // which is why `AiFilterConfig` requires `Send + Sync`.
    let config = &*(config_ptr as *const Box<dyn AiFilterConfig>);
    let mut envoy_filter = unsafe { EnvoyAiFilter::new(filter_envoy_ptr) };
    match config.new_ai_filter(&mut envoy_filter) {
      Some(filter) => crate::wrap_into_c_void_ptr!(filter),
      None => ptr::null(),
    }
  }
  // A null filter skips the stream rather than failing it.
  on_panic = ptr::null()
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_ai_filter_destroy(filter_ptr: *const c_void) {
    crate::drop_wrapped_c_void_ptr!(filter_ptr, AiFilter);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_ai_filter_request(
    filter_envoy_ptr: abi::envoy_dynamic_module_type_ai_filter_envoy_ptr,
    filter_ptr: abi::envoy_dynamic_module_type_ai_filter_module_ptr,
  ) -> abi::envoy_dynamic_module_type_on_ai_filter_request_status {
    // SAFETY: Envoy calls the hooks of one filter from a single thread and never reentrantly, so
    // this is the only live reference to the box for the duration of the call.
    let filter = &mut *(filter_ptr as *mut Box<dyn AiFilter>);
    let mut envoy_filter = unsafe { EnvoyAiFilter::new(filter_envoy_ptr) };
    filter.on_request(&mut envoy_filter)
  }
  // A request the module did not finish inspecting must not reach the upstream.
  on_panic = abi::envoy_dynamic_module_type_on_ai_filter_request_status::Error
}
