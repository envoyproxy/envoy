//! Test module for the bootstrap active-resource-name accessors.
//!
//! On an admin request it fetches each kind of active resource by name via
//! `active_resource_names(kind)`, then performs a subset check: every expected name must be present
//! in its kind's set. The observed names and the check result are returned in the response body.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::BTreeSet;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let registered = envoy_extension_config.register_admin_handler(
    "/config_names",
    "Dump live config-object names by kind.",
    true,
    false,
  );
  assert!(registered, "admin handler registration should succeed");
  envoy_extension_config.signal_init_complete();
  Some(Box::new(ConfigNamesTestConfig {}))
}

struct ConfigNamesTestConfig {}

impl BootstrapExtensionConfig for ConfigNamesTestConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(ConfigNamesTestExtension {})
  }

  fn on_admin_request(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    _path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    // Fetch each kind of active resource by name and bucket them.
    let names = |kind| {
      envoy_extension_config
        .active_resource_names(kind)
        .into_iter()
        .collect::<BTreeSet<String>>()
    };
    let filter_chains = names(ActiveResourceKind::FilterChain);
    let clusters = names(ActiveResourceKind::Cluster);
    let transport_socket_matches = names(ActiveResourceKind::TransportSocketMatch);
    let secrets = names(ActiveResourceKind::Secret);

    // Every expected name must be present in its kind's set (a subset check).
    let expected_clusters = ["cluster_0"];
    let expected_filter_chains = ["chain_0"];
    let present = expected_clusters.iter().all(|n| clusters.contains(*n))
      && expected_filter_chains
        .iter()
        .all(|n| filter_chains.contains(*n));

    let join = |set: &BTreeSet<String>| set.iter().cloned().collect::<Vec<_>>().join(",");
    let body = format!(
      "clusters=[{}] filter_chains=[{}] transport_socket_matches=[{}] secrets=[{}] present={}",
      join(&clusters),
      join(&filter_chains),
      join(&transport_socket_matches),
      join(&secrets),
      present,
    );
    (200, body)
  }
}

struct ConfigNamesTestExtension {}

impl BootstrapExtension for ConfigNamesTestExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap config names test: server initialized");
  }
}
