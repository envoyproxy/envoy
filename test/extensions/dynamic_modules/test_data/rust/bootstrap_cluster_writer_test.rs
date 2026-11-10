//! Test module for the bootstrap extension cluster writer and worker events.
//!
//! The integration test drives this module through the `/cluster_writer?<action>=<argument>` admin
//! endpoint. The module enables the cluster writer in config_new, and a write takes the serialized
//! cluster from the request body. When the extension config carries a serialized cluster, the
//! module writes it as `writer.startup` from a scheduler event that it commits in config_new.
//! Metrics count the worker events and the writer outcomes, and the `outcomes` action returns the
//! recorded outcomes.

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
  let worker_events = envoy_extension_config
    .define_counter("worker_events_total")
    .unwrap();
  let last_done_event_id = envoy_extension_config
    .define_gauge("last_done_event_id")
    .unwrap();
  let update_done_total = envoy_extension_config
    .define_counter("update_done_total")
    .unwrap();
  envoy_extension_config.register_admin_handler(
    "/cluster_writer",
    "Cluster writer test endpoint.",
    false,
    false,
  );
  envoy_extension_config.enable_cluster_writer();
  if !config.is_empty() {
    envoy_extension_config.new_scheduler().commit(1);
  }
  envoy_extension_config.signal_init_complete();
  Some(Box::new(ClusterWriterTestConfig {
    startup_cluster: config.to_vec(),
    worker_events,
    last_done_event_id,
    update_done_total,
    outcomes: Mutex::new(Vec::new()),
  }))
}

struct ClusterWriterTestConfig {
  startup_cluster: Vec<u8>,
  worker_events: EnvoyCounterId,
  last_done_event_id: EnvoyGaugeId,
  update_done_total: EnvoyCounterId,
  outcomes: Mutex<Vec<String>>,
}

impl BootstrapExtensionConfig for ClusterWriterTestConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(ClusterWriterTestExtension {})
  }

  fn on_scheduled(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _event_id: u64,
  ) {
    let (result, update_id) = envoy_extension_config.cluster_writer_add_or_update(
      "writer.startup",
      &self.startup_cluster,
      "startup",
    );
    envoy_log_info!("startup write: {:?} {}", result, update_id);
  }

  fn on_admin_request(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    path: &str,
    body: &[u8],
  ) -> (u32, String) {
    let query = path.trim_start_matches("/cluster_writer?");
    let (action, argument) = query.split_once('=').unwrap_or((query, ""));
    let response = match action {
      "add" => {
        let (result, update_id) =
          envoy_extension_config.cluster_writer_add_or_update(argument, body, "v1");
        format!("{result:?} {update_id}")
      },
      "remove" => format!(
        "{:?}",
        envoy_extension_config.cluster_writer_remove(argument)
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

  fn on_worker_event(
    &self,
    envoy_extension_config: &dyn EnvoyBootstrapExtensionConfig,
    _event_id: u64,
  ) {
    envoy_extension_config
      .increment_counter(self.worker_events, 1)
      .unwrap();
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

  fn on_cluster_writer_update_done(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    update_id: u64,
    cluster_name: &str,
    status: abi::envoy_dynamic_module_type_bootstrap_cluster_writer_update_status,
    error_details: &str,
  ) {
    self.outcomes.lock().unwrap().push(format!(
      "{update_id} {cluster_name} {status:?} {error_details}"
    ));
    envoy_extension_config
      .increment_counter(self.update_done_total, 1)
      .unwrap();
  }
}

struct ClusterWriterTestExtension {}

impl BootstrapExtension for ClusterWriterTestExtension {}
