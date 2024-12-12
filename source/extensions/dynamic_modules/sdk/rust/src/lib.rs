#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

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
/// fn my_new_http_filter_config_fn(
///   _envoy_filter_config: EnvoyHttpFilterConfig,
///   _name: &str,
///   _config: &str,
/// ) -> Option<Box<dyn HttpFilterConfig>> {
///   Some(Box::new(MyHttpFilterConfig {}))
/// }
///
/// struct MyHttpFilterConfig {}
///
/// impl HttpFilterConfig for MyHttpFilterConfig {}
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
pub type NewHttpFilterConfigFunction = fn(
  envoy_filter_config: EnvoyHttpFilterConfig,
  name: &str,
  config: &str,
) -> Option<Box<dyn HttpFilterConfig>>;

/// The global init function for HTTP filter configurations. This is set via the
/// `declare_init_functions` macro, and is not intended to be set directly.
pub static NEW_HTTP_FILTER_CONFIG_FUNCTION: OnceLock<NewHttpFilterConfigFunction> = OnceLock::new();

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpFilterConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// dropped when the corresponding Envoy Http filter config is destroyed. Therefore, the
/// imlementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
pub trait HttpFilterConfig {
  /// This is called when a HTTP filter chain is created for a new stream.
  fn new_http_filter(&self, _envoy: EnvoyHttpFilterConfig) -> Box<dyn HttpFilter> {
    panic!("not implemented");
  }
}

/// The trait that corresponds to an Envoy Http filter for each stream
/// created via the [`HttpFilterConfig::new_http_filter`] method.
pub trait HttpFilter {
  /// This is called when the request headers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_headers_status`] to
  /// indicate the status of the request headers processing.
  fn on_request_headers(
    &mut self,
    _envoy_filter: EnvoyHttpFilter,
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
    _envoy_filter: EnvoyHttpFilter,
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
    _envoy_filter: EnvoyHttpFilter,
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
    _envoy_filter: EnvoyHttpFilter,
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
    _envoy_filter: EnvoyHttpFilter,
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
    _envoy_filter: EnvoyHttpFilter,
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

/// An opaque object that represents the underlying Envoy Http filter. This has one to one mapping
/// with the Envoy Http filter object as well as [`HttpFilter`] object per HTTP stream.
///
/// This is a shallow wrapper around the raw pointer to the Envoy HTTP filter object, and it can be
/// copied and used up until the corresponding [`HttpFilter`] is dropped.
pub struct EnvoyHttpFilter {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
}

impl EnvoyHttpFilter {
  // TODO: add methods like getters for headers, etc.
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
  ($ptr:expr, $t:ident) => {{
    let config = $ptr as *mut *mut dyn $t;

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
  new_fn: &NewHttpFilterConfigFunction,
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
  drop_wrapped_c_void_ptr!(config_ptr, HttpFilterConfig);
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
    let raw = filter_config_ptr as *mut *mut dyn HttpFilterConfig;
    &**raw
  };
  envoy_dynamic_module_on_http_filter_new_impl(envoy_filter_config, filter_config)
}

fn envoy_dynamic_module_on_http_filter_new_impl(
  envoy_filter_config: EnvoyHttpFilterConfig,
  filter_config: &dyn HttpFilterConfig,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let filter = filter_config.new_http_filter(envoy_filter_config);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  drop_wrapped_c_void_ptr!(filter_ptr, HttpFilter);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_request_headers(EnvoyHttpFilter { raw_ptr: envoy_ptr }, end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_request_body(EnvoyHttpFilter { raw_ptr: envoy_ptr }, end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_request_trailers(EnvoyHttpFilter { raw_ptr: envoy_ptr })
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_response_headers(EnvoyHttpFilter { raw_ptr: envoy_ptr }, end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_response_body(EnvoyHttpFilter { raw_ptr: envoy_ptr }, end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;
  let filter = &mut **filter;
  filter.on_response_trailers(EnvoyHttpFilter { raw_ptr: envoy_ptr })
}
