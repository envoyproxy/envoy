//! Integration test module for local address selector dynamic modules.
//!
//! This module registers a local address selector through the `local_address_selector:` arm of
//! `declare_all_init_functions!`. The `selector_name` picks the behavior:
//!   `select`  binds every connection to the IP address in `selector_config`, with port 0.
//!   `default` keeps the address that the default selector picks from the bind config.
//!   `fail`    fails every connection.
//!
//! Any other name is rejected, which makes Envoy reject the cluster.

use envoy_proxy_dynamic_modules_rust_sdk::local_address_selector::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::net::{IpAddr, SocketAddr};

declare_all_init_functions!(init, local_address_selector: new_local_address_selector_config_fn);

fn init() -> bool {
  true
}

fn new_local_address_selector_config_fn(
  name: &str,
  config: &[u8],
  _cluster_name: &str,
) -> Option<Box<dyn LocalAddressSelectorConfig>> {
  let selection = match name {
    "select" => {
      let address = std::str::from_utf8(config).ok()?.parse::<IpAddr>().ok()?;
      LocalAddressSelection::Address(address, 0)
    },
    "default" => LocalAddressSelection::Default,
    "fail" => LocalAddressSelection::Fail,
    _ => return None,
  };
  Some(Box::new(FixedSelection { selection }))
}

/// Selects the same result for every connection.
struct FixedSelection {
  selection: LocalAddressSelection,
}

impl LocalAddressSelectorConfig for FixedSelection {
  fn select(&self, _endpoint: SocketAddr) -> LocalAddressSelection {
    self.selection
  }
}
