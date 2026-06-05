//! Integration test module for stats sink dynamic modules.
//!
//! The lifecycle and event hooks emit log markers via `envoy_log_info!`. The C++ integration
//! test greps Envoy's log output for these markers to verify the sink was loaded, configured,
//! flushed at least once, and received histogram observations.

use envoy_proxy_dynamic_modules_rust_sdk::stats_sink::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_stat_sink_init_functions!(init, new_stat_sink);

fn init() -> bool {
  true
}

fn new_stat_sink(_name: &str, _config: &[u8]) -> Option<Box<dyn StatSink>> {
  envoy_log_info!("stat sink integration test: config_new called");
  Some(Box::new(TestStatSink {}))
}

struct TestStatSink {}

impl StatSink for TestStatSink {
  fn on_flush(&self, snapshot: &MetricSnapshot<'_>) {
    // Exercise the snapshot read-back callbacks to prove they are usable during flush. The specific
    // counts depend on the test setup, so we just log them.
    for counter in snapshot.counters() {
      let _ = counter.name.as_slice();
    }
    for gauge in snapshot.gauges() {
      let _ = gauge.name.as_slice();
    }
    for text_readout in snapshot.text_readouts() {
      let _ = text_readout.value.as_slice();
    }
    envoy_log_info!(
      "stat sink integration test: flush called counters={} gauges={}",
      snapshot.counter_count(),
      snapshot.gauge_count()
    );
  }

  fn on_histogram_complete(&self, name: EnvoyBuffer<'_>, _value: u64) {
    envoy_log_info!(
      "stat sink integration test: histogram complete: {}",
      String::from_utf8_lossy(name.as_slice())
    );
  }
}

impl Drop for TestStatSink {
  fn drop(&mut self) {
    envoy_log_info!("stat sink integration test: config_destroy called");
  }
}
