//! Test module for the bootstrap extension listener writer.
//!
//! The integration test drives this module through the `/writer?<action>=<argument>` admin
//! endpoint. The module enables the listener writer in config_new, and a write takes the
//! serialized listener from the request body. When the extension config carries a serialized
//! listener, the module writes it as `writer.startup` from a scheduler event that it commits in
//! config_new, and only then signals that its initialization is complete. Metrics count the writer
//! outcomes, and the `outcomes` action returns the recorded outcomes.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Mutex;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let last_done_event_id = envoy_extension_config
    .define_gauge("last_done_event_id")
    .unwrap();
  let update_done_total = envoy_extension_config
    .define_counter("update_done_total")
    .unwrap();
  envoy_extension_config.register_admin_handler("/writer", "Writer test endpoint.", false, false);
  envoy_extension_config.enable_listener_writer();
  if config.is_empty() {
    envoy_extension_config.signal_init_complete();
  } else {
    envoy_extension_config.new_scheduler().commit(1);
  }
  Some(Box::new(WriterTestConfig {
    startup_listener: config.to_vec(),
    last_done_event_id,
    update_done_total,
    outcomes: Mutex::new(Vec::new()),
  }))
}

struct WriterTestConfig {
  startup_listener: Vec<u8>,
  last_done_event_id: EnvoyGaugeId,
  update_done_total: EnvoyCounterId,
  outcomes: Mutex<Vec<String>>,
}

impl BootstrapExtensionConfig for WriterTestConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(WriterTestExtension {})
  }

  fn on_scheduled(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _event_id: u64,
  ) {
    let (result, update_id) = envoy_extension_config.listener_writer_add_or_update(
      "writer.startup",
      &self.startup_listener,
      "startup",
    );
    envoy_log_info!("startup write: {:?} {}", result, update_id);
    envoy_extension_config.signal_init_complete();
  }

  fn on_admin_request(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    path: &str,
    body: &[u8],
  ) -> (u32, String) {
    let query = path.trim_start_matches("/writer?");
    let (action, argument) = query.split_once('=').unwrap_or((query, ""));
    let response = match action {
      "add" => {
        let (result, update_id) =
          envoy_extension_config.listener_writer_add_or_update(argument, body, "v1");
        format!("{result:?} {update_id}")
      },
      "remove" => format!(
        "{:?}",
        envoy_extension_config.listener_writer_remove(argument)
      ),
      "run_on_all_workers" => format!(
        "{}",
        envoy_extension_config.run_on_all_workers(argument.parse().unwrap_or(0))
      ),
      "outcomes" => self.outcomes.lock().unwrap().join("\n"),
      _ => return (404, String::new()),
    };
    (200, response)
  }

  fn on_run_on_all_workers_done(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    event_id: u64,
  ) {
    envoy_extension_config
      .set_gauge(self.last_done_event_id, event_id)
      .unwrap();
  }

  fn on_listener_writer_update_done(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    update_id: u64,
    listener_name: &str,
    status: abi::envoy_dynamic_module_type_bootstrap_listener_writer_update_status,
    error_details: &str,
  ) {
    self.outcomes.lock().unwrap().push(format!(
      "{update_id} {listener_name} {status:?} {error_details}"
    ));
    envoy_extension_config
      .increment_counter(self.update_done_total, 1)
      .unwrap();
  }
}

struct WriterTestExtension {}

impl BootstrapExtension for WriterTestExtension {}
