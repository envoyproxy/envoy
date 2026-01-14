use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init(
  server_factory_context: abi::envoy_dynamic_module_type_server_factory_context_envoy_ptr,
) -> bool {
  let concurrency = unsafe { get_server_concurrency(server_factory_context) };
  assert_eq!(concurrency, 1);
  true
}

fn my_new_bootstrap_extension_config_fn(
  _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  Some(Box::new(MyBootstrapExtensionConfig {}))
}

struct MyBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for MyBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(MyBootstrapExtension {})
  }
}

struct MyBootstrapExtension {}

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap extension server initialized from Rust!");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap extension worker thread initialized from Rust!");
  }
}
