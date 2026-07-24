//! Integration test module for health checker dynamic modules.
//!
//! A single `.so` registers a health checker factory that dispatches by `health_checker_name`. The
//! configured mode ("healthy"/"degraded"/"unhealthy"/"timeout"/"double") controls what the session
//! reports. On each interval the session exercises every host accessor and then reports from a
//! separate thread, mirroring how a real module would do its check off the main thread.

use envoy_proxy_dynamic_modules_rust_sdk::health_checker::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_all_init_functions!(init, health_checker: new_health_checker_config_fn);

fn init() -> bool {
  true
}

#[derive(Clone, Copy)]
enum Mode {
  Healthy,
  Degraded,
  Unhealthy,
  Timeout,
  Double,
}

fn parse_mode(config: &[u8]) -> Mode {
  match std::str::from_utf8(config).unwrap_or("healthy").trim() {
    "degraded" => Mode::Degraded,
    "unhealthy" => Mode::Unhealthy,
    "timeout" => Mode::Timeout,
    "double" => Mode::Double,
    _ => Mode::Healthy,
  }
}

/// Health checker factory: dispatches by `health_checker_name`. Returning `None` for an unknown name
/// causes Envoy to reject the health-check configuration at config load time.
fn new_health_checker_config_fn(name: &str, config: &[u8]) -> Option<Box<dyn HealthCheckerConfig>> {
  match name {
    "test_health_checker" => Some(Box::new(TestHealthCheckerConfig {
      mode: parse_mode(config),
    })),
    _ => None,
  }
}

struct TestHealthCheckerConfig {
  mode: Mode,
}

impl HealthCheckerConfig for TestHealthCheckerConfig {
  fn new_session(&self, host: &HealthCheckHost) -> Box<dyn HealthCheckerSession> {
    // Exercise host accessors on the main thread during session creation.
    let _ = host.address();
    Box::new(TestHealthCheckerSession {
      mode: self.mode,
      thread: None,
    })
  }
}

struct TestHealthCheckerSession {
  mode: Mode,
  // The reporting thread is joined on drop so it never outlives the session (and thus the checker
  // and dispatcher it posts to).
  thread: Option<std::thread::JoinHandle<()>>,
}

impl Drop for TestHealthCheckerSession {
  fn drop(&mut self) {
    if let Some(handle) = self.thread.take() {
      let _ = handle.join();
    }
  }
}

impl HealthCheckerSession for TestHealthCheckerSession {
  fn on_interval(&mut self, host: &HealthCheckHost, reporter: Reporter) {
    // Exercise every host accessor on the main thread before going asynchronous: present keys,
    // a wrong-typed lookup, a missing key, and the current health.
    let _ = host.address();
    let _ = host.metadata_string("envoy.lb", "str_key");
    let _ = host.metadata_number("envoy.lb", "num_key");
    let _ = host.metadata_bool("envoy.lb", "bool_key");
    let _ = host.metadata_string("envoy.lb", "num_key"); // wrong type -> None
    let _ = host.metadata_number("envoy.lb", "str_key"); // wrong type -> None
    let _ = host.metadata_bool("envoy.lb", "str_key"); // wrong type -> None
    let _ = host.metadata_string("envoy.lb", "missing"); // missing key -> None
    let _ = host.metadata_string("missing.ns", "k"); // missing namespace -> None
    let _ = host.health();

    // Kick off the check once; the interval keeps firing but a single report is enough to drive the
    // test, and a single joinable thread keeps teardown deterministic.
    if self.thread.is_some() {
      return;
    }
    let mode = self.mode;
    match mode {
      // Never report; Envoy's timeout fires and records a failure.
      Mode::Timeout => {},
      // Report twice from another thread; the second result is ignored by Envoy.
      Mode::Double => {
        self.thread = Some(std::thread::spawn(move || {
          reporter.report(HostHealth::Healthy);
          reporter.report(HostHealth::Healthy);
        }));
      },
      _ => {
        let health = match mode {
          Mode::Degraded => HostHealth::Degraded,
          Mode::Unhealthy => HostHealth::Unhealthy,
          _ => HostHealth::Healthy,
        };
        self.thread = Some(std::thread::spawn(move || {
          reporter.report(health);
        }));
      },
    }
  }

  fn on_timeout(&mut self) {}
}
