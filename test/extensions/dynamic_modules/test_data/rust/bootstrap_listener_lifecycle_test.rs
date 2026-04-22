use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::{Arc, Mutex};

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let scheduler = envoy_extension_config.new_scheduler();
  Some(Box::new(MyBootstrapExtensionConfig {
    scheduler: Arc::new(Mutex::new(scheduler)),
  }))
}

const ENABLE_LISTENER_LIFECYCLE_EVENT: u64 = 1;

struct MyBootstrapExtensionConfig {
  scheduler: Arc<Mutex<Box<dyn EnvoyBootstrapExtensionConfigScheduler>>>,
}

impl BootstrapExtensionConfig for MyBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(MyBootstrapExtension {
      scheduler: self.scheduler.clone(),
    })
  }

  fn on_scheduled(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    event_id: u64,
  ) {
    if event_id == ENABLE_LISTENER_LIFECYCLE_EVENT {
      let result = envoy_extension_config.enable_listener_lifecycle();
      envoy_log_info!("Listener lifecycle enabled: {}", result);
      envoy_extension_config.signal_init_complete();
    }
  }

  fn on_listener_add_or_update(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    listener_name: &str,
  ) {
    envoy_log_info!("Listener added or updated: {}", listener_name);
  }

  fn on_listener_removal(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    listener_name: &str,
  ) {
    envoy_log_info!("Listener removed: {}", listener_name);
  }
}

struct MyBootstrapExtension {
  scheduler: Arc<Mutex<Box<dyn EnvoyBootstrapExtensionConfigScheduler>>>,
}

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap listener lifecycle test: server initialized");
    let scheduler = self.scheduler.lock().unwrap();
    scheduler.commit(ENABLE_LISTENER_LIFECYCLE_EVENT);
  }
}
