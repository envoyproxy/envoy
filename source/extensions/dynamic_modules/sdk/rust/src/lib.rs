#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

pub mod buffer;
pub use buffer::{EnvoyBuffer, EnvoyMutBuffer};
use mockall::predicate::*;
use mockall::*;

#[cfg(test)]
#[path = "./lib_test.rs"]
mod mod_test;

use crate::abi::envoy_dynamic_module_type_metrics_result;
use std::any::Any;
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
/// fn my_new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
///   _envoy_filter_config: &mut EC,
///   _name: &str,
///   _config: &[u8],
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
  ($f:ident, $new_http_filter_config_fn:expr, $new_http_filter_per_route_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION
        .get_or_init(|| $new_http_filter_config_fn);
      envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION
        .get_or_init(|| $new_http_filter_per_route_config_fn);
      if ($f()) {
        envoy_proxy_dynamic_modules_rust_sdk::abi::kAbiVersion.as_ptr()
          as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
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

/// Log a trace message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_trace {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Trace, $($arg)*)
    };
}

/// Log a debug message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_debug {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Debug, $($arg)*)
    };
}

/// Log an info message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_info {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Info, $($arg)*)
    };
}

/// Log a warning message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_warn {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Warn, $($arg)*)
    };
}

/// Log an error message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_error {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Error, $($arg)*)
    };
}

/// Log a critical message to Envoy's logging system with [dynamic_modules] Id. Messages won't be
/// allocated if the log level is not enabled on the Envoy side.
///
/// This accepts the exact same arguments as the `format!` macro, so you can use it to log formatted
/// messages.
#[macro_export]
macro_rules! envoy_log_critical {
    ($($arg:tt)*) => {
        $crate::envoy_log!($crate::abi::envoy_dynamic_module_type_log_level::Critical, $($arg)*)
    };
}

