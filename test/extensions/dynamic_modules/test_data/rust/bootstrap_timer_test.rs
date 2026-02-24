//! Test module for Bootstrap extension timer functionality.
//!
//! This module tests the timer API that allows bootstrap extensions to create timers on the main
//! thread event loop. It creates two timers during config_new, arms them with short delays, and
//! verifies that on_timer_fired is called for each. The timer identity API (`id()`) is used to
//! distinguish which timer fired in the callback.
//!
//! Initialization is deferred until both timers have fired by calling `signal_init_complete` from
//! `on_timer_fired` only after both timers are accounted for. This guarantees the timers fire
//! before `initialize()` returns.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::time::Duration;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  // Create two timers on the main thread dispatcher.
  let timer_a = envoy_extension_config.new_timer();
  let timer_b = envoy_extension_config.new_timer();

  // Verify both timers are not enabled upon creation.
  assert!(
    !timer_a.enabled(),
    "Timer A should not be enabled upon creation"
  );
  assert!(
    !timer_b.enabled(),
    "Timer B should not be enabled upon creation"
  );

  // Verify each timer has a unique identity.
  assert_ne!(
    timer_a.id(),
    timer_b.id(),
    "Different timers must have different ids"
  );

  let timer_a_id = timer_a.id();
  let timer_b_id = timer_b.id();

  // Arm both timers with short delays.
  timer_a.enable(Duration::from_millis(10));
  timer_b.enable(Duration::from_millis(20));

  envoy_log_info!("Two timers created and armed during config_new");

  // Do NOT call signal_init_complete here. Instead, defer it until both timers have fired in
  // on_timer_fired. This ensures the event loop keeps running and both timers fire before
  // initialize() returns.
  Some(Box::new(TimerTestBootstrapExtensionConfig {
    timer_a: Mutex::new(Some(timer_a)),
    timer_b: Mutex::new(Some(timer_b)),
    timer_a_id,
    timer_b_id,
    timer_a_fired: AtomicBool::new(false),
    timer_b_fired: AtomicBool::new(false),
  }))
}

struct TimerTestBootstrapExtensionConfig {
  timer_a: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  timer_b: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  timer_a_id: usize,
  timer_b_id: usize,
  timer_a_fired: AtomicBool,
  timer_b_fired: AtomicBool,
}

impl BootstrapExtensionConfig for TimerTestBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(TimerTestBootstrapExtension {})
  }

  fn on_timer_fired(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
    let fired_id = timer.id();

    if fired_id == self.timer_a_id {
      envoy_log_info!("Timer A fired, identified by id");
      self.timer_a_fired.store(true, Ordering::SeqCst);
      // Drop timer A to clean up.
      let mut guard = self.timer_a.lock().unwrap();
      *guard = None;
    } else if fired_id == self.timer_b_id {
      envoy_log_info!("Timer B fired, identified by id");
      self.timer_b_fired.store(true, Ordering::SeqCst);
      // Drop timer B to clean up.
      let mut guard = self.timer_b.lock().unwrap();
      *guard = None;
    } else {
      panic!("Unknown timer fired with id: {}", fired_id);
    }

    // Signal init complete and log success once both timers have fired.
    if self.timer_a_fired.load(Ordering::SeqCst) && self.timer_b_fired.load(Ordering::SeqCst) {
      envoy_extension_config.signal_init_complete();
      envoy_log_info!("Bootstrap timer test completed successfully!");
    }
  }
}

struct TimerTestBootstrapExtension {}

impl BootstrapExtension for TimerTestBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Timer test bootstrap extension server initialized");
  }
}
