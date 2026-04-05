//! Test module for Bootstrap extension file watcher functionality.
//!
//! This module tests the file watcher API including:
//! - Watching multiple files via separate add_file_watch calls.
//! - Repeated changes to the same file triggering multiple callbacks.
//!
//! Config format: two file paths separated by `|`.
//!
//! During config_new it:
//! 1. Parses two file paths from config.
//! 2. Calls add_file_watch for each path.
//! 3. Creates three timers:
//!    - Timer A (10ms): writes to file_a  → first change
//!    - Timer B (100ms): writes to file_b → verifies second file
//!    - Timer C (200ms): writes to file_a again → verifies repeated changes
//!
//! on_file_changed tracks which paths fired and how many times. Init completes after
//! file_a has been seen at least 2 times AND file_b at least 1 time.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
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
  let config_str = std::str::from_utf8(config).expect("config must be valid UTF-8");
  let paths: Vec<&str> = config_str.split('|').collect();
  assert_eq!(paths.len(), 2, "Expected two paths separated by |");

  let path_a = paths[0].to_string();
  let path_b = paths[1].to_string();
  envoy_log_info!("Watching file_a={} and file_b={}", path_a, path_b);

  // Add file watches for both paths.
  let added_a = envoy_extension_config.add_file_watch(
    &path_a,
    FILE_WATCHER_EVENT_MOVED_TO | FILE_WATCHER_EVENT_MODIFIED,
  );
  assert!(added_a, "add_file_watch for file_a must succeed");
  let added_b = envoy_extension_config.add_file_watch(
    &path_b,
    FILE_WATCHER_EVENT_MOVED_TO | FILE_WATCHER_EVENT_MODIFIED,
  );
  assert!(added_b, "add_file_watch for file_b must succeed");

  envoy_log_info!("File watches added for 2 files");

  // Timer A (10ms): write to file_a.
  let timer_a = envoy_extension_config.new_timer();
  timer_a.enable(Duration::from_millis(10));

  // Timer B (100ms): write to file_b.
  let timer_b = envoy_extension_config.new_timer();
  timer_b.enable(Duration::from_millis(100));

  // Timer C (200ms): write to file_a again (second change).
  let timer_c = envoy_extension_config.new_timer();
  timer_c.enable(Duration::from_millis(200));

  // Do NOT call signal_init_complete here. Defer until all expected file changes are received.
  Some(Box::new(FileWatcherTestConfig {
    timer_a: Mutex::new(Some(timer_a)),
    timer_b: Mutex::new(Some(timer_b)),
    timer_c: Mutex::new(Some(timer_c)),
    path_a,
    path_b,
    file_a_count: AtomicU32::new(0),
    file_b_count: AtomicU32::new(0),
    init_signaled: AtomicBool::new(false),
  }))
}

struct FileWatcherTestConfig {
  timer_a: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  timer_b: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  timer_c: Mutex<Option<Box<dyn EnvoyBootstrapExtensionTimer>>>,
  path_a: String,
  path_b: String,
  file_a_count: AtomicU32,
  file_b_count: AtomicU32,
  init_signaled: AtomicBool,
}

impl BootstrapExtensionConfig for FileWatcherTestConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(FileWatcherTestExtension {})
  }

  fn on_timer_fired(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
    let timer_a_id = self.timer_a.lock().unwrap().as_ref().map(|t| t.id());
    let timer_b_id = self.timer_b.lock().unwrap().as_ref().map(|t| t.id());
    let timer_c_id = self.timer_c.lock().unwrap().as_ref().map(|t| t.id());
    let fired_id = timer.id();

    if Some(fired_id) == timer_a_id {
      envoy_log_info!("Timer A fired: writing to file_a={}", self.path_a);
      std::fs::write(&self.path_a, "change 1 by timer_a").expect("Failed to write to file_a");
      *self.timer_a.lock().unwrap() = None;
    } else if Some(fired_id) == timer_b_id {
      envoy_log_info!("Timer B fired: writing to file_b={}", self.path_b);
      std::fs::write(&self.path_b, "change 1 by timer_b").expect("Failed to write to file_b");
      *self.timer_b.lock().unwrap() = None;
    } else if Some(fired_id) == timer_c_id {
      envoy_log_info!("Timer C fired: writing to file_a again={}", self.path_a);
      std::fs::write(&self.path_a, "change 2 by timer_c").expect("Failed to write to file_a again");
      *self.timer_c.lock().unwrap() = None;
    }
  }

  fn on_file_changed(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    path: &str,
    events: u32,
  ) {
    if path == self.path_a {
      let count = self.file_a_count.fetch_add(1, Ordering::SeqCst) + 1;
      envoy_log_info!(
        "file_a changed: path={}, events=0x{:x}, count={}",
        path,
        events,
        count
      );
    } else if path == self.path_b {
      let count = self.file_b_count.fetch_add(1, Ordering::SeqCst) + 1;
      envoy_log_info!(
        "file_b changed: path={}, events=0x{:x}, count={}",
        path,
        events,
        count
      );
    } else {
      envoy_log_info!("Unknown path changed: path={}, events=0x{:x}", path, events);
    }

    let a_count = self.file_a_count.load(Ordering::SeqCst);
    let b_count = self.file_b_count.load(Ordering::SeqCst);

    // Signal init complete once file_a has changed at least 2 times and file_b at least 1 time.
    if a_count >= 2 && b_count >= 1 && !self.init_signaled.load(Ordering::SeqCst) {
      self.init_signaled.store(true, Ordering::SeqCst);
      envoy_log_info!(
        "All expected file changes received: file_a={}, file_b={}",
        a_count,
        b_count
      );
      envoy_extension_config.signal_init_complete();
      envoy_log_info!("Bootstrap file watcher test completed successfully!");
    }
  }
}

struct FileWatcherTestExtension {}

impl BootstrapExtension for FileWatcherTestExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("File watcher test bootstrap extension server initialized");
  }
}