/// Internal logging macro that handles the actual call to the Envoy logging callback
/// used by envoy_log_* macros.
#[macro_export]
macro_rules! envoy_log {
  ($level:expr, $($arg:tt)*) => {
    {
      #[cfg(not(test))]
      unsafe {
        // Avoid allocating the message if the log level is not enabled.
        if $crate::abi::envoy_dynamic_module_callback_log_enabled($level) {
          let message = format!($($arg)*);
          let message_bytes = message.as_bytes();
          $crate::abi::envoy_dynamic_module_callback_log(
            $level,
            message_bytes.as_ptr() as *const ::std::os::raw::c_char,
            message_bytes.len()
          );
        }
      }
      // In unit tests, just print to stderr since the Envoy symbols are not available.
      #[cfg(test)]
      {
        let message = format!($($arg)*);
        eprintln!("[{}] {}", stringify!($level), message);
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
pub type NewHttpFilterConfigFunction<EC, EHF> = fn(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>>;

/// The global init function for HTTP filter configurations. This is set via the
/// `declare_init_functions` macro, and is not intended to be set directly.
pub static NEW_HTTP_FILTER_CONFIG_FUNCTION: OnceLock<
  NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl>,
> = OnceLock::new();

/// The function signature for the new HTTP filter per-route configuration function.
///
/// This is called when a new HTTP filter per-route configuration is created. It must return an
/// object representing the filter's per-route configuration. Returning `None` will cause the HTTP
/// filter configuration to be rejected.
/// This config can be later retried by the filter via
/// [`EnvoyHttpFilter::get_most_specific_route_config`] method.
pub type NewHttpFilterPerRouteConfigFunction =
  fn(name: &str, config: &[u8]) -> Option<Box<dyn Any>>;

/// The global init function for HTTP filter per-route configurations. This is set via the
/// `declare_init_functions` macro, and is not intended to be set directly.
pub static NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION: OnceLock<
  NewHttpFilterPerRouteConfigFunction,
> = OnceLock::new();

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpFilterConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// dropped when the corresponding Envoy Http filter config is destroyed. Therefore, the
/// implementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
pub trait HttpFilterConfig<EHF: EnvoyHttpFilter> {
  /// This is called when a HTTP filter chain is created for a new stream.
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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
    _envoy_filter: &mut EHF,
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
    _envoy_filter: &mut EHF,
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
    _envoy_filter: &mut EHF,
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
    _envoy_filter: &mut EHF,
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
    _envoy_filter: &mut EHF,
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
    _envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }

  /// This is called when the stream is complete.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  ///
  /// This is called before this [`HttpFilter`] object is dropped and access logs are flushed.
  fn on_stream_complete(&mut self, _envoy_filter: &mut EHF) {}

  /// This is called when the HTTP callout is done.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `callout_id` is the ID of the callout that was done.
  /// * `result` indicates the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers. This is optional.
  /// * `response_body` is the response body. This is optional.
  fn on_http_callout_done(
    &mut self,
    _envoy_filter: &mut EHF,
    _callout_id: u32,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
  }

  /// This is called when the new event is scheduled via the [`EnvoyHttpFilterScheduler::commit`]
  /// for this [`HttpFilter`].
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyHttpFilterScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyHttpFilter::new_scheduler`] for more details on how to use this.
  fn on_scheduled(&mut self, _envoy_filter: &mut EHF, _event_id: u64) {}

  /// This is called when the downstream buffer size goes above the high watermark for a
  /// terminal filter.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  fn on_downstream_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut EHF) {}

  /// This is called when the downstream buffer size goes below the low watermark for a
  /// terminal filter.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  fn on_downstream_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut EHF) {}
}

/// An opaque object that represents the underlying Envoy Http filter config. This has one to one
/// mapping with the Envoy Http filter config object as well as [`HttpFilterConfig`] object.
pub trait EnvoyHttpFilterConfig {
  /// Define a new counter scoped to this filter config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result>;

  // Define a new counter vec scoped to this filter config with the given name.
  fn define_counter_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this filter config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec scoped to this filter config with the given name.
  fn define_gauge_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this filter config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec scoped to this filter config with the given name.
  fn define_histogram_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result>;
}

pub struct EnvoyHttpFilterConfigImpl {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
}

impl EnvoyHttpFilterConfig for EnvoyHttpFilterConfigImpl {
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_counter(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_counter_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_counter_vec(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_gauge(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_gauge_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_gauge_vec(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_histogram(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn define_histogram_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result> {
    let name_ptr = name.as_ptr();
    let name_size = name.len();
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_histogram_vec(
        self.raw_ptr,
        name_ptr as *const _ as *mut _,
        name_size,
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }
}

/// The identifier for an EnvoyCounter.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyCounterId(usize);

/// The identifier for an EnvoyCounterVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyCounterVecId(usize);

/// The identifier for an EnvoyGauge.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyGaugeId(usize);

/// The identifier for an EnvoyGaugeVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyGaugeVecId(usize);

/// The identifier for an EnvoyHistogram.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyHistogramId(usize);

/// The identifier for an EnvoyHistogramVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EnvoyHistogramVecId(usize);

/// An opaque object that represents the underlying Envoy Http filter. This has one to one
/// mapping with the Envoy Http filter object as well as [`HttpFilter`] object per HTTP stream.
///
/// The Envoy filter object is inherently not thread-safe, and it is always recommended to
/// access it from the same thread as the one that [`HttpFilter`] event hooks are called.
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

  /// Add a new request header with the given key and value.
  ///
  /// This will add a new header even if the header with the same key already exists.
  ///
  /// Returns true if the header is added successfully.
  fn add_request_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the request header with the given key.
  ///
  /// Returns true if the header is removed successfully.
  fn remove_request_header(&mut self, key: &str) -> bool;

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

  /// Add a new request trailer with the given key and value.
  ///
  /// This will add a new trailer even if the trailer with the same key already exists.
  ///
  /// Returns true if the trailer is added successfully.
  fn add_request_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the request trailer with the given key.
  ///
  /// Returns true if the trailer is removed successfully.
  fn remove_request_trailer(&mut self, key: &str) -> bool;

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

  /// Add a new response header with the given key and value.
  ///
  /// This will add a new header even if the header with the same key already exists.
  ///
  /// Returns true if the header is added successfully.
  fn add_response_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the response header with the given key.
  ///
  /// Returns true if the header is removed successfully.
  fn remove_response_header(&mut self, key: &str) -> bool;

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

  /// Add a new response trailer with the given key and value.
  ///
  /// This will add a new trailer even if the trailer with the same key already exists.
  ///
  /// Returns true if the trailer is added successfully.
  fn add_response_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the response trailer with the given key.
  ///
  /// Returns true if the trailer is removed successfully.
  fn remove_response_trailer(&mut self, key: &str) -> bool;

  /// Send a response to the downstream with the given status code, headers, and body.
  ///
  /// The headers are passed as a list of key-value pairs.
  fn send_response<'a>(
    &mut self,
    status_code: u32,
    headers: Vec<(&'a str, &'a [u8])>,
    body: Option<&'a [u8]>,
  );

  /// Send response headers to the downstream, optionally indicating end of stream.
  /// Necessary pseudo headers such as :status should be present.
  ///
  /// The headers are passed as a list of key-value pairs.
  fn send_response_headers<'a>(&mut self, headers: Vec<(&'a str, &'a [u8])>, end_stream: bool);

  /// Send response body data to the downstream, optionally indicating end of stream.
  fn send_response_data<'a>(&mut self, body: &'a [u8], end_stream: bool);

  /// Send response trailers to the downstream. This implicitly ends the stream.
  ///
  /// The trailers are passed as a list of key-value pairs.
  fn send_response_trailers<'a>(&mut self, trailers: Vec<(&'a str, &'a [u8])>);

  /// Get the number-typed metadata value with the given key.
  /// Use the `source` parameter to specify which metadata to use.
  /// If the metadata is not found or is the wrong type, this returns `None`.
  fn get_metadata_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<f64>;

  /// Set the number-typed dynamic metadata value with the given key.
  /// If the namespace is not found, this will create a new namespace.
  ///
  /// Returns true if the operation is successful.
  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64) -> bool;

  /// Get the string-typed metadata value with the given key.
  /// Use the `source` parameter to specify which metadata to use.
  /// If the metadata is not found or is the wrong type, this returns `None`.
  fn get_metadata_string<'a>(
    &'a self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<EnvoyBuffer<'a>>;

  /// Set the string-typed dynamic metadata value with the given key.
  /// If the namespace is not found, this will create a new namespace.
  ///
  /// Returns true if the operation is successful.
  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str) -> bool;

  /// Get the bytes-typed filter state value with the given key.
  /// If the filter state is not found or is the wrong type, this returns `None`.
  fn get_filter_state_bytes<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>>;

  /// Set the bytes-typed filter state value with the given key.
  /// If the filter state is not found, this will create a new filter state.
  ///
  /// Returns true if the operation is successful.
  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool;

  /// Get the received request body (the request body pieces received in the latest event).
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// The body is represented as a list of [`EnvoyBuffer`].
  /// Memory contents pointed by each [`EnvoyBuffer`] is mutable and can be modified in place.
  /// However, the vector itself is a "copied view". For example, adding or removing
  /// [`EnvoyBuffer`] from the vector has no effect on the underlying Envoy buffer. To write beyond
  /// the end of the buffer, use [`EnvoyHttpFilter::append_received_request_body`]. To remove data
  /// from the buffer, use [`EnvoyHttpFilter::drain_received_request_body`].
  ///
  /// To write completely new data, use [`EnvoyHttpFilter::drain_received_request_body`] for the
  /// size of the buffer, and then use [`EnvoyHttpFilter::append_received_request_body`] to write
  /// the new data.
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  ///
  /// // This is the test setup.
  /// let mut envoy_filter = MockEnvoyHttpFilter::default();
  /// // Mutable static storage is used for the test to simulate the response body operation.
  /// static mut BUFFER: [u8; 10] = *b"helloworld";
  /// envoy_filter
  ///   .expect_get_received_request_body()
  ///   .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
  /// envoy_filter
  ///   .expect_drain_received_request_body()
  ///   .return_const(true);
  ///
  ///
  /// // Calculate the size of the new received request body in bytes.
  /// let buffers = envoy_filter.get_received_request_body().unwrap();
  /// let mut size = 0;
  /// for buffer in &buffers {
  ///   size += buffer.as_slice().len();
  /// }
  /// assert_eq!(size, 10);
  ///
  /// // drain the new received request body.
  /// assert!(envoy_filter.drain_received_request_body(10));
  ///
  /// // Now start writing new data from the beginning of the request body.
  /// ```
  ///
  /// This returns None if the request body is not available.
  fn get_received_request_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Similar to [`EnvoyHttpFilter::get_received_request_body`], but returns the buffered request
  /// body (the request body pieces buffered so far in the filter chain).
  fn get_buffered_request_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Get the size of the received request body in bytes.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns None if the request body is not available.
  fn get_received_request_body_size(&mut self) -> Option<usize>;

  /// Similar to [`EnvoyHttpFilter::get_received_request_body_size`], but returns the size of the
  /// buffered request body in bytes.
  fn get_buffered_request_body_size(&mut self) -> Option<usize>;

  /// Drain the given number of bytes from the front of the received request body.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns false if the request body is not available.
  ///
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_received_request_body(&mut self, number_of_bytes: usize) -> bool;

  /// Similar to [`EnvoyHttpFilter::drain_received_request_body`], but drains from the buffered
  /// request body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the request body is not available.
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_buffered_request_body(&mut self, number_of_bytes: usize) -> bool;

  /// Append the given data to the end of the received request body.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns false if the request body is not available.
  ///
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_received_request_body(&mut self, data: &[u8]) -> bool;

  /// Similar to [`EnvoyHttpFilter::append_received_request_body`], but appends to the buffered
  /// request body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the request body is not available.
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_buffered_request_body(&mut self, data: &[u8]) -> bool;

  /// Get the received response body (the response body pieces received in the latest event).
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// The body is represented as a list of
  /// [`EnvoyBuffer`]. Memory contents pointed by each [`EnvoyBuffer`] is mutable and can be
  /// modified in place. However, the buffer itself is immutable. For example, adding or removing
  /// [`EnvoyBuffer`] from the vector has no effect on the underlying Envoy buffer. To write the
  /// contents by changing its length, use [`EnvoyHttpFilter::drain_received_response_body`] or
  /// [`EnvoyHttpFilter::append_received_response_body`].
  ///
  /// To write completely new data, use [`EnvoyHttpFilter::drain_received_response_body`] for the
  /// size of the buffer, and then use [`EnvoyHttpFilter::append_received_response_body`] to write
  /// the new data.
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  ///
  /// // This is the test setup.
  /// let mut envoy_filter = MockEnvoyHttpFilter::default();
  /// // Mutable static storage is used for the test to simulate the response body operation.
  /// static mut BUFFER: [u8; 10] = *b"helloworld";
  /// envoy_filter
  ///   .expect_get_received_response_body()
  ///   .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
  /// envoy_filter
  ///   .expect_drain_received_response_body()
  ///   .return_const(true);
  ///
  ///
  /// // Calculate the size of the received response body in bytes.
  /// let buffers = envoy_filter.get_received_response_body().unwrap();
  /// let mut size = 0;
  /// for buffer in &buffers {
  ///   size += buffer.as_slice().len();
  /// }
  /// assert_eq!(size, 10);
  ///
  /// // drain the received response body.
  /// assert!(envoy_filter.drain_received_response_body(10));
  ///
  /// // Now start writing new data from the beginning of the request body.
  /// ```
  ///
  /// Returns None if the response body is not available.
  fn get_received_response_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Similar to [`EnvoyHttpFilter::get_received_response_body`], but returns the buffered response
  /// body (the response body pieces buffered so far in the filter chain).
  fn get_buffered_response_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Get the size of the received response body in bytes.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns None if the response body is not available.
  fn get_received_response_body_size(&mut self) -> Option<usize>;

  /// Similar to [`EnvoyHttpFilter::get_received_response_body_size`], but returns the size of the
  /// buffered response body in bytes.
  ///
  /// Returns None if the response body is not available.
  fn get_buffered_response_body_size(&mut self) -> Option<usize>;

  /// Drain the given number of bytes from the front of the received response body.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns false if the response body is not available.
  ///
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_received_response_body(&mut self, number_of_bytes: usize) -> bool;

  /// Similar to [`EnvoyHttpFilter::drain_received_response_body`], but drains from the buffered
  /// response body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the response body is not available.
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_buffered_response_body(&mut self, number_of_bytes: usize) -> bool;

  /// Append the given data to the end of the received response body.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns false if the response body is not available.
  ///
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_received_response_body(&mut self, data: &[u8]) -> bool;

  /// Similar to [`EnvoyHttpFilter::append_received_response_body`], but appends to the buffered
  /// response body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the response body is not available.
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_buffered_response_body(&mut self, data: &[u8]) -> bool;

  /// Clear the route cache calculated during a previous phase of the filter chain.
  ///
  /// This is useful when the filter wants to force a re-evaluation of the route selection after
  /// modifying the request headers, etc that affect the routing decision.
  fn clear_route_cache(&mut self);

  /// Get the value of the attribute with the given ID as a string.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  fn get_attribute_string<'a>(
    &'a self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'a>>;

  /// Get the value of the attribute with the given ID as an integer.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<i64>;

  /// Send an HTTP callout to the given cluster with the given headers and body.
  /// Multiple callouts can be made from the same filter. Different callouts can be
  /// distinguished by the `callout_id` parameter.
  ///
  /// Headers must contain the `:method`, ":path", and `host` headers.
  ///
  /// This returns the status of the callout. The meaning of the status is
  ///
  ///   * Success: The callout was sent successfully.
  ///   * MissingRequiredHeaders: One of the required headers is missing: `:method`, `:path`, or
  ///     `host`.
  ///   * ClusterNotFound: The cluster with the given name was not found.
  ///   * DuplicateCalloutId: The callout ID is already in use.
  ///   * CouldNotCreateRequest: The request could not be created. This happens when, for example,
  ///     there's no healthy upstream host in the cluster.
  ///
  /// The callout result will be delivered to the [`HttpFilter::on_http_callout_done`] method.
  fn send_http_callout<'a>(
    &mut self,
    _callout_id: u32,
    _cluster_name: &'a str,
    _headers: Vec<(&'a str, &'a [u8])>,
    _body: Option<&'a [u8]>,
    _timeout_milliseconds: u64,
  ) -> abi::envoy_dynamic_module_type_http_callout_init_result;

  /// Get the most specific route configuration for the current route.
  ///
  /// Returns None if no per-route configuration is present on this route. Otherwise,
  /// returns the most specific per-route configuration (i.e. the one most up along the config
  /// hierarchy) created by the filter.
  fn get_most_specific_route_config(&self) -> Option<std::sync::Arc<dyn Any>>;

  /// This can be called to continue the decoding of the HTTP request when the processing is
  /// stopped.
  ///
  /// For example, this can be used inside the [`HttpFilter::on_http_callout_done`] or
  /// [`HttpFilter::on_scheduled`] methods to continue the decoding of the request body
  /// after the callout or scheduled event is done.
  fn continue_decoding(&mut self);

  /// This is exactly the same as [`EnvoyHttpFilter::continue_decoding`], but it is
  /// used to continue the encoding of the HTTP response.
  fn continue_encoding(&mut self);

  /// Create a new implementation of the [`EnvoyHttpFilterScheduler`] trait.
  ///
  /// ## Example Usage
  ///
  /// ```
  /// use abi::*;
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  /// use std::thread;
  ///
  /// struct TestFilter;
  /// impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for TestFilter {
  ///   fn on_request_headers(
  ///     &mut self,
  ///     envoy_filter: &mut EHF,
  ///     _end_of_stream: bool,
  ///   ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
  ///     let scheduler = envoy_filter.new_scheduler();
  ///     let _ = std::thread::spawn(move || {
  ///       // Do some work in a separate thread.
  ///       // ...
  ///       // Then schedule the event to continue processing.
  ///       scheduler.commit(12345);
  ///     });
  ///     // Stops the iteration and schedules the event from the separate thread.
  ///     envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  ///   }
  ///   fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
  ///     // The event_id should match the one we scheduled.
  ///     assert_eq!(event_id, 12345);
  ///     // Then we can continue processing the request.
  ///     envoy_filter.continue_decoding();
  ///   }
  /// }
  /// ```
  fn new_scheduler(&self) -> impl EnvoyHttpFilterScheduler + 'static;

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increment the counter vec with the given id.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge vec with the given id.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge vec with the given id.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge vec with the given id.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram vec with the given id.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;
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
      abi::envoy_dynamic_module_callback_http_get_request_headers_size,
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

  fn add_request_header(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_request_header(
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
      abi::envoy_dynamic_module_callback_http_get_request_trailers_size,
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

  fn add_request_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_request_trailer(
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
      abi::envoy_dynamic_module_callback_http_get_response_headers_size,
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

  fn add_response_header(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_response_header(
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
      abi::envoy_dynamic_module_callback_http_get_response_trailers_size,
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

  fn add_response_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_response_trailer(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }

  fn send_response(&mut self, status_code: u32, headers: Vec<(&str, &[u8])>, body: Option<&[u8]>) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);

    // Note: Casting a (&str, &[u8]) to an abi::envoy_dynamic_module_type_module_http_header works
    // not because of any formal layout guarantees but because:
    // 1) tuples _in practice_ are laid out packed and in order
    // 2) &str and &[u8] are fat pointers (pointers to DSTs), whose layouts _in practice_ are a
    //    pointer and length
    // If these assumptions change, this will break. (Vec is guaranteed to point to a contiguous
    // array, so it's safe to cast to a pointer)
    let headers_ptr = headers.as_ptr() as *mut abi::envoy_dynamic_module_type_module_http_header;

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response(
        self.raw_ptr,
        status_code,
        headers_ptr,
        headers.len(),
        body_ptr as *mut _,
        body_length,
      )
    }
  }

  fn send_response_headers(&mut self, headers: Vec<(&str, &[u8])>, end_stream: bool) {
    // Note: Casting a (&str, &[u8]) to an abi::envoy_dynamic_module_type_module_http_header works
    // not because of any formal layout guarantees but because:
    // 1) tuples _in practice_ are laid out packed and in order
    // 2) &str and &[u8] are fat pointers (pointers to DSTs), whose layouts _in practice_ are a
    //    pointer and length
    // If these assumptions change, this will break. (Vec is guaranteed to point to a contiguous
    // array, so it's safe to cast to a pointer)
    let headers_ptr = headers.as_ptr() as *mut abi::envoy_dynamic_module_type_module_http_header;

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_headers(
        self.raw_ptr,
        headers_ptr,
        headers.len(),
        end_stream,
      )
    }
  }

  fn send_response_data(&mut self, body: &[u8], end_stream: bool) {
    let body_ptr = body.as_ptr();
    let body_length = body.len();

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_data(
        self.raw_ptr,
        body_ptr as *mut _,
        body_length,
        end_stream,
      )
    }
  }

  fn send_response_trailers(&mut self, trailers: Vec<(&str, &[u8])>) {
    // Note: Casting a (&str, &[u8]) to an abi::envoy_dynamic_module_type_module_http_header works
    // not because of any formal layout guarantees but because:
    // 1) tuples _in practice_ are laid out packed and in order
    // 2) &str and &[u8] are fat pointers (pointers to DSTs), whose layouts _in practice_ are a
    //    pointer and length
    // If these assumptions change, this will break. (Vec is guaranteed to point to a contiguous
    // array, so it's safe to cast to a pointer)
    let trailers_ptr = trailers.as_ptr() as *mut abi::envoy_dynamic_module_type_module_http_header;

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_trailers(
        self.raw_ptr,
        trailers_ptr,
        trailers.len(),
      )
    }
  }

  fn get_metadata_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<f64> {
    let namespace_ptr = namespace.as_ptr();
    let namespace_size = namespace.len();
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let mut value: f64 = 0f64;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_number(
        self.raw_ptr,
        source,
        namespace_ptr as *const _ as *mut _,
        namespace_size,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut value as *mut _ as *mut _,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64) -> bool {
    let namespace_ptr = namespace.as_ptr();
    let namespace_size = namespace.len();
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
        self.raw_ptr,
        namespace_ptr as *const _ as *mut _,
        namespace_size,
        key_ptr as *const _ as *mut _,
        key_size,
        value,
      )
    }
  }

  fn get_metadata_string(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<EnvoyBuffer> {
    let namespace_ptr = namespace.as_ptr();
    let namespace_size = namespace.len();
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_string(
        self.raw_ptr,
        source,
        namespace_ptr as *const _ as *mut _,
        namespace_size,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) })
    } else {
      None
    }
  }

  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str) -> bool {
    let namespace_ptr = namespace.as_ptr();
    let namespace_size = namespace.len();
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
        self.raw_ptr,
        namespace_ptr as *const _ as *mut _,
        namespace_size,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }

  fn get_filter_state_bytes(&self, key: &[u8]) -> Option<EnvoyBuffer> {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_filter_state_bytes(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) })
    } else {
      None
    }
  }

  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let value_ptr = value.as_ptr();
    let value_size = value.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_filter_state_bytes(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        value_ptr as *const _ as *mut _,
        value_size,
      )
    }
  }

  fn get_received_request_body(&mut self) -> Option<Vec<EnvoyMutBuffer>> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_request_body_chunks_size(
        self.raw_ptr,
        &mut size,
      )
    };
    if !ok || size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_request_body_chunks(
        self.raw_ptr,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_buffered_request_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size(
        self.raw_ptr,
        &mut size,
      )
    };
    if !ok || size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_request_body_chunks(
        self.raw_ptr,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_received_request_body_size(&mut self) -> Option<usize> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_request_body_size(
        self.raw_ptr,
        &mut size as *mut _ as *mut _,
      )
    };
    if ok {
      Some(size)
    } else {
      None
    }
  }

  fn get_buffered_request_body_size(&mut self) -> Option<usize> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_request_body_size(
        self.raw_ptr,
        &mut size as *mut _ as *mut _,
      )
    };
    if ok {
      Some(size)
    } else {
      None
    }
  }

  fn drain_received_request_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_received_request_body(
        self.raw_ptr,
        number_of_bytes,
      )
    }
  }

  fn drain_buffered_request_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_buffered_request_body(
        self.raw_ptr,
        number_of_bytes,
      )
    }
  }

  fn append_received_request_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_received_request_body(
        self.raw_ptr,
        data.as_ptr() as *const _ as *mut _,
        data.len(),
      )
    }
  }

  fn append_buffered_request_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_buffered_request_body(
        self.raw_ptr,
        data.as_ptr() as *const _ as *mut _,
        data.len(),
      )
    }
  }

  fn get_received_response_body(&mut self) -> Option<Vec<EnvoyMutBuffer>> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_response_body_chunks_size(
        self.raw_ptr,
        &mut size,
      )
    };
    if !ok || size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_response_body_chunks(
        self.raw_ptr,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_buffered_response_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_response_body_chunks_size(
        self.raw_ptr,
        &mut size,
      )
    };
    if !ok || size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_response_body_chunks(
        self.raw_ptr,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_received_response_body_size(&mut self) -> Option<usize> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_received_response_body_size(
        self.raw_ptr,
        &mut size as *mut _ as *mut _,
      )
    };
    if ok {
      Some(size)
    } else {
      None
    }
  }

  fn get_buffered_response_body_size(&mut self) -> Option<usize> {
    let mut size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_http_get_buffered_response_body_size(
        self.raw_ptr,
        &mut size as *mut _ as *mut _,
      )
    };
    if ok {
      Some(size)
    } else {
      None
    }
  }

  fn drain_received_response_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_received_response_body(
        self.raw_ptr,
        number_of_bytes,
      )
    }
  }

  fn drain_buffered_response_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_buffered_response_body(
        self.raw_ptr,
        number_of_bytes,
      )
    }
  }

  fn append_received_response_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_received_response_body(
        self.raw_ptr,
        data.as_ptr() as *const _ as *mut _,
        data.len(),
      )
    }
  }

  fn append_buffered_response_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_buffered_response_body(
        self.raw_ptr,
        data.as_ptr() as *const _ as *mut _,
        data.len(),
      )
    }
  }

  fn clear_route_cache(&mut self) {
    unsafe { abi::envoy_dynamic_module_callback_http_clear_route_cache(self.raw_ptr) }
  }

  fn remove_request_header(&mut self, key: &str) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_request_header(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        std::ptr::null_mut(),
        0,
      )
    }
  }

  fn remove_request_trailer(&mut self, key: &str) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_request_trailer(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        std::ptr::null_mut(),
        0,
      )
    }
  }

  fn remove_response_header(&mut self, key: &str) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_response_header(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        std::ptr::null_mut(),
        0,
      )
    }
  }

  fn remove_response_trailer(&mut self, key: &str) -> bool {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_response_trailer(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        std::ptr::null_mut(),
        0,
      )
    }
  }

  fn get_attribute_string(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer> {
    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_get_attribute_string(
        self.raw_ptr,
        attribute_id,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) })
    } else {
      None
    }
  }

  fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<i64> {
    let mut result: i64 = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_get_attribute_int(
        self.raw_ptr,
        attribute_id,
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(result)
    } else {
      None
    }
  }

  fn send_http_callout<'a>(
    &mut self,
    callout_id: u32,
    cluster_name: &'a str,
    headers: Vec<(&'a str, &'a [u8])>,
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> abi::envoy_dynamic_module_type_http_callout_init_result {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let headers_ptr = headers.as_ptr() as *const abi::envoy_dynamic_module_type_module_http_header;
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_http_callout(
        self.raw_ptr,
        callout_id,
        cluster_name.as_ptr() as *const _ as *mut _,
        cluster_name.len(),
        headers_ptr as *const _ as *mut _,
        headers.len(),
        body_ptr as *const _ as *mut _,
        body_length,
        timeout_milliseconds,
      )
    }
  }

  fn get_most_specific_route_config(&self) -> Option<std::sync::Arc<dyn Any>> {
    unsafe {
      let filter_config_ptr =
        abi::envoy_dynamic_module_callback_get_most_specific_route_config(self.raw_ptr)
          as *mut std::sync::Arc<dyn Any>;

      filter_config_ptr.as_ref().cloned()
    }
  }

  fn continue_decoding(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_continue_decoding(self.raw_ptr);
    }
  }

  fn continue_encoding(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_continue_encoding(self.raw_ptr);
    }
  }

  fn new_scheduler(&self) -> impl EnvoyHttpFilterScheduler + 'static {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_http_filter_scheduler_new(self.raw_ptr);
      EnvoyHttpFilterSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_counter(self.raw_ptr, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_counter_vec(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increase_gauge(self.raw_ptr, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increase_gauge_vec(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_decrease_gauge(self.raw_ptr, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_decrease_gauge_vec(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res =
      unsafe { abi::envoy_dynamic_module_callback_http_filter_set_gauge(self.raw_ptr, id, value) };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_set_gauge_vec(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_record_histogram_value(self.raw_ptr, id, value)
    })?;
    Ok(())
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramVecId(id) = id;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    })?;
    Ok(())
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
      size: *mut usize,
    ) -> bool,
    getter_callback: unsafe extern "C" fn(
      filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
      result_buffer_ptr: *mut abi::envoy_dynamic_module_type_envoy_http_header,
    ) -> bool,
  ) -> Vec<(EnvoyBuffer, EnvoyBuffer)> {
    let mut count: usize = 0;
    let ok = unsafe { count_callback(self.raw_ptr, &mut count as *mut _ as *mut _) };
    if !ok || count == 0 {
      return Vec::default();
    }

    let mut headers: Vec<(EnvoyBuffer, EnvoyBuffer)> = Vec::with_capacity(count);
    let success = unsafe {
      getter_callback(
        self.raw_ptr,
        headers.as_mut_ptr() as *mut abi::envoy_dynamic_module_type_envoy_http_header,
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
      optional_size: *mut usize,
    ) -> bool,
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
        std::ptr::null_mut(),
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
      optional_size: *mut usize,
    ) -> bool,
  ) -> Vec<EnvoyBuffer> {
    let key_ptr = key.as_ptr();
    let key_size = key.len();
    let mut result_ptr: *const u8 = std::ptr::null();
    let mut result_size: usize = 0;

    let mut count: usize = 0;

    // Get the first value to get the count.
    let ret = unsafe {
      callback(
        self.raw_ptr,
        key_ptr as *const _ as *mut _,
        key_size,
        &mut result_ptr as *mut _ as *mut _,
        &mut result_size as *mut _ as *mut _,
        0,
        &mut count as *mut _ as *mut _,
      )
    };

    let mut results = Vec::new();
    if count == 0 || !ret {
      return results;
    }

    // At this point, we assume at least one value is present.
    results.push(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) });
    // So, we iterate from 1 to count - 1.
    for i in 1 .. count {
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
          std::ptr::null_mut(),
        )
      };
      // Within the range, all results are guaranteed to be non-null by Envoy.
      results.push(unsafe { EnvoyBuffer::new_from_raw(result_ptr, result_size) });
    }
    results
  }
}

