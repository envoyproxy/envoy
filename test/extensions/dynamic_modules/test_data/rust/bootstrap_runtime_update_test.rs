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
}

struct MyBootstrapExtension;

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    for key in [
      "dynamic_modules_test.number",
      "dynamic_modules_test.ratio",
      "envoy.reloadable_features.test_feature_true",
    ] {
      let enabled = envoy_extension.enable_runtime_update(key);
      envoy_log_info!("Runtime update enabled for {}: {}", key, enabled);
    }
  }

  fn on_runtime_update(
    &mut self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    key: &str,
    value: Option<RuntimeValue>,
  ) {
    let Some(value) = value else {
      envoy_log_info!("Runtime update: {} removed", key);
      return;
    };
    // The runtime getters already return the new value.
    let getter_agrees = match value {
      RuntimeValue::Bool(value) => get_runtime_bool(key, !value) == value,
      RuntimeValue::Int(value) => get_runtime_int(key, value + 1) == value,
      RuntimeValue::Number(value) => get_runtime_number(key, value + 1.0) == value,
    };
    envoy_log_info!(
      "Runtime update: {}={:?}, getter agrees: {}",
      key,
      value,
      getter_agrees
    );
  }
}
