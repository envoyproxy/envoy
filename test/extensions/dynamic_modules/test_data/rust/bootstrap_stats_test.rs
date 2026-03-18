//! Test module for Bootstrap extension stats access and metrics definition functionality.
//!
//! This module tests the stats access callbacks that allow Bootstrap extensions to read
//! counter values, gauge values, and histogram summaries from the Envoy stats store.
//! It also tests the metrics definition and update callbacks that allow Bootstrap extensions
//! to create and update their own counters, gauges, and histograms.

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
  // Define metrics during config creation.
  let counter_id = envoy_extension_config
    .define_counter("refresh_success_total")
    .expect("Failed to define counter");
  let gauge_id = envoy_extension_config
    .define_gauge("connection_state")
    .expect("Failed to define gauge");
  let histogram_id = envoy_extension_config
    .define_histogram("refresh_duration_ms")
    .expect("Failed to define histogram");

  // Increment the counter.
  envoy_extension_config
    .increment_counter(counter_id, 3)
    .expect("Failed to increment counter");
  envoy_extension_config
    .increment_counter(counter_id, 2)
    .expect("Failed to increment counter");
  envoy_log_info!("Counter incremented to expected value of 5");

  // Set, increase, and decrease the gauge.
  envoy_extension_config
    .set_gauge(gauge_id, 100)
    .expect("Failed to set gauge");
  envoy_extension_config
    .increase_gauge(gauge_id, 10)
    .expect("Failed to increase gauge");
  envoy_extension_config
    .decrease_gauge(gauge_id, 30)
    .expect("Failed to decrease gauge");
  envoy_log_info!("Gauge set to expected value of 80");

  // Record histogram values.
  envoy_extension_config
    .record_histogram_value(histogram_id, 42)
    .expect("Failed to record histogram value");
  envoy_extension_config
    .record_histogram_value(histogram_id, 100)
    .expect("Failed to record histogram value");
  envoy_log_info!("Histogram values recorded successfully");

  // ---- Labeled metrics (vec variants) ----

  // Define a counter vec with labels.
  let counter_vec_id = envoy_extension_config
    .define_counter_vec("request_total", &["method", "status"])
    .expect("Failed to define counter vec");
  envoy_extension_config
    .increment_counter_vec(counter_vec_id, &["GET", "200"], 7)
    .expect("Failed to increment counter vec");
  envoy_log_info!("Counter vec incremented successfully");

  // Define a gauge vec with labels.
  let gauge_vec_id = envoy_extension_config
    .define_gauge_vec("active_connections", &["upstream"])
    .expect("Failed to define gauge vec");
  envoy_extension_config
    .set_gauge_vec(gauge_vec_id, &["svc_a"], 50)
    .expect("Failed to set gauge vec");
  envoy_extension_config
    .increase_gauge_vec(gauge_vec_id, &["svc_a"], 5)
    .expect("Failed to increase gauge vec");
  envoy_extension_config
    .decrease_gauge_vec(gauge_vec_id, &["svc_a"], 10)
    .expect("Failed to decrease gauge vec");
  envoy_log_info!("Gauge vec manipulated successfully");

  // Define a histogram vec with labels.
  let histogram_vec_id = envoy_extension_config
    .define_histogram_vec("latency_ms", &["endpoint"])
    .expect("Failed to define histogram vec");
  envoy_extension_config
    .record_histogram_value_vec(histogram_vec_id, &["backend_a"], 15)
    .expect("Failed to record histogram vec value");
  envoy_log_info!("Histogram vec recorded successfully");

  envoy_log_info!("Bootstrap metrics definition and update test completed successfully!");

  envoy_extension_config.signal_init_complete();
  Some(Box::new(StatsTestBootstrapExtensionConfig {}))
}

struct StatsTestBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for StatsTestBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(StatsTestBootstrapExtension {})
  }
}

struct StatsTestBootstrapExtension {}

impl BootstrapExtension for StatsTestBootstrapExtension {
  fn on_server_initialized(&mut self, envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Test get_counter_value - looking for a counter that should exist after server init.
    // The server.live gauge should exist.
    let live_gauge = envoy_extension.get_gauge_value("server.live");
    if let Some(value) = live_gauge {
      envoy_log_info!("Found server.live gauge with value: {}", value);
    } else {
      envoy_log_info!("server.live gauge not found (this is expected in some test configs)");
    }

    // Test iterate_counters - count how many counters exist.
    let mut counter_count = 0;
    envoy_extension.iterate_counters(&mut |name, _value| {
      counter_count += 1;
      // Log the first few counters for debugging.
      if counter_count <= 3 {
        envoy_log_debug!("Counter: {}", name);
      }
      true // Continue iteration.
    });
    envoy_log_info!("Found {} counters in stats store", counter_count);

    // Test iterate_gauges - count how many gauges exist.
    let mut gauge_count = 0;
    envoy_extension.iterate_gauges(&mut |name, _value| {
      gauge_count += 1;
      // Log the first few gauges for debugging.
      if gauge_count <= 3 {
        envoy_log_debug!("Gauge: {}", name);
      }
      true // Continue iteration.
    });
    envoy_log_info!("Found {} gauges in stats store", gauge_count);

    // Test get_counter_value with a non-existent counter.
    let missing = envoy_extension.get_counter_value("non.existent.counter");
    if missing.is_none() {
      envoy_log_info!("Correctly returned None for non-existent counter");
    }

    // Test get_gauge_value with a non-existent gauge.
    let missing = envoy_extension.get_gauge_value("non.existent.gauge");
    if missing.is_none() {
      envoy_log_info!("Correctly returned None for non-existent gauge");
    }

    // Test get_histogram_summary with a non-existent histogram.
    let missing = envoy_extension.get_histogram_summary("non.existent.histogram");
    if missing.is_none() {
      envoy_log_info!("Correctly returned None for non-existent histogram");
    }

    envoy_log_info!("Bootstrap stats access test completed successfully!");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Stats access is also available on worker threads.
    envoy_log_info!("Bootstrap extension worker thread initialized with stats access!");
  }
}
