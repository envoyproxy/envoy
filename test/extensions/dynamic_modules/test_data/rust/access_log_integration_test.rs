//! Integration test module for access logger dynamic modules.
//!
//! This module implements a simple access logger that records log events and flush calls.

use envoy_proxy_dynamic_modules_rust_sdk::access_log::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicU32, Ordering};

declare_init_functions!(init, new_nop_http_filter_config_fn);
declare_access_logger!(TestAccessLoggerConfig);

/// Global counter for log events.
static LOG_COUNT: AtomicU32 = AtomicU32::new(0);

/// Global counter for flush calls.
static FLUSH_COUNT: AtomicU32 = AtomicU32::new(0);

fn init() -> bool {
  true
}

/// Dummy HTTP filter config function (required by declare_init_functions).
fn new_nop_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  None
}

/// Access logger configuration.
struct TestAccessLoggerConfig {
  _name: String,
}

impl AccessLoggerConfig for TestAccessLoggerConfig {
  fn new(name: &str, _config: &[u8]) -> Result<Self, String> {
    Ok(Self {
      _name: name.to_string(),
    })
  }

  fn create_logger(&self) -> Box<dyn AccessLogger> {
    Box::new(TestAccessLogger { pending_logs: 0 })
  }
}

/// Access logger instance that tracks pending (unflushed) logs.
struct TestAccessLogger {
  pending_logs: u32,
}

impl AccessLogger for TestAccessLogger {
  fn log(&mut self, ctx: &LogContext) {
    // Increment the global log count.
    LOG_COUNT.fetch_add(1, Ordering::SeqCst);
    self.pending_logs += 1;

    // Access some log context data to verify callbacks work.
    let _response_code = ctx.response_code();
    let _protocol = ctx.protocol();
    let _route_name = ctx.route_name();
    let _is_health_check = ctx.is_health_check();
    let _timing = ctx.timing_info();
    let _bytes = ctx.bytes_info();
  }

  fn flush(&mut self) {
    // Increment flush count and reset pending logs.
    FLUSH_COUNT.fetch_add(1, Ordering::SeqCst);
    self.pending_logs = 0;
  }
}
