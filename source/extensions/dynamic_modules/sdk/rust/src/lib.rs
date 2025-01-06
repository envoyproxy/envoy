#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

pub mod buffer;
pub use buffer::EnvoyBuffer;
use mockall::predicate::*;
use mockall::*;

#[cfg(test)]
#[path = "./lib_test.rs"]
mod mod_test;

use std::sync::OnceLock;

/// This module contains the generated bindings for the envoy dynamic modules ABI.
///
/// This is not meant to be used directly.
pub mod abi {
  include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

/// Declare the init functions for the dynamic module.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is
/// loaded.
///
/// The second argument has [`NewHttpFilterConfigFunction`] type, and it is called when the new HTTP
/// filter configuration is created.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// declare_init_functions!(my_program_init, my_new_http_filter_config_fn);
///
/// fn my_program_init() -> bool {
///   true
/// }
///
/// fn my_new_http_filter_config_fn<EHF: EnvoyHttpFilter>(
///   _envoy_filter_config: EnvoyHttpFilterConfig,
///   _name: &str,
///   _config: &str,
/// ) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
///   Some(Box::new(MyHttpFilterConfig {}))
/// }
///
/// struct MyHttpFilterConfig {}
///
/// impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for MyHttpFilterConfig {}
/// ```
#[macro_export]
macro_rules! declare_init_functions {
  ($f:ident, $new_http_filter_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION
        .get_or_init(|| $new_http_filter_config_fn);
      if ($f()) {
        envoy_proxy_dynamic_modules_rust_sdk::abi::kAbiVersion.as_ptr()
          as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
}

/// The function signature for the program init function.
///
/// This is called when the dynamic module is loaded, and it must return true on success, and false
/// on failure. When it returns false, the dynamic module will not be loaded.
///
/// This is useful to perform any process-wide initialization that the dynamic module needs.
pub type ProgramInitFunction = fn() -> bool;

/// The function signature for the new HTTP filter configuration function.
///
/// This is called when a new HTTP filter configuration is created, and it must return a new
/// instance of the [`HttpFilterConfig`] object. Returning `None` will cause the HTTP filter
/// configuration to be rejected.
//
// TODO(@mathetake): I guess there would be a way to avoid the use of dyn in the first place.
// E.g. one idea is to accept all concrete type parameters for HttpFilterConfig and HttpFilter
// traits in declare_init_functions!, and generate the match statement based on that.
pub type NewHttpFilterConfigFunction<EHF> = fn(
  envoy_filter_config: EnvoyHttpFilterConfig,
  name: &str,
  config: &str,
) -> Option<Box<dyn HttpFilterConfig<EHF>>>;

/// The global init function for HTTP filter configurations. This is set via the
/// `declare_init_functions` macro, and is not intended to be set directly.
pub static NEW_HTTP_FILTER_CONFIG_FUNCTION: OnceLock<
  NewHttpFilterConfigFunction<EnvoyHttpFilterImpl>,
> = OnceLock::new();

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpFilterConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// dropped when the corresponding Envoy Http filter config is destroyed. Therefore, the
/// imlementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
pub trait HttpFilterConfig<EHF: EnvoyHttpFilter> {
  /// This is called when a HTTP filter chain is created for a new stream.
  fn new_http_filter(&self, _envoy: EnvoyHttpFilterConfig) -> Box<dyn HttpFilter<EHF>> {
    panic!("not implemented");
  }
}

/// The trait that corresponds to an Envoy Http filter for each stream
/// created via the [`HttpFilterConfig::new_http_filter`] method.
///
/// All the event hooks are called on the same thread as the one that the [`HttpFilter`] is created
/// via the [`HttpFilterConfig::new_http_filter`] method. In other words, the [`HttpFilter`] object
/// is thread-local.
pub trait HttpFilter<EHF: EnvoyHttpFilter> {
  /// This is called when the request headers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_headers_status`] to
  /// indicate the status of the request headers processing.
  fn on_request_headers(
    &mut self,
    _envoy_filter: EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  /// This is called when the request body is received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_body_status`] to
  /// indicate the status of the request body processing.
  fn on_request_body(
    &mut self,
    _envoy_filter: EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  /// This is called when the request trailers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status`] to
  /// indicate the status of the request trailers processing.
  fn on_request_trailers(
    &mut self,
    _envoy_filter: EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  /// This is called when the response headers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_headers_status`] to
  /// indicate the status of the response headers processing.
  fn on_response_headers(
    &mut self,
    _envoy_filter: EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  /// This is called when the response body is received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_body_status`] to
  /// indicate the status of the response body processing.
  fn on_response_body(
    &mut self,
    _envoy_filter: EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  /// This is called when the response trailers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status`] to
  /// indicate the status of the response trailers processing.
  fn on_response_trailers(
    &mut self,
    _envoy_filter: EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}

/// An opaque object that represents the underlying Envoy Http filter config. This has one to one
/// mapping with the Envoy Http filter config object as well as [`HttpFilterConfig`] object.
///
/// This is a shallow wrapper around the raw pointer to the Envoy HTTP filter config object, and it
/// can be copied and used up until the corresponding [`HttpFilterConfig`] is dropped.
//
// TODO(@mathetake): make this only avaialble for non-test code, and provide a mock for testing so
// that users can write unit tests for their HttpFilterConfig implementations.
#[derive(Debug, Clone, Copy)]
pub struct EnvoyHttpFilterConfig {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
}

impl EnvoyHttpFilterConfig {
  // TODO: add methods like defining metrics, etc.
}

/// An opaque object that represents the underlying Envoy Http filter. This has one to one
/// mapping with the Envoy Http filter object as well as [`HttpFilter`] object per HTTP stream.
///
/// The Envoy filter object is inherently not thread-safe, and it is always recommended to
/// access it from the same thread as the one that [`HttpFilter`] even hooks are called.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyHttpFilter {
  /// Get the value of the request header with the given key.
  /// If the header is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_request_header_values`] variant.
  fn get_request_header_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the request header with the given key.
  ///
  /// If the header is not found, this returns an empty vector.
  fn get_request_header_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all request headers.
  ///
  /// Returns a list of key-value pairs of the request headers.
  /// If there are no headers or headers are not available, this returns an empty list.
  fn get_request_headers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the request header with the given key and value.
  ///
  /// This will overwrite the existing value if the header is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the header is set successfully.
  fn set_request_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Get the value of the request trailer with the given key.
  /// If the trailer is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_request_trailer_values`] variant.
  fn get_request_trailer_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the request trailer with the given key.
  ///
  /// If the trailer is not found, this returns an empty vector.
  fn get_request_trailer_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all request trailers.
  ///
  /// Returns a list of key-value pairs of the request trailers.
  /// If there are no trailers or trailers are not available, this returns an empty list.
  fn get_request_trailers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the request trailer with the given key and value.
  ///
  /// This will overwrite the existing value if the trailer is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the trailer is set successfully.
  fn set_request_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Get the value of the response header with the given key.
  /// If the header is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_response_header_values`] variant.
  fn get_response_header_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the response header with the given key.
  ///
  /// If the header is not found, this returns an empty vector.
  fn get_response_header_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all response headers.
  ///
  /// Returns a list of key-value pairs of the response headers.
  /// If there are no headers or headers are not available, this returns an empty list.
  fn get_response_headers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the response header with the given key and value.
  ///
  /// This will overwrite the existing value if the header is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the header is set successfully.
  fn set_response_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Get the value of the response trailer with the given key.
  /// If the trailer is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_response_trailer_values`] variant.
  fn get_response_trailer_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the response trailer with the given key.
  ///
  /// If the trailer is not found, this returns an empty vector.
  fn get_response_trailer_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;
  /// Get all response trailers.
  ///
  /// Returns a list of key-value pairs of the response trailers.
  /// If there are no trailers or trailers are not available, this returns an empty list.
  fn get_response_trailers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the response trailer with the given key and value.
  ///
  /// This will overwrite the existing value if the trailer is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the operation is successful.
  fn set_response_trailer(&mut self, key: &str, value: &[u8]) -> bool;
}

/// This implements the [`EnvoyHttpFilter`] trait with the given raw pointer to the Envoy HTTP
/// filter object.
///
/// This is not meant to be used directly.
pub struct EnvoyHttpFilterImpl {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
}

impl EnvoyHttpFilter for EnvoyHttpFilterImpl {
  fn get_request_header_value(&self, key: &str) -> Option<EnvoyBuffer> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_request_header,
    )
  }

  fn get_request_header_values(&self, key: &str) -> Vec<EnvoyBuffer> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_request_header,
    )
  }

  fn get_request_headers(&self) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    self.get_headers_impl(
      abi::envoy_dynamic_module_callback_http_get_request_headers_count,
      abi::envoy_dynamic_module_callback_http_get_request_headers,
    )
  }

  fn set_request_header(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_request_header(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }

  fn get_request_trailer_value(&self, key: &str) -> Option<EnvoyBuffer> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_request_trailer,
    )
  }

  fn get_request_trailer_values(&self, key: &str) -> Vec<EnvoyBuffer> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_request_trailer,
    )
  }

