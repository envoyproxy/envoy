#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

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

fn envoy_dynamic_module_on_http_filter_config_new_impl(
  envoy_filter_config: EnvoyHttpFilterConfig,
  name: &str,
  config: &str,
  new_fn: &NewHttpFilterConfigFunction,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
  if let Some(config) = new_fn(envoy_filter_config, name, config) {
    let boxed_filter_config_ptr = Box::into_raw(Box::new(config));
    boxed_filter_config_ptr as abi::envoy_dynamic_module_type_http_filter_config_module_ptr
  } else {
    std::ptr::null()
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
) {
  let config = config_ptr as *mut *mut dyn HttpFilterConfig;

  // Drop the Box<*mut dyn HttpFilterConfig>, and then the Box<dyn HttpFilterConfig>, which also
  // drops the underlying object.
  let _outer = Box::from_raw(config);
  let _inner = Box::from_raw(*config);
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
  let boxed = Box::into_raw(Box::new(filter));
  boxed as abi::envoy_dynamic_module_type_http_filter_module_ptr
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter;

  // Drop the Box<*mut dyn HttpFilter>, and then the Box<dyn HttpFilter>, which also drops the
  // underlying object.
  let _outer = Box::from_raw(filter);
  let _inner = Box::from_raw(*filter);
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

#[cfg(test)]
mod tests {
  use super::*;
  use std::sync::atomic::AtomicBool; // This is used for testing the drop, not for the actual concurrency.

  #[test]
  fn test_envoy_dynamic_module_on_http_filter_config_new_impl() {
    struct TestHttpFilterConfig;
    impl HttpFilterConfig for TestHttpFilterConfig {}

    let envoy_filter_config = EnvoyHttpFilterConfig {
      raw_ptr: std::ptr::null_mut(),
    };
    let mut new_fn: NewHttpFilterConfigFunction = |_, _, _| Some(Box::new(TestHttpFilterConfig));
    let result = envoy_dynamic_module_on_http_filter_config_new_impl(
      envoy_filter_config,
      "test_name",
      "test_config",
      &new_fn,
    );
    assert!(!result.is_null());

    unsafe {
      envoy_dynamic_module_on_http_filter_config_destroy(result);
    }

    // None should result in null pointer.
    new_fn = |_, _, _| None;
    let result = envoy_dynamic_module_on_http_filter_config_new_impl(
      envoy_filter_config,
      "test_name",
      "test_config",
      &new_fn,
    );
    assert!(result.is_null());
  }

  #[test]
  fn test_envoy_dynamic_module_on_http_filter_config_destroy() {
    // This test is mainly to check if the drop is called correctly after wrapping/unwrapping the
    // Box.
    static DROPPED: AtomicBool = AtomicBool::new(false);
    struct TestHttpFilterConfig;
    impl HttpFilterConfig for TestHttpFilterConfig {}
    impl Drop for TestHttpFilterConfig {
      fn drop(&mut self) {
        DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
      }
    }

    // This is a sort of round-trip to ensure the same control flow as the actual usage.
    let new_fn: NewHttpFilterConfigFunction = |_, _, _| Some(Box::new(TestHttpFilterConfig));
    let config_ptr = envoy_dynamic_module_on_http_filter_config_new_impl(
      EnvoyHttpFilterConfig {
        raw_ptr: std::ptr::null_mut(),
      },
      "test_name",
      "test_config",
      &new_fn,
    );

    unsafe {
      envoy_dynamic_module_on_http_filter_config_destroy(config_ptr);
    }
    // Now that the drop is called, DROPPED must be set to true.
    assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
  }

  #[test]
  // This tests the round-trip of the envoy_dynamic_module_on_http_filter_new_impl and
  // envoy_dynamic_module_on_http_filter_destroy.
  fn test_envoy_dynamic_module_on_http_filter_new_destroy() {
    static DROPPED: AtomicBool = AtomicBool::new(false);
    struct TestHttpFilterConfig;
    impl HttpFilterConfig for TestHttpFilterConfig {
      fn new_http_filter(&self, _envoy: EnvoyHttpFilterConfig) -> Box<dyn HttpFilter> {
        Box::new(TestHttpFilter)
      }
    }

    struct TestHttpFilter;
    impl HttpFilter for TestHttpFilter {}
    impl Drop for TestHttpFilter {
      fn drop(&mut self) {
        DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
      }
    }

    let filter_config = TestHttpFilterConfig;
    let result = envoy_dynamic_module_on_http_filter_new_impl(
      EnvoyHttpFilterConfig {
        raw_ptr: std::ptr::null_mut(),
      },
      &filter_config,
    );
    assert!(!result.is_null());

    unsafe {
      envoy_dynamic_module_on_http_filter_destroy(result);
    }

    assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
  }

  #[test]
  // This tests all the on_* methods on the HttpFilter trait through the actual entry points.
  fn test_envoy_dynamic_module_on_http_filter_callbacks() {
    struct TestHttpFilterConfig;
    impl HttpFilterConfig for TestHttpFilterConfig {
      fn new_http_filter(&self, _envoy: EnvoyHttpFilterConfig) -> Box<dyn HttpFilter> {
        Box::new(TestHttpFilter)
      }
    }

    static ON_REQUEST_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
    static ON_REQUEST_BODY_CALLED: AtomicBool = AtomicBool::new(false);
    static ON_REQUEST_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);
    static ON_RESPONSE_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
    static ON_RESPONSE_BODY_CALLED: AtomicBool = AtomicBool::new(false);
    static ON_RESPONSE_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);

    struct TestHttpFilter;
    impl HttpFilter for TestHttpFilter {
      fn on_request_headers(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
        _end_of_stream: bool,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
        ON_REQUEST_HEADERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
      }

      fn on_request_body(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
        _end_of_stream: bool,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
        ON_REQUEST_BODY_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
      }

      fn on_request_trailers(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
        ON_REQUEST_TRAILERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
      }

      fn on_response_headers(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
        _end_of_stream: bool,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
        ON_RESPONSE_HEADERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
      }

      fn on_response_body(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
        _end_of_stream: bool,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
        ON_RESPONSE_BODY_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
      }

      fn on_response_trailers(
        &mut self,
        _envoy_filter: EnvoyHttpFilter,
      ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
        ON_RESPONSE_TRAILERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
        abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
      }
    }

    let filter_config = TestHttpFilterConfig;
    let filter = envoy_dynamic_module_on_http_filter_new_impl(
      EnvoyHttpFilterConfig {
        raw_ptr: std::ptr::null_mut(),
      },
      &filter_config,
    );

    unsafe {
      assert_eq!(
        envoy_dynamic_module_on_http_filter_request_headers(std::ptr::null_mut(), filter, false),
        abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
      );
      assert_eq!(
        envoy_dynamic_module_on_http_filter_request_body(std::ptr::null_mut(), filter, false),
        abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
      );
      assert_eq!(
        envoy_dynamic_module_on_http_filter_request_trailers(std::ptr::null_mut(), filter),
        abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
      );
      assert_eq!(
        envoy_dynamic_module_on_http_filter_response_headers(std::ptr::null_mut(), filter, false),
        abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
      );
      assert_eq!(
        envoy_dynamic_module_on_http_filter_response_body(std::ptr::null_mut(), filter, false),
        abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
      );
      assert_eq!(
        envoy_dynamic_module_on_http_filter_response_trailers(std::ptr::null_mut(), filter),
        abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
      );
      envoy_dynamic_module_on_http_filter_destroy(filter);
    }

    assert!(ON_REQUEST_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
    assert!(ON_REQUEST_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
    assert!(ON_REQUEST_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
    assert!(ON_RESPONSE_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
    assert!(ON_RESPONSE_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
    assert!(ON_RESPONSE_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  }
}
