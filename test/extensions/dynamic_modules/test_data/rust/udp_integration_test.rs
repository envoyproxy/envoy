//! UDP listener filter integration test module. Mirror of
//! test_data/go/udp_integration_test/udp_integration_test.go.
//!
//! Two filters:
//!   "test_filter"    — passthrough; returns Continue on every datagram.
//!   "stop_iteration" — drops every datagram by returning StopIteration.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_udp_listener_filter_init_functions!(init, new_filter_config);

fn init() -> bool {
  true
}

fn new_filter_config<EC: EnvoyUdpListenerFilterConfig, ELF: EnvoyUdpListenerFilter>(
  _envoy: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn UdpListenerFilterConfig<ELF>>> {
  match name {
    "test_filter" => Some(Box::new(PassthroughConfig {})),
    "stop_iteration" => Some(Box::new(StopIterationConfig {})),
    _ => None,
  }
}

// -----------------------------------------------------------------------------
// passthrough
// -----------------------------------------------------------------------------

struct PassthroughConfig {}

impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for PassthroughConfig {
  fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
    Box::new(PassthroughFilter {})
  }
}

struct PassthroughFilter {}

impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for PassthroughFilter {
  fn on_data(
    &mut self,
    envoy: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_udp_listener_filter_status {
    // Exercise a couple of read-only accessors so they're hit at runtime.
    let _ = envoy.get_datagram_data();
    let _ = envoy.get_peer_address();
    abi::envoy_dynamic_module_type_on_udp_listener_filter_status::Continue
  }
}

// -----------------------------------------------------------------------------
// stop_iteration
// -----------------------------------------------------------------------------

struct StopIterationConfig {}

impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for StopIterationConfig {
  fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
    Box::new(StopIterationFilter {})
  }
}

struct StopIterationFilter {}

impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for StopIterationFilter {
  fn on_data(
    &mut self,
    _envoy: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_udp_listener_filter_status {
    abi::envoy_dynamic_module_type_on_udp_listener_filter_status::StopIteration
  }
}
