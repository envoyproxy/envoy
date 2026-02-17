//! Test module demonstrating a combined Bootstrap Extension + HTTP Filter pattern.
//!
//! This module uses `declare_all_init_functions!` to register both a bootstrap extension and an
//! HTTP filter from a single dynamic module. This pattern is essential for integrations that
//! require server-wide initialization (e.g., Dicer clerk manager setup) followed by per-request
//! processing (e.g., routing based on slice keys).
//!
//! The bootstrap extension performs asynchronous initialization in a background thread, signals
//! readiness via `signal_init_complete`, and stores shared state in a global `OnceLock`. The HTTP
//! filter accesses the shared state during request processing (after the init target has completed
//! and traffic is allowed) for per-request routing decisions including header manipulation and
//! local reply for error cases.
//!
//! Note: The HTTP filter config is created during Envoy config loading, which happens before the
//! bootstrap init target is signaled. Therefore, shared state must NOT be accessed during HTTP
//! filter config creation — only during request processing when the init target guarantees
//! readiness.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::HashMap;
use std::sync::{Arc, OnceLock};

/// Shared routing table between the bootstrap extension and the HTTP filter.
static SHARED_ROUTING_TABLE: OnceLock<Arc<RoutingTable>> = OnceLock::new();

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
// Function exposed via the process-wide function registry.
// -------------------------------------------------------------------------------------

/// Checks whether a service is onboarded in the routing table.
///
/// This function is registered in the process-wide function registry by the bootstrap extension
/// and can be resolved by any module in the same process.
extern "C" fn is_service_onboarded(service_ptr: *const u8, service_len: usize) -> bool {
  let service =
    unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(service_ptr, service_len)) };
  SHARED_ROUTING_TABLE
    .get()
    .map(|table| table.get_route(service).is_some())
    .unwrap_or(false)
}

// -------------------------------------------------------------------------------------
// Bootstrap extension.
// -------------------------------------------------------------------------------------

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  // Create a scheduler to signal async initialization completion back to the main thread.
  let scheduler = envoy_extension_config.new_scheduler();

  // Define a counter to track initialization events.
  let init_counter = envoy_extension_config
    .define_counter("init_complete_total")
    .expect("failed to define init_complete_total counter");

  // Simulate asynchronous initialization in a background thread. In a real Dicer integration,
  // this would create a tokio runtime and call DicerClerkManager::new().
  std::thread::spawn(move || {
    std::thread::sleep(std::time::Duration::from_millis(50));
    let table = Arc::new(RoutingTable::new());
    SHARED_ROUTING_TABLE.set(table).ok();
    envoy_log_info!("async initialization complete, scheduling readiness signal");
    scheduler.commit(1);
  });

  Some(Box::new(CombinedBootstrapConfig { init_counter }))
}

struct CombinedBootstrapConfig {
  init_counter: EnvoyCounterId,
}

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
          "is_service_onboarded",
          is_service_onboarded as *const std::ffi::c_void,
        )
      };
      envoy_log_info!("function registry registration: {}", registered);

      // Increment the init counter to track successful initialization.
      envoy_extension_config
        .increment_counter(self.init_counter, 1)
        .ok();

      // Signal init complete to unblock Envoy startup.
      envoy_extension_config.signal_init_complete();
      envoy_log_info!("bootstrap init signaled complete after async initialization");
    }
  }
}

struct CombinedBootstrapExtension {}

impl BootstrapExtension for CombinedBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Note: on_server_initialized is called during the synchronous init sequence, which may
    // happen BEFORE the async init thread completes and signal_init_complete is called. Therefore,
    // shared state is NOT guaranteed to be available here. Use on_request_headers for verification.
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
// HTTP filter.
// -------------------------------------------------------------------------------------

fn my_new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  // Note: Do NOT access the shared routing table here. The HTTP filter config is created during
  // Envoy config loading, which happens before the bootstrap init target is signaled. The shared
  // state is only guaranteed to be available during request processing.
  envoy_log_info!("http filter config created");

  // Define an HTTP filter counter for tracking routed requests.
  let requests_routed_counter = envoy_filter_config
    .define_counter("requests_routed_total")
    .expect("failed to define requests_routed_total counter");

  Some(Box::new(CombinedHttpFilterConfig {
    requests_routed_counter,
  }))
}

struct CombinedHttpFilterConfig {
  requests_routed_counter: EnvoyCounterId,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for CombinedHttpFilterConfig {
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(CombinedHttpFilter {
      requests_routed_counter: self.requests_routed_counter,
    })
  }
}

struct CombinedHttpFilter {
  requests_routed_counter: EnvoyCounterId,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for CombinedHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Access the shared routing table. This is safe because the bootstrap init target has been
    // signaled before any traffic is allowed through.
    let routing_table = match SHARED_ROUTING_TABLE.get() {
      Some(table) => table,
      None => {
        // This should not happen if the init target mechanism works correctly.
        envoy_log_error!("routing table not available, init target may not have completed");
        envoy_filter.send_response(
          503,
          vec![("x-error-reason", b"routing_table_unavailable")],
          Some(b"routing table not initialized"),
          Some("routing_table_unavailable"),
        );
        return abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
      },
    };

    // Read the target service name from the request header.
    let service = envoy_filter
      .get_request_header_value("x-target-service")
      .map(|v| String::from_utf8_lossy(v.as_slice()).to_string());

    match service {
      Some(svc) => {
        if let Some(endpoint) = routing_table.get_route(&svc) {
          // Service is onboarded: set routing header and continue.
          envoy_filter.set_request_header("x-routed-to", endpoint.as_bytes());
          envoy_filter
            .increment_counter(self.requests_routed_counter, 1)
            .ok();
          envoy_log_info!("routed service '{}' to endpoint '{}'", svc, endpoint);
          abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
        } else {
          // Service is not onboarded: send 503 local reply.
          envoy_filter.send_response(
            503,
            vec![("x-error-reason", b"service_not_onboarded")],
            Some(format!("service '{}' is not onboarded for routing", svc).as_bytes()),
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
