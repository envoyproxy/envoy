//! Integration test module for stats sink dynamic modules.
//!
//! The lifecycle and event hooks emit log markers via `envoy_log_info!`. The C++ integration test
//! greps Envoy's log output for these markers to verify the sink was loaded, configured, flushed at
//! least once, and received histogram observations. A separate test reads the published gauge value
//! directly to verify the off-main-thread aggregation round trip.

use envoy_proxy_dynamic_modules_rust_sdk::stats_sink::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::mpsc::{channel, Sender};
use std::sync::Mutex;
use std::thread::JoinHandle;

declare_stat_sink_init_functions!(init, new_stat_sink);

fn init() -> bool {
  true
}

fn new_stat_sink(
  _name: &str,
  _config: &[u8],
  envoy_config: &mut EnvoyStatSinkConfig,
) -> Option<Box<dyn StatSink>> {
  envoy_log_info!("stat sink integration test: config_new called");

  // Define a gauge to publish the aggregated result into, and a scheduler to post that result back
  // to the main thread once it has been computed off the main thread.
  let gauge_id = envoy_config
    .define_gauge("integration_aggregated_counters")
    .expect("gauge definition must succeed during config creation");
  let scheduler = envoy_config.new_config_scheduler();

  // Aggregate snapshots on a dedicated worker thread to model a module that offloads work from the
  // main thread, for example onto a Tokio runtime. The worker owns the scheduler and commits the
  // aggregated total as the event id, which the main thread then publishes into the gauge.
  let (sender, receiver) = channel::<OwnedMetricSnapshot>();
  let worker = std::thread::spawn(move || {
    while let Ok(snapshot) = receiver.recv() {
      let total: u64 = snapshot.counters.iter().map(|counter| counter.value).sum();
      scheduler.commit(total);
    }
  });

  Some(Box::new(TestStatSink {
    gauge_id,
    sender: Mutex::new(Some(sender)),
    worker: Mutex::new(Some(worker)),
  }))
}

struct TestStatSink {
  gauge_id: EnvoyGaugeId,
  sender: Mutex<Option<Sender<OwnedMetricSnapshot>>>,
  worker: Mutex<Option<JoinHandle<()>>>,
}

impl StatSink for TestStatSink {
  fn on_flush(&self, snapshot: &MetricSnapshot<'_>) {
    // Exercise the buffer-based snapshot callbacks to prove names decode straight into a
    // module-owned buffer during flush. A single buffer is reused across every entry, starting
    // empty so the first decode exercises the SDK grow-and-retry path. This is the allocation-free
    // pattern the API enables (for example writing each name to a socket).
    let mut name = Vec::new();
    let mut value = Vec::new();
    let mut tag_name = Vec::new();
    let mut tag_value = Vec::new();
    // Exercise the counter tag callbacks against every counter in the live snapshot. Every tag
    // index the count reports must resolve; a mismatch means the count and the per-tag reader
    // disagree.
    let mut counter_tags_consistent = true;
    for index in 0..snapshot.counter_count() {
      let _ = snapshot.counter(index, &mut name);
      if !snapshot.counter_tag_extracted_name(index, &mut name) {
        counter_tags_consistent = false;
        continue;
      }
      let Some(tag_count) = snapshot.counter_tag_count(index) else {
        counter_tags_consistent = false;
        continue;
      };
      for tag_index in 0..tag_count {
        if !snapshot.counter_tag(index, tag_index, &mut tag_name, &mut tag_value) {
          counter_tags_consistent = false;
        }
      }
    }
    // Decode every gauge name and look for the always-present "server.uptime" gauge, which proves a
    // name round-trips byte-for-byte through the buffer API end to end.
    let mut found_uptime = false;
    // Reconstruct the dimensional name of a tagged gauge from its tag-extracted name and tags, the
    // way a Prometheus-style sink would. "cluster.cluster_0.membership_total" carries a single
    // "envoy.cluster_name" tag whose value is the cluster name, so the tag callbacks must yield
    // tag-extracted name "cluster.membership_total" and tag ("envoy.cluster_name", "cluster_0").
    let mut found_tagged_gauge = false;
    for index in 0..snapshot.gauge_count() {
      if snapshot.gauge(index, &mut name).is_some() && name.as_slice() == b"server.uptime" {
        found_uptime = true;
      }
      if snapshot.gauge_tag_extracted_name(index, &mut name)
        && name.as_slice() == b"cluster.membership_total"
        && snapshot.gauge_tag_count(index) == Some(1)
        && snapshot.gauge_tag(index, 0, &mut tag_name, &mut tag_value)
        && tag_name.as_slice() == b"envoy.cluster_name"
        && tag_value.as_slice() == b"cluster_0"
      {
        found_tagged_gauge = true;
      }
    }
    for index in 0..snapshot.text_readout_count() {
      let _ = snapshot.text_readout(index, &mut name, &mut value);
    }
    if found_uptime {
      envoy_log_info!("stat sink integration test: found gauge server.uptime");
    }
    // A tagged gauge reconstructed from the tag callbacks, and every counter's tag count agreed
    // with its per-tag reads: the tag ABI round-trips end to end against a live snapshot.
    if found_tagged_gauge && counter_tags_consistent {
      envoy_log_info!("stat sink integration test: reconstructed tagged gauge cluster.membership_total envoy.cluster_name=cluster_0");
    }
    envoy_log_info!(
      "stat sink integration test: flush called counters={} gauges={}",
      snapshot.counter_count(),
      snapshot.gauge_count()
    );
    // Copy the snapshot so it outlives this call, then hand it to the worker thread to aggregate.
    if let Some(sender) = self.sender.lock().unwrap().as_ref() {
      let _ = sender.send(snapshot.to_owned());
    }
  }

  fn on_histogram_complete(&self, name: EnvoyBuffer<'_>, _value: u64) {
    envoy_log_info!(
      "stat sink integration test: histogram complete: {}",
      String::from_utf8_lossy(name.as_slice())
    );
  }

  fn on_config_scheduled(&self, envoy_config: &mut EnvoyStatSinkConfig, event_id: u64) {
    // Runs on the main thread. Publish the value aggregated off the main thread into the gauge.
    let _ = envoy_config.set_gauge(self.gauge_id, event_id);
    envoy_log_info!(
      "stat sink integration test: scheduled publish event_id={}",
      event_id
    );
  }
}

impl Drop for TestStatSink {
  fn drop(&mut self) {
    // Close the channel so the worker thread exits, then wait for it to finish.
    self.sender.lock().unwrap().take();
    if let Some(worker) = self.worker.lock().unwrap().take() {
      let _ = worker.join();
    }
    envoy_log_info!("stat sink integration test: config_destroy called");
  }
}
