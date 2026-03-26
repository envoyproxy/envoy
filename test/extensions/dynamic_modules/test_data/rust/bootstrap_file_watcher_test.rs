//! Test module for Bootstrap extension file watcher functionality.
//!
//! This module tests the file watcher API that allows bootstrap extensions to watch files for
//! changes on the main thread event loop. During config_new it:
//! 1. Creates a file watcher for a path passed via config.
//! 2. Creates a short timer (10ms) that writes to the watched file when it fires.
//!
//! When the file change is detected, on_file_changed verifies the watcher identity and signals
//! init complete. This guarantees the full watcher lifecycle is exercised before `initialize()`
//! returns.

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
  config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  // The config payload is the path to the file we should watch.
  let watch_path = std::str::from_utf8(config).expect("config must be valid UTF-8");
  envoy_log_info!("Creating file watcher for path: {}", watch_path);

  // Create a file watcher and add a watch for MovedTo and Modified events.
  let watcher = envoy_extension_config.new_file_watcher();
  let added = watcher.add_watch(watch_path, FILE_WATCHER_EVENT_MOVED_TO | FILE_WATCHER_EVENT_MODIFIED);
  assert!(added, "add_watch must succeed");

  let watcher_id = watcher.id();
  envoy_log_info!("File watcher created with id: {}", watcher_id);

  // Create a short timer that will modify the watched file to trigger the watcher.
  let trigger_timer = envoy_extension_config.new_timer();
  trigger_timer.enable(Duration::from_millis(10));

  // Do NOT call signal_init_complete here. Defer until on_file_changed fires.
  Some(Box::new(FileWatcherTestBootstrapExtensionConfig {
    watcher: Mutex::new(Some(watcher)),
    trigger_timer: Mutex::new(Some(trigger_timer)),
    watch_path: watch_path.to_string(),
    watcher_id,
    file_changed: AtomicBool::new(false),
  }))
}

struct FileWatcherTestBootstrapExtensionConfig {
  watcher: Mutex<Option<Box<dyn EnvoyBootstrapExtensionFileWatcher>>>,
  trigger_timer: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  watch_path: String,
  watcher_id: usize,
  file_changed: AtomicBool,
}

impl BootstrapExtensionConfig for FileWatcherTestBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(FileWatcherTestBootstrapExtension {})
  }

  fn on_timer_fired(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
    // The trigger timer fired. Write to the watched file to trigger the file watcher.
    envoy_log_info!("Trigger timer fired, writing to watched file: {}", self.watch_path);
    std::fs::write(&self.watch_path, "modified by timer").expect("Failed to write to watched file");

    // Drop the timer since we only need it once.
    let mut guard = self.trigger_timer.lock().unwrap();
    *guard = None;
  }

  fn on_file_changed(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    watcher: &dyn EnvoyBootstrapExtensionFileWatcher,
    path: &str,
    events: u32,
  ) {
    let fired_id = watcher.id();
    envoy_log_info!(
      "File changed callback: watcher_id={}, path={}, events=0x{:x}",
      fired_id,
      path,
      events
    );

    assert_eq!(
      fired_id, self.watcher_id,
      "Fired watcher id must match the created watcher id"
    );

    self.file_changed.store(true, Ordering::SeqCst);

    // Note: Do NOT drop the watcher here. We are inside the watcher's own inotify callback,
    // so destroying it would close the inotify fd while onInotifyEvent() is still reading from it.
    // The watcher will be cleaned up when this config struct is dropped.

    envoy_extension_config.signal_init_complete();
    envoy_log_info!("Bootstrap file watcher test completed successfully!");
  }
}

struct FileWatcherTestBootstrapExtension {}

impl BootstrapExtension for FileWatcherTestBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("File watcher test bootstrap extension server initialized");
  }
}