  fn get_request_trailers(&self) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    self.get_headers_impl(
      abi::envoy_dynamic_module_callback_http_get_request_trailers_count,
      abi::envoy_dynamic_module_callback_http_get_request_trailers,
    )
  }

  fn set_request_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_request_trailer(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }

  fn get_response_header_value(&self, key: &str) -> Option<EnvoyBuffer> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_response_header,
    )
  }

  fn get_response_header_values(&self, key: &str) -> Vec<EnvoyBuffer> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_response_header,
    )
  }

  fn get_response_headers(&self) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    self.get_headers_impl(
      abi::envoy_dynamic_module_callback_http_get_response_headers_count,
      abi::envoy_dynamic_module_callback_http_get_response_headers,
    )
  }

  fn set_response_header(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_response_header(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }


  fn get_response_trailer_value(&self, key: &str) -> Option<EnvoyBuffer> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_response_trailer,
    )
  }

  fn get_response_trailer_values(&self, key: &str) -> Vec<EnvoyBuffer> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_callback_http_get_response_trailer,
    )
  }

  fn get_response_trailers(&self) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    self.get_headers_impl(
      abi::envoy_dynamic_module_callback_http_get_response_trailers_count,
      abi::envoy_dynamic_module_callback_http_get_response_trailers,
    )
  }

  fn set_response_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_response_trailer(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }
}

