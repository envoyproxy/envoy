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
    // Exercise the buffer-based snapshot callbacks to prove names decode straight into a
    // module-owned buffer during flush. A single buffer is reused across every entry, starting
    // empty so the first decode exercises the SDK grow-and-retry path. This is the allocation-free
    // pattern the API enables (for example writing each name to a socket).
    let mut name = Vec::new();
    let mut value = Vec::new();
    for index in 0..snapshot.counter_count() {
      let _ = snapshot.counter(index, &mut name);
    }
    // Decode every gauge name and look for the always-present "server.uptime" gauge, which proves a
    // name round-trips byte-for-byte through the buffer API end to end.
    let mut found_uptime = false;
    for index in 0..snapshot.gauge_count() {
      if snapshot.gauge(index, &mut name).is_some() && name.as_slice() == b"server.uptime" {
        found_uptime = true;
      }
    }
    for index in 0..snapshot.text_readout_count() {
      let _ = snapshot.text_readout(index, &mut name, &mut value);
    }
    if found_uptime {
      envoy_log_info!("stat sink integration test: found gauge server.uptime");
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
