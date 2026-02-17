//! Test module for Bootstrap extension timer functionality.
//!
//! This module tests the timer API that allows bootstrap extensions to create timers on the main
//! thread event loop. It creates a timer during config_new, arms it with a short delay, and
//! verifies that on_timer_fired is called.

use envoy_proxy_dynamic_modules_rust_sdk::*;
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
  // Create a timer on the main thread dispatcher.
  let timer = envoy_extension_config.new_timer();

  // Verify the timer is not enabled upon creation.
  assert!(
    !timer.enabled(),
    "Timer should not be enabled upon creation"
  );

  // Arm the timer with a short delay.
  timer.enable(Duration::from_millis(10));
  assert!(timer.enabled(), "Timer should be enabled after arming");

  envoy_log_info!("Timer created and armed during config_new");

  envoy_extension_config.signal_init_complete();
  Some(Box::new(TimerTestBootstrapExtensionConfig {
    timer: Mutex::new(Some(timer)),
  }))
}

struct TimerTestBootstrapExtensionConfig {
  timer: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
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
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
    envoy_log_info!("Bootstrap timer test completed successfully!");
    // Disable and drop the timer to clean up.
    let mut timer_guard = self.timer.lock().unwrap();
    *timer_guard = None;
  }
}

struct TimerTestBootstrapExtension {}

impl BootstrapExtension for TimerTestBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Timer test bootstrap extension server initialized");
  }
}
