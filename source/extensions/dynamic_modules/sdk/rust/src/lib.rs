#![deny(warnings)]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(clippy::unnecessary_cast)]

pub mod access_log;
pub mod bootstrap;
pub mod buffer;
pub mod catch_unwind;
pub mod cert_validator;
pub mod cluster;
pub mod dns_resolver;
// Implementation detail. Public so SDK-provided macros (for example, `declare_matcher!`) that
// expand in user crates can reach the safe helpers; users should not depend on this module
// directly.
#[doc(hidden)]
pub mod ffi_helpers;
pub mod http;
pub mod listener;
pub mod load_balancer;
pub mod matcher;
pub mod network;
pub mod tracer;
pub mod transport_socket;
pub mod udp_listener;
pub mod upstream_http_tcp_bridge;
pub mod utility;
pub use bootstrap::*;
pub use buffer::*;
pub use catch_unwind::*;
pub use cert_validator::*;
pub use cluster::*;
pub use dns_resolver::*;
pub use http::*;
pub use listener::*;
pub use load_balancer::*;
pub use network::*;
pub use tracer::*;
pub use transport_socket::*;
pub use udp_listener::*;
pub use upstream_http_tcp_bridge::*;
pub use utility::*;

#[cfg(test)]
#[path = "./lib_test.rs"]
mod mod_test;

use crate::abi::envoy_dynamic_module_type_metrics_result;
use std::any::Any;
use std::sync::OnceLock;

/// Convert a panic payload (as captured by [`std::panic::catch_unwind`]) into a printable
/// string. Public so that [`declare_matcher!`] and other macros can format the payload from
/// the consuming crate.
#[doc(hidden)]
pub fn panic_payload_to_string(payload: Box<dyn Any + Send>) -> String {
  match payload.downcast::<String>() {
    Ok(s) => *s,
    Err(payload) => match payload.downcast::<&str>() {
      Ok(s) => s.to_string(),
      Err(_) => "<non-string panic payload>".to_string(),
    },
  }
}

