//! Test module for Bootstrap extension admin handler functionality.
//!
//! This module tests the admin handler API that allows bootstrap extensions to register custom
//! admin HTTP endpoints. It registers an admin handler during config_new and verifies that
//! on_admin_request is called when the endpoint is requested.

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
  // Register the admin handler during config_new since admin is available at this point.
  let registered = envoy_extension_config.register_admin_handler(
    "/dynamic_module_admin_test",
    "Dynamic module admin handler test endpoint.",
    true,
    false,
  );
  envoy_log_info!("Admin handler registered: {}", registered);
  assert!(registered, "Admin handler registration should succeed");

  // Signal init complete so Envoy can start accepting traffic.
  envoy_extension_config.signal_init_complete();

  Some(Box::new(AdminHandlerTestBootstrapExtensionConfig {}))
}

struct AdminHandlerTestBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for AdminHandlerTestBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(AdminHandlerTestBootstrapExtension {})
  }

  fn on_admin_request(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    method: &str,
    path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    envoy_log_info!("Admin request received: {} {}", method, path);
    (
      200,
      format!(
        "Hello from dynamic module admin handler! method={} path={}",
        method, path
      ),
    )
  }
}

struct AdminHandlerTestBootstrapExtension {}

impl BootstrapExtension for AdminHandlerTestBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Admin handler test bootstrap extension server initialized");
  }
}