/// This represents a thin thread-safe object that can be used to schedule a generic event to the
/// Envoy HTTP filter on the work thread.
///
/// For example, this can be used to offload some blocking work from the HTTP filter processing
/// thread to a module-managed thread, and then schedule an event to continue
/// processing the request.
///
/// Since this is primarily designed to be used from a different thread than the one
/// where the [`HttpFilter`] instance was created, it is marked as `Send` so that
/// the [`Box<dyn EnvoyHttpFilterScheduler>`] can be sent across threads.
///
/// It is also safe to be called concurrently, so it is marked as `Sync` as well.
#[automock]
pub trait EnvoyHttpFilterScheduler: Send + Sync {
  /// Commit the scheduled event to the worker thread where [`HttpFilter`] is running.
  ///
  /// It accepts an `event_id` which can be used to distinguish different events
  /// scheduled by the same filter. The `event_id` can be any value.
  ///
  /// Once this is called, [`HttpFilter::on_scheduled`] will be called with
  /// the same `event_id` on the worker thread where the filter is running IF
  /// by the time the event is committed, the filter is still alive.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyHttpFilterScheduler`] trait with the given raw pointer to the Envoy
/// HTTP filter scheduler object.
struct EnvoyHttpFilterSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_scheduler_module_ptr,
}