impl EnvoyHttpFilterImpl {
  fn new(raw_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self { raw_ptr }
  }

  /// Implement the common logic for getting all headers/trailers.
  fn get_headers_impl(
    &self,
    count_callback: unsafe extern "C" fn(
      filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
    ) -> usize,
    getter_callback: unsafe extern "C" fn(
      filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
      result_buffer_ptr: *mut abi::envoy_dynamic_module_type_http_header,
    ) -> bool,
  ) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    let count = unsafe { count_callback(self.raw_ptr) };
    let mut headers: Vec<(EnvoyBuffer, EnvoyBuffer)> = Vec::with_capacity(count);
    let success = unsafe {
      getter_callback(
        self.raw_ptr,
        headers.as_mut_ptr() as *mut abi::envoy_dynamic_module_type_http_header,
      )
    };
    unsafe {
      headers.set_len(count);
    }
    if success {
      headers
    } else {
      Vec::default()
    }
  }

  /// This implements the common logic for getting the header/trailer values.
  fn get_header_value_impl(
    &self,
    key: &str,
    callback: unsafe extern "C" fn(
      filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
      key: abi::envoy_dynamic_module_type_buffer_module_ptr,
      key_length: usize,
      result_buffer_ptr: *mut abi::envoy_dynamic_module_type_buffer_envoy_ptr,
      result_buffer_length_ptr: *mut usize,
      index: usize,
    ) -> usize,
  ) -> Option<EnvoyBuffer> {
    let key_ptr = key.as_ptr();
    let key_size = key.len();

    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;

    unsafe {
      callback(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
        0, // Only the first value is needed.
      )
    };

    if result_ptr.is_null() {
      None
    } else {
      Some(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) })
    }
  }

  /// This implements the common logic for getting the header/trailer values.
  ///
  /// TODO: use smallvec or similar to avoid the heap allocations for majority of the cases.
  fn get_header_values_impl(
    &self,
    key: &str,
    callback: unsafe extern "C" fn(
      filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
      key: abi::envoy_dynamic_module_type_buffer_module_ptr,
      key_length: usize,
      result_buffer_ptr: *mut abi::envoy_dynamic_module_type_buffer_envoy_ptr,
      result_buffer_length_ptr: *mut usize,
      index: usize,
    ) -> usize,
  ) -> Vec<EnvoyBuffer> {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;

    // Get the first value to get the count.
    let counts = unsafe {
      callback(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
        0,
      )
    };

    let mut results = Vec::new();
    if counts == 0 {
      return results;
    }

    // At this point, we assume at least one value is present.
    results.push(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) });
    // So, we iterate from 1 to counts - 1.
    for i in 1 .. counts {
      let mut result_ptr: *const u8 = std::ptr::null();
      let mut result_size: usize = 0;
      unsafe {
        callback(
          self.raw_ptr,
          key_ptr as *const _ as *mut _,
          key_size,
          &mut result_ptr as *mut _ as *mut _,
          &mut result_size as *mut _ as *mut _,
          i,
        )
      };
      // Within the range, all results are guaranteed to be non-null by Envoy.
      results.push(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) });
    }
    results
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_new(
  envoy_filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  name_ptr: *const u8,
  name_size: usize,
  config_ptr: *const u8,
  config_size: usize,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
  // This assumes that the name and config are valid UTF-8 strings. Should we relax? At the moment,
  // both are String at protobuf level.
  let name =
    std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_size)).unwrap_or_default();
  let config =
    std::str::from_utf8(std::slice::from_raw_parts(config_ptr, config_size)).unwrap_or_default();

  let envoy_filter_config = EnvoyHttpFilterConfig {
    raw_ptr: envoy_filter_config_ptr,
  };

  envoy_dynamic_module_on_http_filter_config_new_impl(
    envoy_filter_config,
    name,
    config,
    NEW_HTTP_FILTER_CONFIG_FUNCTION
      .get()
      .expect("NEW_HTTP_FILTER_CONFIG_FUNCTION must be set"),
  )
}

