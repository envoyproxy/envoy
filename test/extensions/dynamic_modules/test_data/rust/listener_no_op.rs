use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_listener_filter_init_functions!(init, new_nop_listener_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewListenerFilterConfigFunction`]
/// signature.
fn new_nop_listener_filter_config_fn<EC: EnvoyListenerFilterConfig, ELF: EnvoyListenerFilter>(
  _envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn ListenerFilterConfig<ELF>>> {
  Some(Box::new(NopListenerFilterConfig {}))
}

/// A no-op listener filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::ListenerFilterConfig`].
struct NopListenerFilterConfig {}

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for NopListenerFilterConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(NopListenerFilter {})
  }
}

/// A no-op listener filter that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::ListenerFilter`].
struct NopListenerFilter {}

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for NopListenerFilter {}