unsafe impl Send for EnvoyHttpFilterSchedulerImpl {}
unsafe impl Sync for EnvoyHttpFilterSchedulerImpl {}

impl Drop for EnvoyHttpFilterSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyHttpFilterScheduler for EnvoyHttpFilterSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

// Box<dyn EnvoyHttpFilterScheduler> is returned by mockall, so we need to implement
// EnvoyHttpFilterScheduler for it as well.
impl EnvoyHttpFilterScheduler for Box<dyn EnvoyHttpFilterScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
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
  // This assumes that the name is a valid UTF-8 string. Should we relax? At the moment,
  // it is a String at protobuf level.
  let name = if !name_ptr.is_null() {
    std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_size)).unwrap_or_default()
  } else {
    ""
  };
  let config = if !config_ptr.is_null() {
    std::slice::from_raw_parts(config_ptr, config_size)
  } else {
    b""
  };

  let mut envoy_filter_config = EnvoyHttpFilterConfigImpl {
    raw_ptr: envoy_filter_config_ptr,
  };

  envoy_dynamic_module_on_http_filter_config_new_impl(
    &mut envoy_filter_config,
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
  ($ptr:expr, $trait_:ident $(< $($args:ident),* >)?) => {{
    let config = $ptr as *mut *mut dyn $trait_$(< $($args),* >)?;

    // Drop the Box<*mut $t>, and then the Box<$t>, which also
    // drops the underlying object.
    unsafe {
      let _outer = Box::from_raw(config);
      let _inner = Box::from_raw(*config);
    }
  }};
}