/// Log a panic caught at an FFI boundary. Exposed via `#[doc(hidden)]` so SDK-provided macros
/// such as `declare_matcher!` and `declare_init_functions!` can call it from user crates after
/// expansion.
///
/// Logging runs after `catch_unwind` has already captured the original panic, so a secondary
/// panic inside `format!` or `envoy_log_error!` would unwind into `libc::abort` rather than
/// across the FFI boundary. That is intentional: a recursive panic in the log path indicates
/// the process is too broken to continue safely.
#[doc(hidden)]
pub fn log_ffi_panic(function_name: &str, payload: Box<dyn Any + Send>) {
  crate::envoy_log_error!(
    "{}: caught panic at FFI boundary: {}",
    function_name,
    crate::panic_payload_to_string(payload)
  );
}

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
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION,
          $new_http_filter_config_fn,
          "NEW_HTTP_FILTER_CONFIG_FUNCTION"
        );
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION,
          $new_http_filter_per_route_config_fn,
          "NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
  ($f:ident, $new_http_filter_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION,
          $new_http_filter_config_fn,
          "NEW_HTTP_FILTER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

/// Get the concurrency from the server context options.
/// # Safety
///
/// This function must be called on the main thread.
pub unsafe fn get_server_concurrency() -> u32 {
  unsafe { abi::envoy_dynamic_module_callback_get_concurrency() }
}

/// Check if the server is running in config validation mode (`--mode validate`).
///
/// This allows modules to optimize by only parsing and validating their config without
/// performing expensive operations such as provider lookups or loading external resources.
///
/// # Safety
///
/// This function must be called on the main thread.
pub unsafe fn is_validation_mode() -> bool {
  unsafe { abi::envoy_dynamic_module_callback_is_validation_mode() }
}

/// Register a function pointer under a name in the process-wide function registry.
///
/// This allows modules loaded in the same process to expose functions that other modules can
/// resolve by name and call directly, enabling zero-copy cross-module interactions. For example,
/// a bootstrap extension can register a function that returns a pointer to a tenant snapshot,
/// and HTTP filters can resolve and call it on every request.
///
/// Registration is typically done once during bootstrap (e.g., in `on_server_initialized`).
/// Duplicate registration under the same key returns `false`.
///
/// Callers are responsible for agreeing on the function signature out-of-band, since the
/// registry stores opaque pointers — analogous to `dlsym` semantics.
///
/// This is thread-safe and can be called from any thread.
///
/// # Safety
///
/// The `function_ptr` must point to a valid function that remains valid for the lifetime of the
/// process.
pub unsafe fn register_function(key: &str, function_ptr: *const std::ffi::c_void) -> bool {
  unsafe {
    abi::envoy_dynamic_module_callback_register_function(
      str_to_module_buffer(key),
      function_ptr as *mut std::ffi::c_void,
    )
  }
}

/// Retrieve a previously registered function pointer by name from the process-wide function
/// registry. The returned pointer can be cast to the expected function signature and called
/// directly.
///
/// Resolution is typically done once during configuration creation (e.g., in
/// `on_http_filter_config_new`) and the result cached for per-request use.
///
/// This is thread-safe and can be called from any thread.
pub fn get_function(key: &str) -> Option<*const std::ffi::c_void> {
  let mut ptr: *mut std::ffi::c_void = std::ptr::null_mut();
  let found =
    unsafe { abi::envoy_dynamic_module_callback_get_function(str_to_module_buffer(key), &mut ptr) };
  if found {
    Some(ptr as *const std::ffi::c_void)
  } else {
    None
  }
}

/// Register an opaque data pointer under a name in the process-wide shared data registry.
///
/// This allows modules loaded in the same process to share arbitrary state, such as runtime
/// handles, configuration snapshots, or shared caches without requiring direct access to
/// each other's globals. For example, a bootstrap extension can register a `Tokio` runtime handle
/// and HTTP filters or cluster extensions can retrieve and use it for async operations.
///
/// Unlike [`register_function`], the shared data registry allows overwriting an existing entry.
/// If the key already exists, the data pointer is updated and the function returns `true`. This
/// supports patterns where shared state is refreshed (e.g., after a configuration reload).
/// Callers are responsible for managing the lifetime of overwritten data pointers.
///
/// Registration is typically done once during bootstrap (e.g., in `on_server_initialized` or
/// `on_scheduled`).
///
/// This is thread-safe and can be called from any thread.
///
/// # Safety
///
/// The `data_ptr` must point to valid data that remains valid for the lifetime of the process.
/// Callers are responsible for agreeing on the data type out-of-band, since the registry stores
/// opaque pointers.
pub unsafe fn register_shared_data(key: &str, data_ptr: *const std::ffi::c_void) -> bool {
  unsafe {
    abi::envoy_dynamic_module_callback_register_shared_data(
      str_to_module_buffer(key),
      data_ptr as *mut std::ffi::c_void,
    )
  }
}

/// Retrieve a previously registered data pointer by name from the process-wide shared data
/// registry. The returned pointer can be cast to the expected data type and used directly.
///
/// Resolution is typically done once during configuration creation (e.g., in
/// `on_http_filter_config_new`) and the result cached for per-request use.
///
/// This is thread-safe and can be called from any thread.
pub fn get_shared_data(key: &str) -> Option<*const std::ffi::c_void> {
  let mut ptr: *mut std::ffi::c_void = std::ptr::null_mut();
  let found = unsafe {
    abi::envoy_dynamic_module_callback_get_shared_data(str_to_module_buffer(key), &mut ptr)
  };
  if found {
    Some(ptr as *const std::ffi::c_void)
  } else {
    None
  }
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
      {
        let level = $level;
        // SAFETY: envoy_dynamic_module_callback_log_enabled and envoy_dynamic_module_callback_log
        // are FFI calls provided by the Envoy host.
        let enabled = unsafe { $crate::abi::envoy_dynamic_module_callback_log_enabled(level) };
        if enabled {
          let message = format!($($arg)*);
          let message_bytes = message.as_bytes();
          unsafe {
            $crate::abi::envoy_dynamic_module_callback_log(
              level,
              $crate::abi::envoy_dynamic_module_type_module_buffer {
                ptr: message_bytes.as_ptr() as *const ::std::os::raw::c_char,
                length: message_bytes.len(),
              },
            );
          }
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

/// Guard macro that ensures each factory `OnceLock` is registered by exactly one module.
///
/// When the same module is re-initialized (for example, static modules loaded multiple times
/// via `newDynamicModuleByName`, or per-route config triggering a second init), the function
/// pointer will be identical and the re-registration is silently accepted (idempotent).
///
/// If a *different* module (standalone `.so` or consolidated `.so`) tries to register a
/// different factory function for the same slot, this macro logs a critical message via
/// `envoy_log_critical!` and returns `null` from the surrounding `on_program_init`, causing
/// Envoy to refuse to start — the correct behaviour for a data-correctness issue. The macro
/// is only invoked inside `envoy_dynamic_module_on_program_init` (signature
/// `-> *const c_char`), where the null return is the documented "init failed" sentinel.
#[macro_export]
macro_rules! set_factory_once {
  ($static:expr, $fn:expr, $name:literal) => {
    if let Err(new_val) = $static.set($fn) {
      if !::std::ptr::fn_addr_eq(*$static.get().unwrap(), new_val) {
        $crate::envoy_log_critical!(
          "Duplicate factory registration for {}. A different module already registered this \
           factory. Check dynamic_module_config for conflicting standalone and consolidated .so \
           loads.",
          $name,
        );
        // Return the "init failed" sentinel rather than panicking; unwinding across the
        // `extern "C"` boundary is undefined behavior on the default `panic="unwind"`.
        return ::std::ptr::null();
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
  NewHttpFilterConfigFunction<http::EnvoyHttpFilterConfigImpl, http::EnvoyHttpFilterImpl>,
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

// HTTP filter types are in the http module and re-exported above.

pub(crate) fn str_to_module_buffer(s: &str) -> abi::envoy_dynamic_module_type_module_buffer {
  abi::envoy_dynamic_module_type_module_buffer {
    ptr: s.as_ptr() as *const _ as *mut _,
    length: s.len(),
  }
}

pub(crate) fn strs_to_module_buffers(
  strs: &[&str],
) -> Vec<abi::envoy_dynamic_module_type_module_buffer> {
  strs.iter().map(|s| str_to_module_buffer(s)).collect()
}

pub(crate) fn bytes_to_module_buffer(b: &[u8]) -> abi::envoy_dynamic_module_type_module_buffer {
  abi::envoy_dynamic_module_type_module_buffer {
    ptr: b.as_ptr() as *const _ as *mut _,
    length: b.len(),
  }
}

/// Host count information for a cluster at a specific priority level.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ClusterHostCount {
  /// Total number of hosts in the cluster at this priority level.
  pub total: usize,
  /// Number of healthy hosts in the cluster at this priority level.
  pub healthy: usize,
  /// Number of degraded hosts in the cluster at this priority level.
  pub degraded: usize,
}

/// The identifier for an EnvoyCounter.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyCounterId(pub usize);

/// The identifier for an EnvoyCounterVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyCounterVecId(pub usize);

/// The identifier for an EnvoyGauge.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyGaugeId(pub usize);

/// The identifier for an EnvoyGaugeVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyGaugeVecId(pub usize);

/// The identifier for an EnvoyHistogram.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyHistogramId(pub usize);

/// The identifier for an EnvoyHistogramVec.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct EnvoyHistogramVecId(pub usize);

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

/// We wrap the Box<dyn T> in another Box to be able to pass the address of the Box to C, and
/// retrieve it back when the C code calls the destroy function via [`drop_wrapped_c_void_ptr!`].
/// This is necessary because the Box<dyn T> is a fat pointer, and we can't pass it directly.
/// See https://users.rust-lang.org/t/sending-a-boxed-trait-over-ffi/21708 for the exact problem.
//
// Implementation note: this can be a simple function taking a type parameter, but we have it as
// a macro to align with the other macro drop_wrapped_c_void_ptr!.
#[macro_export]
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
#[macro_export]
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

// =============================================================================
// Network Filter Support
// =============================================================================

/// Declare the init functions for the dynamic module with network filter support only.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is
/// loaded.
///
/// The second argument has [`NewNetworkFilterConfigFunction`] type, and it is called when the new
/// network filter configuration is created.
///
/// Note that if a module needs to support both HTTP and Network filters,
/// [`declare_all_init_functions`] should be used instead.
#[macro_export]
macro_rules! declare_network_filter_init_functions {
  ($f:ident, $new_network_filter_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_NETWORK_FILTER_CONFIG_FUNCTION,
          $new_network_filter_config_fn,
          "NEW_NETWORK_FILTER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

/// Declare the init functions for the dynamic module with any combination of filter types.
///
/// This macro allows a single module to provide any combination of HTTP, Network, Listener,
/// UDP Listener, Bootstrap filters, and Certificate Validators.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is
/// loaded.
///
/// The remaining arguments are keyword-labeled filter config functions. Omitted filters won't be
/// registered.
/// Supported filters:
/// - `http:` — [`NewHttpFilterConfigFunction`] for HTTP filters
/// - `network:` — [`NewNetworkFilterConfigFunction`] for Network filters
/// - `listener:` — [`NewListenerFilterConfigFunction`] for Listener filters
/// - `udp_listener:` — [`NewUdpListenerFilterConfigFunction`] for UDP Listener filters
/// - `bootstrap:` — [`NewBootstrapExtensionConfigFunction`] for Bootstrap extensions
/// - `cert_validator:` — [`NewCertValidatorConfigFunction`] for TLS certificate validators
/// - `upstream_http_tcp_bridge:` — [`NewUpstreamHttpTcpBridgeConfigFunction`] for upstream HTTP TCP
///   bridges
/// - `http_per_route:` — [`NewHttpFilterPerRouteConfigFunction`] for HTTP per-route configs
/// - `load_balancer:` — [`NewLoadBalancerConfigFunction`] for load balancer policies
/// - `cluster:` — [`NewClusterConfigFunction`] for custom clusters
/// - `tracer:` — [`NewTracerConfigFunction`] for tracers
/// - `dns_resolver:` — [`NewDnsResolverConfigFunction`] for DNS resolvers
/// - `transport_socket:` — [`NewTransportSocketFactoryConfigFunction`] for transport sockets
/// - `access_logger:` — [`NewAccessLoggerConfigFunction`] for access loggers
///
/// # Examples
///
/// HTTP only:
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// declare_all_init_functions!(my_program_init, http: my_new_http_filter_config_fn);
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
///
/// Network + UDP Listener:
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// declare_all_init_functions!(my_program_init,
///   network: my_new_network_filter_config_fn,
///   udp_listener: my_new_udp_listener_filter_config_fn,
/// );
///
/// fn my_program_init() -> bool {
///   true
/// }
///
/// fn my_new_network_filter_config_fn<EC: EnvoyNetworkFilterConfig, ENF: EnvoyNetworkFilter>(
///   _envoy_filter_config: &mut EC,
///   _name: &str,
///   _config: &[u8],
/// ) -> Option<Box<dyn NetworkFilterConfig<ENF>>> {
///   Some(Box::new(MyNetworkFilterConfig {}))
/// }
///
/// struct MyNetworkFilterConfig {}
///
/// impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for MyNetworkFilterConfig {
///   fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
///     Box::new(MyNetworkFilter {})
///   }
/// }
///
/// struct MyNetworkFilter {}
///
/// impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for MyNetworkFilter {}
///
/// fn my_new_udp_listener_filter_config_fn<
///   EC: EnvoyUdpListenerFilterConfig,
///   ELF: EnvoyUdpListenerFilter,
/// >(
///   _envoy_filter_config: &mut EC,
///   _name: &str,
///   _config: &[u8],
/// ) -> Option<Box<dyn UdpListenerFilterConfig<ELF>>> {
///   Some(Box::new(MyUdpListenerFilterConfig {}))
/// }
///
/// struct MyUdpListenerFilterConfig {}
///
/// impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for MyUdpListenerFilterConfig {
///   fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
///     Box::new(MyUdpListenerFilter {})
///   }
/// }
///
/// struct MyUdpListenerFilter {}
///
/// impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for MyUdpListenerFilter {}
/// ```
#[macro_export]
macro_rules! declare_all_init_functions {
  ($f:ident, $($filter_type:ident : $filter_fn:expr),+ $(,)?) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        $(
          declare_all_init_functions!(@register $filter_type : $filter_fn);
        )+
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };

  (@register http : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION,
      $fn,
      "NEW_HTTP_FILTER_CONFIG_FUNCTION"
    );
  };
  (@register http_per_route : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION,
      $fn,
      "NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION"
    );
  };
  (@register network : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_NETWORK_FILTER_CONFIG_FUNCTION,
      $fn,
      "NEW_NETWORK_FILTER_CONFIG_FUNCTION"
    );
  };
  (@register listener : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_LISTENER_FILTER_CONFIG_FUNCTION,
      $fn,
      "NEW_LISTENER_FILTER_CONFIG_FUNCTION"
    );
  };
  (@register udp_listener : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION,
      $fn,
      "NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION"
    );
  };
  (@register bootstrap : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION,
      $fn,
      "NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION"
    );
  };
  (@register load_balancer : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_LOAD_BALANCER_CONFIG_FUNCTION,
      $fn,
      "NEW_LOAD_BALANCER_CONFIG_FUNCTION"
    );
  };
  (@register cluster : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_CLUSTER_CONFIG_FUNCTION,
      $fn,
      "NEW_CLUSTER_CONFIG_FUNCTION"
    );
  };
  (@register cert_validator : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_CERT_VALIDATOR_CONFIG_FUNCTION,
      $fn,
      "NEW_CERT_VALIDATOR_CONFIG_FUNCTION"
    );
  };
  (@register upstream_http_tcp_bridge : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION,
      $fn,
      "NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION"
    );
  };
  (@register tracer : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_TRACER_CONFIG_FUNCTION,
      $fn,
      "NEW_TRACER_CONFIG_FUNCTION"
    );
  };
  (@register dns_resolver : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_DNS_RESOLVER_CONFIG_FUNCTION,
      $fn,
      "NEW_DNS_RESOLVER_CONFIG_FUNCTION"
    );
  };
  (@register transport_socket : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION,
      $fn,
      "NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION"
    );
  };
  (@register access_logger : $fn:expr) => {
    envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
      envoy_proxy_dynamic_modules_rust_sdk::NEW_ACCESS_LOGGER_CONFIG_FUNCTION,
      $fn,
      "NEW_ACCESS_LOGGER_CONFIG_FUNCTION"
    );
  };
}

