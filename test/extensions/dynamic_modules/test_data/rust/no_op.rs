use envoy_proxy_dynamic_modules_rust_sdk::declare_init_functions;
use std::sync::atomic::{AtomicI32, Ordering};

declare_init_functions!(init, new_nop_http_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

static SOME_VARIABLE: AtomicI32 = AtomicI32::new(1);

#[no_mangle]
pub extern "C" fn getSomeVariable() -> i32 {
  SOME_VARIABLE.fetch_add(1, Ordering::SeqCst)
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewHttpFilterConfigFunction`]
/// signature.
fn new_nop_http_filter_config_fn(
  _envoy_filter_factory: envoy_proxy_dynamic_modules_rust_sdk::EnvoyHttpFilterConfig,
  name: &str,
  config: &str,
) -> Option<Box<dyn envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig>> {
  let name = name.to_string();
  let config = config.to_string();
  Some(Box::new(NopHttpFilterConfig { name, config }))
}

/// A no-op HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] as well as the [`Drop`] to test the
/// cleanup of the configuration.
struct NopHttpFilterConfig {
  name: String,
  config: String,
}

impl envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig for NopHttpFilterConfig {}

impl Drop for NopHttpFilterConfig {
  fn drop(&mut self) {
    assert_eq!(self.name, "foo");
    assert_eq!(self.config, "bar");
  }
}