fn envoy_dynamic_module_on_http_filter_config_new_impl(
  envoy_filter_config: &mut EnvoyHttpFilterConfigImpl,
  name: &str,
  config: &[u8],
  new_fn: &NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl>,
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
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_per_route_config_new(
  name_ptr: *const u8,
  name_size: usize,
  config_ptr: *const u8,
  config_size: usize,
) -> abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr {
  // This assumes that the name is a valid UTF-8 string. Should we relax? At the moment,
  // it is a String at protobuf level.
  let name = if !name_ptr.is_null() {
    std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_size)).unwrap_or_default()
  } else {
    ""
  };
  let config = if !config_ptr.is_null() {
    std::slice::from_raw_parts(config_ptr, config_size)
  } else {
    b""
  };

  envoy_dynamic_module_on_http_filter_per_route_config_new_impl(
    name,
    config,
    NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION
      .get()
      .expect("NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION must be set"),
  )
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_per_route_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr,
) {
  let ptr = config_ptr as *mut std::sync::Arc<dyn Any>;
  std::mem::drop(Box::from_raw(ptr));
}

fn envoy_dynamic_module_on_http_filter_per_route_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &NewHttpFilterPerRouteConfigFunction,
) -> abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr {
  if let Some(config) = new_fn(name, config) {
    let arc: std::sync::Arc<dyn Any> = std::sync::Arc::from(config);
    let ptr = Box::into_raw(Box::new(arc));
    ptr as abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
  } else {
    std::ptr::null()
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let mut envoy_filter = EnvoyHttpFilterImpl {
    raw_ptr: filter_envoy_ptr,
  };
  let filter_config = {
    let raw = filter_config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    &mut **raw
  };
  envoy_dynamic_module_on_http_filter_new_impl(&mut envoy_filter, filter_config)
}

fn envoy_dynamic_module_on_http_filter_new_impl(
  envoy_filter: &mut EnvoyHttpFilterImpl,
  filter_config: &mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let filter = filter_config.new_http_filter(envoy_filter);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  drop_wrapped_c_void_ptr!(filter_ptr, HttpFilter<EnvoyHttpFilterImpl>);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_stream_complete(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_stream_complete(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_headers(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_body(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_request_trailers(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_headers(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_body(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_response_trailers(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_callout_done(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  callout_id: u32,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  let headers = if headers_size > 0 {
    Some(unsafe {
      std::slice::from_raw_parts(headers as *const (EnvoyBuffer, EnvoyBuffer), headers_size)
    })
  } else {
    None
  };
  let body = if body_chunks_size > 0 {
    Some(unsafe { std::slice::from_raw_parts(body_chunks as *const EnvoyBuffer, body_chunks_size) })
  } else {
    None
  };
  filter.on_http_callout_done(
    &mut EnvoyHttpFilterImpl::new(envoy_ptr),
    callout_id,
    result,
    headers,
    body,
  )
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_scheduled(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  event_id: u64,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_scheduled(&mut EnvoyHttpFilterImpl::new(envoy_ptr), event_id);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_downstream_above_write_buffer_high_watermark(&mut EnvoyHttpFilterImpl::new(envoy_ptr));
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
  let filter = &mut **filter;
  filter.on_downstream_below_write_buffer_low_watermark(&mut EnvoyHttpFilterImpl::new(envoy_ptr));
}

impl From<envoy_dynamic_module_type_metrics_result>
  for Result<(), envoy_dynamic_module_type_metrics_result>
{
  fn from(result: envoy_dynamic_module_type_metrics_result) -> Self {
    if result == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(result)
    }
  }
}
