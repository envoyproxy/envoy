//! Listener filter integration test module. Mirror of
//! test_data/go/listener_integration_test/listener_integration_test.go.
//!
//! Registers a single filter "test_filter" that returns Continue from on_accept and
//! exercises a couple of read-only handle accessors.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_listener_filter_init_functions!(init, new_filter_config);

fn init() -> bool {
  true
}

fn new_filter_config<EC: EnvoyListenerFilterConfig, ELF: EnvoyListenerFilter>(
  _envoy: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn ListenerFilterConfig<ELF>>> {
  match name {
    "test_filter" => Some(Box::new(PassthroughConfig {})),
    _ => None,
  }
}

struct PassthroughConfig {}

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for PassthroughConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(PassthroughFilter {})
  }
}

struct PassthroughFilter {}

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for PassthroughFilter {
  fn on_accept(
    &mut self,
    envoy: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    let _ = envoy.get_remote_address();
    let _ = envoy.get_local_address();
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }
}
