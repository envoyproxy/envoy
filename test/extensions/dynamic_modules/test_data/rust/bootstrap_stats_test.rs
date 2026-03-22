//! Test module for Bootstrap extension stats access functionality.
//!
//! This module tests the stats access callbacks that allow Bootstrap extensions to read
//! counter values, gauge values, and histogram summaries from the Envoy stats store.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
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
