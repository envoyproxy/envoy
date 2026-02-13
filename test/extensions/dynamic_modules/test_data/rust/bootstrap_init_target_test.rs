//! Test module for Bootstrap extension init target functionality.
//!
//! This module tests that Envoy automatically registers an init target for every bootstrap
//! extension, blocking traffic until the module signals readiness via signal_init_complete.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  // Envoy automatically registers an init target for every bootstrap extension.
  // Signal completion immediately since this test does not require asynchronous initialization.
  envoy_extension_config.signal_init_complete();
  envoy_log_info!("Init target signaled complete during config creation");

  Some(Box::new(InitTargetTestBootstrapExtensionConfig {}))
}

struct InitTargetTestBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for InitTargetTestBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(InitTargetTestBootstrapExtension {})
  }
}

struct InitTargetTestBootstrapExtension {}

impl BootstrapExtension for InitTargetTestBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap init target test: server initialized after init target completed");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap init target test completed successfully!");
  }
}
