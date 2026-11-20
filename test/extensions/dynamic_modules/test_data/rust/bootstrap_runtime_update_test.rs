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
  for key in [
    "dynamic_modules_test.number",
    "envoy.reloadable_features.test_feature_true",
  ] {
    let enabled = envoy_extension_config.enable_runtime_update(key);
    envoy_log_info!("Runtime update enabled for {}: {}", key, enabled);
  }
  envoy_extension_config.signal_init_complete();
  Some(Box::new(MyBootstrapExtensionConfig))
}

struct MyBootstrapExtensionConfig;

impl BootstrapExtensionConfig for MyBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(MyBootstrapExtension)
  }

  fn on_runtime_update(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    key: &str,
    value: Option<&str>,
  ) {
    let Some(value) = value else {
      envoy_log_info!("Runtime update: {} removed", key);
      return;
    };
    // The runtime getters already return the new value.
    let getter_agrees = match value {
      "true" => get_runtime_bool(key, false),
      "false" => !get_runtime_bool(key, true),
      _ => get_runtime_number(key, f64::NAN).to_string() == value,
    };
    envoy_log_info!(
      "Runtime update: {}={}, getter agrees: {}",
      key,
      value,
      getter_agrees
    );
  }
}

struct MyBootstrapExtension;

impl BootstrapExtension for MyBootstrapExtension {}