/// The function signature for the new network filter configuration function.
///
/// This is called when a new network filter configuration is created, and it must return a new
/// [`NetworkFilterConfig`] object. Returning `None` will cause the network filter configuration to
/// be rejected.
///
/// The first argument `envoy_filter_config` is a mutable reference to an
/// [`EnvoyNetworkFilterConfig`] object that provides access to Envoy operations.
/// The second argument `name` is the name of the filter configuration as specified in the Envoy
/// config.
/// The third argument `config` is the raw configuration bytes.
pub type NewNetworkFilterConfigFunction<EC, ENF> = fn(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn NetworkFilterConfig<ENF>>>;

/// The global init function for network filter configurations. This is set via the
/// `declare_network_filter_init_functions` macro, and is not intended to be set directly.
pub static NEW_NETWORK_FILTER_CONFIG_FUNCTION: OnceLock<
  NewNetworkFilterConfigFunction<
    network::EnvoyNetworkFilterConfigImpl,
    network::EnvoyNetworkFilterImpl,
  >,
> = OnceLock::new();

// =============================================================================
// Listener Filter Support
// =============================================================================

/// Declare the init functions for the dynamic module with listener filter support only.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is
/// loaded.
///
/// The second argument has [`NewListenerFilterConfigFunction`] type, and it is called when the new
/// listener filter configuration is created.
#[macro_export]
macro_rules! declare_listener_filter_init_functions {
  ($f:ident, $new_listener_filter_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init(
      server_factory_context_ptr: abi::envoy_dynamic_module_type_server_factory_context_envoy_ptr,
    ) -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_LISTENER_FILTER_CONFIG_FUNCTION,
          $new_listener_filter_config_fn,
          "NEW_LISTENER_FILTER_CONFIG_FUNCTION"
        );
        if ($f(server_factory_context_ptr)) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

/// The function signature for the new listener filter configuration function.
///
/// This is called when a new listener filter configuration is created, and it must return a new
/// [`ListenerFilterConfig`] object. Returning `None` will cause the listener filter configuration
/// to be rejected.
///
/// The first argument `envoy_filter_config` is a mutable reference to an
/// [`EnvoyListenerFilterConfig`] object that provides access to Envoy operations.
/// The second argument `name` is the name of the filter configuration as specified in the Envoy
/// config.
/// The third argument `config` is the raw configuration bytes.
pub type NewListenerFilterConfigFunction<EC, ELF> =
  fn(
    envoy_filter_config: &mut EC,
    name: &str,
    config: &[u8],
  ) -> Option<Box<dyn listener::ListenerFilterConfig<ELF>>>;

/// The global init function for listener filter configurations. This is set via the
/// `declare_listener_filter_init_functions` macro, and is not intended to be set directly.
pub static NEW_LISTENER_FILTER_CONFIG_FUNCTION: OnceLock<
  NewListenerFilterConfigFunction<
    listener::EnvoyListenerFilterConfigImpl,
    listener::EnvoyListenerFilterImpl,
  >,
> = OnceLock::new();

// =============================================================================
// UDP Listener Filter Support
// =============================================================================

/// Declare the init functions for the dynamic module with UDP listener filter support only.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is
/// loaded.
///
/// The second argument has [`NewUdpListenerFilterConfigFunction`] type, and it is called when the
/// new UDP listener filter configuration is created.
#[macro_export]
macro_rules! declare_udp_listener_filter_init_functions {
  ($f:ident, $new_udp_listener_filter_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION,
          $new_udp_listener_filter_config_fn,
          "NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

/// The function signature for the new UDP listener filter configuration function.
///
/// This is called when a new UDP listener filter configuration is created, and it must return a new
/// [`UdpListenerFilterConfig`] object. Returning `None` will cause the filter configuration
/// to be rejected.
///
/// The first argument `envoy_filter_config` is a mutable reference to an
/// [`EnvoyUdpListenerFilterConfig`] object that provides access to Envoy operations.
/// The second argument `name` is the name of the filter configuration as specified in the Envoy
/// config.
/// The third argument `config` is the raw configuration bytes.
pub type NewUdpListenerFilterConfigFunction<EC, ELF> =
  fn(
    envoy_filter_config: &mut EC,
    name: &str,
    config: &[u8],
  ) -> Option<Box<dyn udp_listener::UdpListenerFilterConfig<ELF>>>;

/// The global init function for UDP listener filter configurations. This is set via the
/// `declare_udp_listener_filter_init_functions` macro, and is not intended to be set directly.
pub static NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION: OnceLock<
  NewUdpListenerFilterConfigFunction<
    udp_listener::EnvoyUdpListenerFilterConfigImpl,
    udp_listener::EnvoyUdpListenerFilterImpl,
  >,
> = OnceLock::new();

// ============================================================================
// Bootstrap Extension
// ============================================================================

/// A global variable that holds the factory function to create a new bootstrap extension config.
/// This is set by the [`declare_bootstrap_init_functions`] macro.
pub static NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION: OnceLock<NewBootstrapExtensionConfigFunction> =
  OnceLock::new();

/// The type of the factory function that creates a new bootstrap extension configuration.
pub type NewBootstrapExtensionConfigFunction = fn(
  &mut dyn EnvoyBootstrapExtensionConfig,
  &str,
  &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>>;

/// Declare the init functions for the bootstrap extension dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with [`NewBootstrapExtensionConfigFunction`] type.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);
///
/// fn my_program_init() -> bool {
///   true
/// }
///
/// fn my_new_bootstrap_extension_config_fn(
///   _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
///   _name: &str,
///   _config: &[u8],
/// ) -> Option<Box<dyn BootstrapExtensionConfig>> {
///   Some(Box::new(MyBootstrapExtensionConfig {}))
/// }
///
/// struct MyBootstrapExtensionConfig {}
///
/// impl BootstrapExtensionConfig for MyBootstrapExtensionConfig {
///   fn new_bootstrap_extension(
///     &self,
///     _envoy_extension: &mut dyn EnvoyBootstrapExtension,
///   ) -> Box<dyn BootstrapExtension> {
///     Box::new(MyBootstrapExtension {})
///   }
/// }
///
/// struct MyBootstrapExtension {}
///
/// impl BootstrapExtension for MyBootstrapExtension {
///   fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
///     // Use envoy_log_info! macro for logging.
///     // envoy_log_info!("Bootstrap extension initialized!");
///   }
/// }
/// ```
#[macro_export]
macro_rules! declare_bootstrap_init_functions {
  ($f:ident, $new_bootstrap_extension_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION,
          $new_bootstrap_extension_config_fn,
          "NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

// =================================================================================================
// Access Logger Dynamic Module
// =================================================================================================

/// The function signature for creating a new access logger configuration.
///
/// The `ctx` provides access to the metrics-defining APIs that should be invoked at
/// configuration time. The `name` is the value of `logger_name` from the `dynamic_modules`
/// access-log configuration, allowing a single module to dispatch to different logger
/// implementations. Returning `None` causes Envoy to reject the access-log configuration.
pub type NewAccessLoggerConfigFunction = fn(
  ctx: &access_log::ConfigContext,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn access_log::AccessLoggerConfig>>;

/// The global factory function for access logger configurations. This is set via the
/// `access_logger:` arm of [`declare_all_init_functions!`] (or the legacy
/// [`declare_access_logger!`] shim) and is not intended to be set directly.
pub static NEW_ACCESS_LOGGER_CONFIG_FUNCTION: OnceLock<NewAccessLoggerConfigFunction> =
  OnceLock::new();

// =================================================================================================
// Cluster Dynamic Module
// =================================================================================================

/// The type of the factory function that creates a new cluster configuration.
///
/// The `envoy_cluster_metrics` parameter provides access to metric-defining and metric-recording
/// callbacks. The caller receives an `Arc` so it can be stored and used at runtime
/// (e.g., during cluster lifecycle events) for recording metrics.
pub type NewClusterConfigFunction = fn(
  name: &str,
  config: &[u8],
  envoy_cluster_metrics: std::sync::Arc<dyn cluster::EnvoyClusterMetrics>,
) -> Option<Box<dyn cluster::ClusterConfig>>;

/// Global storage for the cluster config factory function.
pub static NEW_CLUSTER_CONFIG_FUNCTION: OnceLock<NewClusterConfigFunction> = OnceLock::new();

/// Declare the init functions for the cluster dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with [`NewClusterConfigFunction`] type.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
/// use std::sync::Arc;
///
/// declare_cluster_init_functions!(my_program_init, my_new_cluster_config_fn);
///
/// fn my_program_init() -> bool {
///   true
/// }
///
/// fn my_new_cluster_config_fn(
///   _name: &str,
///   _config: &[u8],
///   envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics>,
/// ) -> Option<Box<dyn ClusterConfig>> {
///   let counter_id = envoy_cluster_metrics.define_counter("my_counter").ok()?;
///   Some(Box::new(MyClusterConfig {
///     counter_id,
///     envoy_cluster_metrics,
///   }))
/// }
///
/// struct MyClusterConfig {
///   counter_id: EnvoyCounterId,
///   envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics>,
/// }
///
/// impl ClusterConfig for MyClusterConfig {
///   fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
///     Box::new(MyCluster {
///       counter_id: self.counter_id,
///       envoy_cluster_metrics: self.envoy_cluster_metrics.clone(),
///     })
///   }
/// }
///
/// struct MyCluster {
///   counter_id: EnvoyCounterId,
///   envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics>,
/// }
///
/// impl Cluster for MyCluster {
///   fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
///     self
///       .envoy_cluster_metrics
///       .increment_counter(self.counter_id, 1)
///       .ok();
///     envoy_cluster.pre_init_complete();
///   }
///
///   fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
///     Box::new(MyClusterLb {})
///   }
/// }
///
/// struct MyClusterLb {}
///
/// impl ClusterLb for MyClusterLb {
///   fn choose_host(
///     &mut self,
///     _context: Option<&dyn ClusterLbContext>,
///     _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
///   ) -> HostSelectionResult {
///     HostSelectionResult::NoHost
///   }
/// }
/// ```
#[macro_export]
macro_rules! declare_cluster_init_functions {
  ($f:ident, $new_cluster_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_CLUSTER_CONFIG_FUNCTION,
          $new_cluster_config_fn,
          "NEW_CLUSTER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

// =================================================================================================
// Load Balancer Dynamic Module Support
// =================================================================================================

/// The function signature for creating a new load balancer configuration.
///
/// The `envoy_lb_config` parameter provides access to metric-defining and metric-recording
/// callbacks. The caller receives an `Arc` so it can be stored and used at runtime
/// (e.g., in `choose_host`) for recording metrics.
pub type NewLoadBalancerConfigFunction = fn(
  name: &str,
  config: &[u8],
  envoy_lb_config: std::sync::Arc<dyn load_balancer::EnvoyLbConfig>,
) -> Option<Box<dyn load_balancer::LoadBalancerConfig>>;

/// Global function for creating load balancer configurations.
pub static NEW_LOAD_BALANCER_CONFIG_FUNCTION: OnceLock<NewLoadBalancerConfigFunction> =
  OnceLock::new();

/// Declare the init functions for a load balancer dynamic module.
///
/// This macro generates the necessary `extern "C"` functions for the load balancer module.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
/// use std::sync::Arc;
///
/// fn program_init() -> bool {
///   true
/// }
///
/// fn new_lb_config(
///   name: &str,
///   config: &[u8],
///   envoy_lb_config: Arc<dyn EnvoyLbConfig>,
/// ) -> Option<Box<dyn LoadBalancerConfig>> {
///   let counter_id = envoy_lb_config.define_counter("my_counter").ok()?;
///   Some(Box::new(MyLbConfig {
///     counter_id,
///     envoy_lb_config,
///   }))
/// }
///
/// declare_load_balancer_init_functions!(program_init, new_lb_config);
///
/// struct MyLbConfig {
///   counter_id: EnvoyCounterId,
///   envoy_lb_config: Arc<dyn EnvoyLbConfig>,
/// }
///
/// impl LoadBalancerConfig for MyLbConfig {
///   fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyLoadBalancer) -> Box<dyn LoadBalancer> {
///     Box::new(MyLoadBalancer {
///       next_index: 0,
///       counter_id: self.counter_id,
///       envoy_lb_config: self.envoy_lb_config.clone(),
///     })
///   }
/// }
///
/// struct MyLoadBalancer {
///   next_index: usize,
///   counter_id: EnvoyCounterId,
///   envoy_lb_config: Arc<dyn EnvoyLbConfig>,
/// }
///
/// impl LoadBalancer for MyLoadBalancer {
///   fn choose_host(&mut self, envoy_lb: &dyn EnvoyLoadBalancer) -> Option<HostSelection> {
///     let count = envoy_lb.get_healthy_hosts_count(0);
///     if count == 0 {
///       return None;
///     }
///     let index = self.next_index % count;
///     self.next_index += 1;
///     self
///       .envoy_lb_config
///       .increment_counter(self.counter_id, 1)
///       .ok();
///     Some(HostSelection::at_default_priority(index as u32))
///   }
/// }
/// ```
#[macro_export]
macro_rules! declare_load_balancer_init_functions {
  ($f:ident, $new_lb_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_LOAD_BALANCER_CONFIG_FUNCTION,
          $new_lb_config_fn,
          "NEW_LOAD_BALANCER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

// =============================================================================
// Certificate Validator
// =============================================================================

/// The function signature for creating a new cert validator configuration.
pub type NewCertValidatorConfigFunction =
  fn(name: &str, config: &[u8]) -> Option<Box<dyn cert_validator::CertValidatorConfig>>;

/// Global function for creating cert validator configurations.
pub static NEW_CERT_VALIDATOR_CONFIG_FUNCTION: OnceLock<NewCertValidatorConfigFunction> =
  OnceLock::new();

/// Declare the init functions for a cert validator dynamic module.
///
/// This macro generates the necessary `extern "C"` functions for the cert validator module.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::cert_validator::*;
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// fn program_init() -> bool {
///   true
/// }
///
/// fn new_cert_validator_config(name: &str, config: &[u8]) -> Option<Box<dyn CertValidatorConfig>> {
///   Some(Box::new(MyCertValidatorConfig {}))
/// }
///
/// declare_cert_validator_init_functions!(program_init, new_cert_validator_config);
///
/// struct MyCertValidatorConfig {}
///
/// impl CertValidatorConfig for MyCertValidatorConfig {
///   fn do_verify_cert_chain(
///     &self,
///     _envoy_cert_validator: &EnvoyCertValidator,
///     certs: &[&[u8]],
///     host_name: &str,
///     is_server: bool,
///   ) -> ValidationResult {
///     ValidationResult::successful()
///   }
///
///   fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
///     0x03
///   }
///
///   fn update_digest(&self) -> &[u8] {
///     b"my_cert_validator"
///   }
/// }
/// ```
#[macro_export]
macro_rules! declare_cert_validator_init_functions {
  ($f:ident, $new_cert_validator_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_CERT_VALIDATOR_CONFIG_FUNCTION,
          $new_cert_validator_config_fn,
          "NEW_CERT_VALIDATOR_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

// =================================================================================================
// Upstream HTTP TCP Bridge Dynamic Module Support
// =================================================================================================

/// The type of the factory function that creates a new upstream HTTP TCP bridge configuration.
pub type NewUpstreamHttpTcpBridgeConfigFunction =
  fn(name: &str, config: &[u8]) -> Option<Box<dyn UpstreamHttpTcpBridgeConfig>>;

/// Global storage for the upstream HTTP TCP bridge config factory function.
pub static NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION: OnceLock<
  NewUpstreamHttpTcpBridgeConfigFunction,
> = OnceLock::new();

// =================================================================================================
// Tracer Dynamic Module Support
// =================================================================================================

/// The type of the factory function that creates a new tracer configuration.
///
/// The `ctx` provides access to metrics definition and update APIs. Metrics should be defined
/// during configuration creation and the context stored for runtime metric updates.
pub type NewTracerConfigFunction =
  fn(ctx: TracerConfigContext, name: &str, config: &[u8]) -> Option<Box<dyn TracerConfig>>;

/// Global storage for the tracer config factory function.
pub static NEW_TRACER_CONFIG_FUNCTION: OnceLock<NewTracerConfigFunction> = OnceLock::new();

// =================================================================================================
// DNS Resolver Dynamic Module Support
// =================================================================================================

/// The type of the factory function that creates a new DNS resolver configuration.
///
/// The `envoy_dns_resolver_config` parameter provides access to the Envoy-side DNS resolver
/// configuration. The caller receives an `Arc` so it can be stored and used at runtime
/// (e.g., during resolution) for recording metrics.
pub type NewDnsResolverConfigFunction = fn(
  name: &str,
  config: &[u8],
  envoy_dns_resolver_config: std::sync::Arc<dyn dns_resolver::EnvoyDnsResolverConfig>,
) -> Option<Box<dyn dns_resolver::DnsResolverConfig>>;

/// Global storage for the DNS resolver config factory function.
pub static NEW_DNS_RESOLVER_CONFIG_FUNCTION: OnceLock<NewDnsResolverConfigFunction> =
  OnceLock::new();

/// Declare the init functions for a DNS resolver dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with [`NewDnsResolverConfigFunction`] type.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
/// use std::sync::Arc;
///
/// fn program_init() -> bool {
///   true
/// }
///
/// fn new_dns_resolver_config(
///   _name: &str,
///   _config: &[u8],
///   _envoy_dns_resolver_config: Arc<dyn EnvoyDnsResolverConfig>,
/// ) -> Option<Box<dyn DnsResolverConfig>> {
///   Some(Box::new(MyDnsResolverConfig {}))
/// }
///
/// declare_dns_resolver_init_functions!(program_init, new_dns_resolver_config);
///
/// struct MyDnsResolverConfig {}
///
/// impl DnsResolverConfig for MyDnsResolverConfig {
///   fn new_resolver(
///     &self,
///     envoy_callback: Arc<dyn EnvoyDnsResolverCallback>,
///   ) -> Box<dyn DnsResolverInstance> {
///     Box::new(MyDnsResolver { envoy_callback })
///   }
/// }
///
/// struct MyDnsResolver {
///   envoy_callback: Arc<dyn EnvoyDnsResolverCallback>,
/// }
///
/// impl DnsResolverInstance for MyDnsResolver {
///   fn resolve(
///     &self,
///     _dns_name: &str,
///     _lookup_family: DnsLookupFamily,
///     _query_id: u64,
///   ) -> Option<Box<dyn DnsActiveQuery>> {
///     None
///   }
/// }
/// ```
#[macro_export]
macro_rules! declare_dns_resolver_init_functions {
  ($f:ident, $new_dns_resolver_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_DNS_RESOLVER_CONFIG_FUNCTION,
          $new_dns_resolver_config_fn,
          "NEW_DNS_RESOLVER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}

// =================================================================================================
// Transport Socket Dynamic Module Support
// =================================================================================================

/// The factory function signature for creating a new transport socket factory configuration.
pub type NewTransportSocketFactoryConfigFunction<ETS> =
  fn(
    name: &str,
    config: &[u8],
    is_upstream: bool,
  ) -> Option<Box<dyn transport_socket::TransportSocketFactoryConfig<ETS>>>;

/// Global storage for the transport socket factory configuration function.
pub static NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION: OnceLock<
  NewTransportSocketFactoryConfigFunction<EnvoyTransportSocketImpl>,
> = OnceLock::new();

/// Declare the init functions for a transport socket dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with
/// [`NewTransportSocketFactoryConfigFunction`] type.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// fn program_init() -> bool {
///   true
/// }
///
/// fn new_transport_socket_factory_config(
///   _name: &str,
///   _config: &[u8],
///   _is_upstream: bool,
/// ) -> Option<Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>> {
///   None
/// }
///
/// declare_transport_socket_init_functions!(program_init, new_transport_socket_factory_config);
/// ```
#[macro_export]
macro_rules! declare_transport_socket_init_functions {
  ($f:ident, $new_transport_socket_factory_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION,
          $new_transport_socket_factory_config_fn,
          "NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}
