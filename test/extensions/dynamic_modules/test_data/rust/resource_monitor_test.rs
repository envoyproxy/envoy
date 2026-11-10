//! Test module for dynamic module resource monitors.
//!
//! The configuration of the runtime pressure monitors is the runtime key that holds their
//! pressure, so tests change the pressure through the runtime. The configuration of the admission
//! monitor is the number of requests it accepts.

use envoy_proxy_dynamic_modules_rust_sdk::resource_monitor::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{channel, Sender};
use std::sync::Mutex;
use std::thread::JoinHandle;

declare_all_init_functions!(
  init,
  resource_monitor: new_resource_monitor,
  synchronous_feedback_resource_monitor: new_synchronous_feedback_resource_monitor
);

fn init() -> bool {
  true
}

fn new_synchronous_feedback_resource_monitor(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn SynchronousFeedbackResourceMonitor>> {
  if name != "admission" {
    return None;
  }
  Some(Box::new(AdmissionMonitor {
    accepted_load_limit: std::str::from_utf8(config).ok()?.parse().ok()?,
    accepted_load: AtomicU64::new(0),
  }))
}

fn new_resource_monitor(
  name: &str,
  config: &[u8],
  envoy_config: &mut EnvoyResourceMonitorConfig,
) -> Option<Box<dyn ResourceMonitor>> {
  let runtime_key = String::from_utf8(config.to_vec()).ok()?;
  match name {
    "runtime_pressure" => Some(Box::new(RuntimePressureMonitor { runtime_key })),
    "pending_runtime_pressure" => Some(Box::new(PendingRuntimePressureMonitor::new(
      runtime_key,
      envoy_config,
    ))),
    "empty_config" => runtime_key
      .is_empty()
      .then(|| Box::new(FailureMonitor) as Box<dyn ResourceMonitor>),
    "panic" => panic!("intentional panic while creating the resource monitor"),
    _ => None,
  }
}

/// Reports the pressure that its runtime key holds.
struct RuntimePressureMonitor {
  runtime_key: String,
}

impl ResourceMonitor for RuntimePressureMonitor {
  fn update_resource_usage(&self) -> ResourceMonitorUpdate {
    ResourceMonitorUpdate::Pressure(get_runtime_number(&self.runtime_key, 0.0))
  }
}

/// Leaves every update pending and has a module thread report the pressure that its runtime key
/// holds. The runtime is only reachable on the main thread, so the update reads it there.
struct PendingRuntimePressureMonitor {
  runtime_key: String,
  sender: Mutex<Option<Sender<f64>>>,
  worker: Mutex<Option<JoinHandle<()>>>,
}

impl PendingRuntimePressureMonitor {
  fn new(runtime_key: String, envoy_config: &mut EnvoyResourceMonitorConfig) -> Self {
    let scheduler = envoy_config.new_config_scheduler();
    let (sender, receiver) = channel::<f64>();
    let worker = std::thread::spawn(move || {
      while let Ok(resource_pressure) = receiver.recv() {
        scheduler.report_resource_usage(resource_pressure);
      }
    });
    Self {
      runtime_key,
      sender: Mutex::new(Some(sender)),
      worker: Mutex::new(Some(worker)),
    }
  }
}

impl ResourceMonitor for PendingRuntimePressureMonitor {
  fn update_resource_usage(&self) -> ResourceMonitorUpdate {
    if let Some(sender) = self.sender.lock().unwrap().as_ref() {
      let _ = sender.send(get_runtime_number(&self.runtime_key, 0.0));
    }
    ResourceMonitorUpdate::Pending
  }
}

impl Drop for PendingRuntimePressureMonitor {
  fn drop(&mut self) {
    // Close the channel so the worker thread exits, then wait for it to finish.
    self.sender.lock().unwrap().take();
    if let Some(worker) = self.worker.lock().unwrap().take() {
      let _ = worker.join();
    }
  }
}

/// Fails every update.
struct FailureMonitor;

impl ResourceMonitor for FailureMonitor {
  fn update_resource_usage(&self) -> ResourceMonitorUpdate {
    ResourceMonitorUpdate::Failure
  }
}

/// Reports full pressure once the HTTP connection manager has accepted the configured number of
/// requests.
struct AdmissionMonitor {
  accepted_load_limit: u64,
  accepted_load: AtomicU64,
}

impl SynchronousFeedbackResourceMonitor for AdmissionMonitor {
  fn resource_usage(&self) -> f64 {
    if self.accepted_load.load(Ordering::Relaxed) >= self.accepted_load_limit {
      1.0
    } else {
      0.0
    }
  }

  fn on_load_accepted(&self, load_shed_point_name: EnvoyBuffer<'_>) {
    if load_shed_point_name.as_slice()
      == b"envoy.load_shed_points.http_connection_manager_decode_headers"
    {
      self.accepted_load.fetch_add(1, Ordering::Relaxed);
    }
  }
}
