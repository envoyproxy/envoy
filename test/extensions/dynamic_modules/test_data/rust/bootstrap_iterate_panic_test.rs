//! Test module verifying that a panic inside a stats iterator visitor is caught by the SDK
//! trampoline instead of unwinding across the C boundary and aborting Envoy.

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
  // Signal init complete so startup is not blocked on this extension.
  envoy_extension_config.signal_init_complete();
  Some(Box::new(IteratePanicBootstrapExtensionConfig {}))
}

struct IteratePanicBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for IteratePanicBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(IteratePanicBootstrapExtension {})
  }
}

struct IteratePanicBootstrapExtension {}

impl BootstrapExtension for IteratePanicBootstrapExtension {
  fn on_server_initialized(&mut self, envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Panic inside the counter visitor. The SDK trampoline must catch it so Envoy stays up.
    envoy_extension.iterate_counters(&mut |_name, _value| {
      panic!("intentional panic inside iterate_counters visitor");
    });
    envoy_log_info!("Survived panic inside iterate_counters visitor");

    // Panic inside the gauge visitor as well.
    envoy_extension.iterate_gauges(&mut |_name, _value| {
      panic!("intentional panic inside iterate_gauges visitor");
    });
    envoy_log_info!("Survived panic inside iterate_gauges visitor");

    envoy_log_info!("Bootstrap iterate panic test completed successfully!");
  }
}
