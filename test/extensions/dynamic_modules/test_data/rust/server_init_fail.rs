use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_init_functions!(init, new_nop_http_filter_config_fn);
declare_server_init_function!(init_server);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ServerInitFunction`] signature.
fn init_server(
  _server_factory_context: abi::envoy_dynamic_module_type_server_factory_context_envoy_ptr,
) -> bool {
  false
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
