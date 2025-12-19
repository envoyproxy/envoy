use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};

declare_network_filter_init_functions!(init, new_nop_network_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

static SOME_VARIABLE: AtomicI32 = AtomicI32::new(1);

#[no_mangle]
pub extern "C" fn getNetworkSomeVariable() -> i32 {
  SOME_VARIABLE.fetch_add(1, Ordering::SeqCst)
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewNetworkFilterConfigFunction`]
/// signature.
fn new_nop_network_filter_config_fn<EC: EnvoyNetworkFilterConfig, ENF: EnvoyNetworkFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn NetworkFilterConfig<ENF>>> {
  let name = name.to_string();
  let config = String::from_utf8(config.to_owned()).unwrap_or_default();
  Some(Box::new(NopNetworkFilterConfig { name, config }))
}

/// A no-op network filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::NetworkFilterConfig`] as well as the [`Drop`] to test
/// the cleanup of the configuration.
struct NopNetworkFilterConfig {
  name: String,
  config: String,
}

impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for NopNetworkFilterConfig {
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
    Box::new(NopNetworkFilter {
      on_new_connection_called: false,
      on_read_called: false,
      on_write_called: false,
    })
  }
}

impl Drop for NopNetworkFilterConfig {
  fn drop(&mut self) {
    // Config cleanup verification.
    envoy_log_debug!(
      "Dropping NopNetworkFilterConfig: name={}, config={}",
      self.name,
      self.config
    );
  }
}

/// A no-op network filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::NetworkFilter`]
/// as well as the [`Drop`] to test the cleanup of the filter.
struct NopNetworkFilter {
  on_new_connection_called: bool,
  on_read_called: bool,
  on_write_called: bool,
}

impl Drop for NopNetworkFilter {
  fn drop(&mut self) {
    // Filter cleanup verification.
    envoy_log_debug!(
      "Dropping NopNetworkFilter: new_conn={}, read={}, write={}",
      self.on_new_connection_called,
      self.on_read_called,
      self.on_write_called
    );
  }
}

impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for NopNetworkFilter {
  fn on_new_connection(
    &mut self,
    _envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self.on_new_connection_called = true;
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_read(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self.on_read_called = true;
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_write(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self.on_write_called = true;
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
    // No-Op Event handling.
  }

  fn on_destroy(&mut self, _envoy_filter: &mut ENF) {
    // No-Op Filter destruction.
  }
}