/// We wrap the Box<dyn T> in another Box to be able to pass the address of the Box to C, and
/// retrieve it back when the C code calls the destroy function via [`drop_wrapped_c_void_ptr!`].
/// This is necessary because the Box<dyn T> is a fat pointer, and we can't pass it directly.
/// See https://users.rust-lang.org/t/sending-a-boxed-trait-over-ffi/21708 for the exact problem.
//
// Implementation note: this can be a simple function taking a type parameter, but we have it as
// a macro to align with the other macro drop_wrapped_c_void_ptr!.
macro_rules! wrap_into_c_void_ptr {
  ($t:expr) => {{
    let boxed = Box::new($t);
    Box::into_raw(boxed) as *const ::std::os::raw::c_void
  }};
}

/// This macro is used to drop the Box<dyn T> and the underlying object when the C code calls the
/// destroy function. This is a counterpart to [`wrap_into_c_void_ptr!`].
//
// Implementation note: this cannot be a function as we need to cast as *mut *mut dyn T which is
// not feasible via usual function type params.
macro_rules! drop_wrapped_c_void_ptr {
  ($ptr:expr, $trait_:ident < $($args:ident),* $(,)* >) => {{
    let config = $ptr as *mut *mut dyn $trait_<$($args)*>;

    // Drop the Box<*mut $t>, and then the Box<$t>, which also
    // drops the underlying object.
    unsafe {
      let _outer = Box::from_raw(config);
      let _inner = Box::from_raw(*config);
    }
  }};
}

fn envoy_dynamic_module_on_http_filter_config_new_impl(
  envoy_filter_config: EnvoyHttpFilterConfig,
  name: &str,
  config: &str,
  new_fn: &NewHttpFilterConfigFunction<EnvoyHttpFilterImpl>,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
  if let Some(config) = new_fn(envoy_filter_config, name, config) {
    wrap_into_c_void_ptr!(config)
  } else {
    std::ptr::null()
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_ptr, HttpFilterConfig<EnvoyHttpFilterImpl>);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let envoy_filter_config = EnvoyHttpFilterConfig {
    raw_ptr: filter_envoy_ptr,
  };
  let filter_config = {
    let raw = filter_config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    &**raw
  };
  envoy_dynamic_module_on_http_filter_new_impl(envoy_filter_config, filter_config)
}

fn envoy_dynamic_module_on_http_filter_new_impl(
  envoy_filter_config: EnvoyHttpFilterConfig,
  filter_config: &dyn HttpFilterConfig<EnvoyHttpFilterImpl>,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let filter = filter_config.new_http_filter(envoy_filter_config);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  drop_wrapped_c_void_ptr!(filter_ptr, HttpFilter<EnvoyHttpFilterImpl>);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_headers(EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_body(EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_trailers(EnvoyHttpFilterImpl::new(envoy_ptr))
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_headers(EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_body(EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_trailers(EnvoyHttpFilterImpl::new(envoy_ptr))
}
