//! Test fixture .so for the reverse tunnel reporter ABI hooks.
//!
//! Implements `ReverseTunnelReporter` and wires the five C ABI hooks via
//! `declare_reverse_tunnel_reporter!`. Each hook writes a stderr log line
//! that the e2e test scrapes:
//!
//! - `reverse_tunnel_reporter: on_reverse_tunnel_server_initialized`
//! - `reverse_tunnel_reporter: on_reverse_tunnel_connected node=N cluster=C tenant=T`
//! - `reverse_tunnel_reporter: on_reverse_tunnel_disconnected node=N cluster=C`

use envoy_proxy_dynamic_modules_rust_sdk::declare_reverse_tunnel_reporter;
use envoy_proxy_dynamic_modules_rust_sdk::reverse_tunnel::ReverseTunnelReporter;

const PREFIX: &str = "reverse_tunnel_reporter:";

struct TestReporter;

impl ReverseTunnelReporter for TestReporter {
  fn new(_config: &[u8]) -> Self {
    TestReporter
  }
  fn on_server_initialized(&mut self) {
    eprintln!("{PREFIX} on_reverse_tunnel_server_initialized");
  }
  fn on_connected(&mut self, node_id: &str, cluster_id: &str, tenant_id: &str) {
    eprintln!(
      "{PREFIX} on_reverse_tunnel_connected node={node_id} cluster={cluster_id} tenant={tenant_id}",
    );
  }
  fn on_disconnected(&mut self, node_id: &str, cluster_id: &str) {
    eprintln!("{PREFIX} on_reverse_tunnel_disconnected node={node_id} cluster={cluster_id}",);
  }
}

declare_reverse_tunnel_reporter!(TestReporter);

// envoy_dynamic_module_on_program_init is required by Envoy's loader for every
// dynamic module. The SDK's `declare_init_functions!` macros only target
// modules with a factory function (HTTP/network filters, bootstrap, etc.); for
// a reporter-only module we define it manually and return the ABI version.
#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const std::os::raw::c_char {
  envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
    as *const std::os::raw::c_char
}
