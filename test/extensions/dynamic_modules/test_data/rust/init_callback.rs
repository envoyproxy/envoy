use envoy_proxy_dynamic_modules_rust_sdk::*;
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
fn new_nop_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  None
}
