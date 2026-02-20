//! Test module demonstrating cross-module data sharing via the function registry.
//!
//! This module uses `declare_all_init_functions!` to register both a bootstrap extension and an
//! HTTP filter from a single dynamic module. The bootstrap extension performs asynchronous
//! initialization, populates a routing table, and registers a lookup function in the process-wide
//! function registry. The HTTP filter resolves this function via `get_function` during config
//! creation and calls it on every request to perform routing decisions.
//!
//! This pattern demonstrates the value of the function registry: when multiple dynamic modules
//! are loaded in the same process, they can share data through registered functions without
//! needing direct access to each other's globals. In this test, both the bootstrap extension and
//! HTTP filter happen to be in the same shared object, but the HTTP filter deliberately avoids
//! accessing the `ROUTING_TABLE` static directly — it resolves the lookup function by name,
//! exactly as a separate module would.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock};

/// Routing table populated by the bootstrap extension. The HTTP filter does NOT access this
/// directly — it uses the function registry instead.
static ROUTING_TABLE: OnceLock<Arc<RoutingTable>> = OnceLock::new();

declare_all_init_functions!(
  my_program_init,
  bootstrap: my_new_bootstrap_extension_config_fn,
  http: my_new_http_filter_config_fn,
);

fn my_program_init() -> bool {
  true
}

// -------------------------------------------------------------------------------------
// Shared state.
// -------------------------------------------------------------------------------------

/// A routing table that maps service names to their resolved endpoints.
struct RoutingTable {
  routes: HashMap<String, String>,
}

impl RoutingTable {
  /// Creates a new routing table populated with test service entries.
  fn new() -> Self {
    let mut routes = HashMap::new();
    routes.insert("service-a".to_string(), "10.0.0.1:8080".to_string());
    routes.insert("service-b".to_string(), "10.0.0.2:9090".to_string());
    Self { routes }
  }

  /// Returns the resolved endpoint for the given service name, if onboarded.
  fn get_route(&self, service: &str) -> Option<&str> {
    self.routes.get(service).map(|s| s.as_str())
  }
}

// -------------------------------------------------------------------------------------
// Functions exposed via the process-wide function registry.
// -------------------------------------------------------------------------------------

/// Looks up the endpoint for a service name in the routing table.
///
/// On success, writes the endpoint pointer and length to the output parameters and returns true.
/// On failure (service not found or routing table not initialized), returns false.
///
/// # Safety
///
/// The returned pointer is valid for the lifetime of the process because the routing table is
/// stored in a `OnceLock<Arc<...>>` that is never dropped.
extern "C" fn get_route_endpoint(
  service_ptr: *const u8,
  service_len: usize,
  out_ptr: *mut *const u8,
  out_len: *mut usize,
) -> bool {
  let service =
    unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(service_ptr, service_len)) };
  match ROUTING_TABLE.get().and_then(|t| t.get_route(service)) {
    Some(endpoint) => {
      unsafe {
        *out_ptr = endpoint.as_ptr();
        *out_len = endpoint.len();
      }
      true
    },
    None => false,
  }
}

// -------------------------------------------------------------------------------------
// Bootstrap extension.
// -------------------------------------------------------------------------------------

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let scheduler = envoy_extension_config.new_scheduler();

  // Simulate asynchronous initialization in a background thread.
  std::thread::spawn(move || {
    std::thread::sleep(std::time::Duration::from_millis(50));
    let table = Arc::new(RoutingTable::new());
    ROUTING_TABLE.set(table).ok();
    envoy_log_info!("async initialization complete, scheduling readiness signal");
    scheduler.commit(1);
  });

  Some(Box::new(CombinedBootstrapConfig {}))
}

struct CombinedBootstrapConfig {}

impl BootstrapExtensionConfig for CombinedBootstrapConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(CombinedBootstrapExtension {})
  }

  fn on_scheduled(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    event_id: u64,
  ) {
    if event_id == 1 {
      // Register the lookup function in the process-wide function registry.
      let registered = unsafe {
        register_function(
          "get_route_endpoint",
          get_route_endpoint as *const std::ffi::c_void,
        )
      };
      envoy_log_info!("function registry registration: {}", registered);

      envoy_extension_config.signal_init_complete();
      envoy_log_info!("bootstrap init signaled complete after async initialization");
    }
  }
}

struct CombinedBootstrapExtension {}

impl BootstrapExtension for CombinedBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("combined module: server initialized");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("combined module: worker thread initialized");
  }

  fn on_shutdown(
    &mut self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    completion: CompletionCallback,
  ) {
    envoy_log_info!("combined module: shutdown");
    completion.done();
  }
}

// -------------------------------------------------------------------------------------
// HTTP filter — uses the function registry to access bootstrap-loaded data.
// -------------------------------------------------------------------------------------

/// Function pointer type for the registered `get_route_endpoint` function.
type GetRouteEndpointFn = extern "C" fn(*const u8, usize, *mut *const u8, *mut usize) -> bool;

fn my_new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  // Note: The function registry may not yet contain the bootstrap-registered function at this
  // point because HTTP filter configs are created during Envoy config loading, which happens
  // before the bootstrap init target is signaled. Resolution is deferred to request time.
  envoy_log_info!("http filter config created (function resolution deferred to request time)");
  Some(Box::new(CombinedHttpFilterConfig {}))
}

struct CombinedHttpFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for CombinedHttpFilterConfig {
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(CombinedHttpFilter {})
  }
}

struct CombinedHttpFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for CombinedHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Resolve the lookup function from the process-wide function registry. This is safe because
    // the bootstrap init target has been signaled before any traffic is allowed through.
    let fn_ptr = match get_function("get_route_endpoint") {
      Some(ptr) => ptr,
      None => {
        envoy_log_error!("get_route_endpoint not found in function registry");
        envoy_filter.send_response(
          503,
          vec![("x-error-reason", b"function_not_registered")],
          Some(b"routing function not registered"),
          Some("function_not_registered"),
        );
        return abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
      },
    };
    let get_route: GetRouteEndpointFn = unsafe { std::mem::transmute(fn_ptr) };

    // Read the target service name from the request header.
    let service = envoy_filter
      .get_request_header_value("x-target-service")
      .map(|v| String::from_utf8_lossy(v.as_slice()).to_string());

    match service {
      Some(svc) => {
        let mut endpoint_ptr: *const u8 = std::ptr::null();
        let mut endpoint_len: usize = 0;
        let found = get_route(
          svc.as_ptr(),
          svc.len(),
          &mut endpoint_ptr,
          &mut endpoint_len,
        );

        if found {
          let endpoint = unsafe {
            std::str::from_utf8_unchecked(std::slice::from_raw_parts(endpoint_ptr, endpoint_len))
          };
          envoy_filter.set_request_header("x-routed-to", endpoint.as_bytes());
          envoy_log_info!("routed service '{}' to endpoint '{}'", svc, endpoint);
          abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
        } else {
          envoy_filter.send_response(
            503,
            vec![("x-error-reason", b"service_not_onboarded")],
            Some(format!("service '{}' is not onboarded", svc).as_bytes()),
            Some("service_not_onboarded"),
          );
          abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
        }
      },
      None => {
        // No target service header: pass through without modification.
        abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
      },
    }
  }
}
