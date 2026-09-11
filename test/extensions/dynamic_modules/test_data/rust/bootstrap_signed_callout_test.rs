//! Test module reproducing the shape of a bootstrap extension that gates server initialization on
//! an AWS-signed HTTP callout.
//!
//! The module holds its init target open from `new_bootstrap_extension_config` and only signals
//! completion once an HTTP callout through `cluster_0` has returned a 2xx. The callout is issued
//! with a request body so that an upstream `aws_request_signing` filter on that cluster takes its
//! `decodeData` signing path, which is where a request blocks while AWS credentials are still
//! pending.
//!
//! Combined with a credentials chain that has no synchronous provider, this closes a loop that
//! used to deadlock: signing waited on an async credential refresh, that refresh could not
//! resolve until worker threads were running, and worker threads could not start until this init
//! target completed.
//!
//! Failures are retried rather than given up on immediately, so that a slow environment does not
//! turn into a flake, but the retries are budgeted so the module cannot loop forever. Once the
//! budget is exhausted it signals init complete without logging its success message, which fails
//! the integration test's log assertion. (A regression is bounded either way, because the test
//! harness gives up waiting for listeners after 20 seconds.)

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::Duration;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

/// Cluster the callout is sent to. This is the default static cluster of the integration test,
/// which carries the upstream `aws_request_signing` filter.
const CALLOUT_CLUSTER: &str = "cluster_0";

/// Callout deadline. Generous relative to the credential refresh it waits behind, so that a
/// timeout here means the request never got signed rather than that it was merely slow.
const CALLOUT_TIMEOUT_MS: u64 = 5000;

/// Delay before the first callout attempt and between retries.
const CALLOUT_DELAY: Duration = Duration::from_millis(100);

/// How many callouts may complete unsuccessfully before the module stops retrying. With the bug
/// this reproduces every attempt burns the full `CALLOUT_TIMEOUT_MS`, so this keeps the total under
/// half a minute.
const MAX_CALLOUT_FAILURES: u32 = 4;

/// How many times the callout may fail to start, which happens while `cluster_0` is still warming
/// up. These retries cost only `CALLOUT_DELAY` each, so the budget is larger.
const MAX_START_FAILURES: u32 = 50;

/// Scheduler event id used to hop from `on_server_initialized` back onto the config.
const EVENT_SERVER_INITIALIZED: u64 = 1;

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  // Deliberately do NOT signal init complete here. The init target stays open until the callout
  // below succeeds, which is the whole point of this module.
  let timer = envoy_extension_config.new_timer();
  let scheduler = Arc::new(envoy_extension_config.new_scheduler());
  envoy_log_info!("signed callout module: config created, init target held open");
  Some(Box::new(SignedCalloutConfig {
    timer,
    scheduler,
    init_signaled: AtomicBool::new(false),
    callout_failures: AtomicU32::new(0),
    start_failures: AtomicU32::new(0),
  }))
}

struct SignedCalloutConfig {
  timer: Box<dyn EnvoyBootstrapExtensionTimer>,
  scheduler: Arc<Box<dyn EnvoyBootstrapExtensionConfigScheduler>>,
  init_signaled: AtomicBool,
  callout_failures: AtomicU32,
  start_failures: AtomicU32,
}

impl SignedCalloutConfig {
  /// Signals init complete at most once, so a retry that races a success cannot double-signal.
  /// Returns whether this call was the one that signalled.
  fn signal_init_complete_once(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  ) -> bool {
    let signalled = self
      .init_signaled
      .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
      .is_ok();
    if signalled {
      envoy_extension_config.signal_init_complete();
    }
    signalled
  }

  /// Unblocks server initialization without logging the success message. The integration test
  /// asserts on that message, so it fails on the assertion rather than waiting out the test
  /// timeout.
  fn give_up(&self, envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig, reason: &str) {
    envoy_log_info!("signed callout module: giving up, {}", reason);
    self.signal_init_complete_once(envoy_extension_config);
  }
}

impl BootstrapExtensionConfig for SignedCalloutConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(SignedCalloutExtension {
      scheduler: self.scheduler.clone(),
    })
  }

  fn on_scheduled(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    event_id: u64,
  ) {
    if event_id == EVENT_SERVER_INITIALIZED {
      self.timer.enable(CALLOUT_DELAY);
    }
  }

  fn on_timer_fired(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
    // A body is sent so that the upstream aws_request_signing filter signs from decodeData
    // (use_unsigned_payload defaults to false).
    let (result, callout_id) = envoy_extension_config.send_http_callout(
      CALLOUT_CLUSTER,
      vec![
        (":method", b"POST"),
        (":path", b"/oauth2/token"),
        (":authority", b"authorizer"),
      ],
      Some(b"grant_type=client_credentials"),
      CALLOUT_TIMEOUT_MS,
    );
    if result != abi::envoy_dynamic_module_type_http_callout_init_result::Success {
      // The cluster may not be warm yet on the first attempt; keep retrying within budget.
      if self.start_failures.fetch_add(1, Ordering::SeqCst) + 1 > MAX_START_FAILURES {
        self.give_up(envoy_extension_config, "callout never started");
        return;
      }
      envoy_log_info!("signed callout module: callout could not be started, retrying");
      self.timer.enable(CALLOUT_DELAY);
      return;
    }
    envoy_log_info!("signed callout module: callout {} in flight", callout_id);
  }

  fn on_http_callout_done(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _callout_id: u64,
    result: abi::envoy_dynamic_module_type_http_callout_result,
    response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
    let status = if result == abi::envoy_dynamic_module_type_http_callout_result::Success {
      response_headers
        .unwrap_or_default()
        .iter()
        .find(|(name, _)| name.as_slice() == b":status")
        .and_then(|(_, value)| std::str::from_utf8(value.as_slice()).ok())
        .and_then(|value| value.parse::<u32>().ok())
        .unwrap_or(0)
    } else {
      0
    };

    if (200..300).contains(&status) {
      if self.signal_init_complete_once(envoy_extension_config) {
        envoy_log_info!("Bootstrap signed callout test completed successfully!");
      }
      return;
    }

    // Anything else -- a reset, or the 504 the router emits when signing never completes and the
    // callout deadline expires -- is retried within budget, so a slow environment does not flake.
    if self.callout_failures.fetch_add(1, Ordering::SeqCst) + 1 > MAX_CALLOUT_FAILURES {
      self.give_up(
        envoy_extension_config,
        "callout never returned a successful status",
      );
      return;
    }
    envoy_log_info!(
      "signed callout module: callout failed with status {}, retrying",
      status
    );
    self.timer.enable(CALLOUT_DELAY);
  }
}

struct SignedCalloutExtension {
  scheduler: Arc<Box<dyn EnvoyBootstrapExtensionConfigScheduler>>,
}

impl BootstrapExtension for SignedCalloutExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // The cluster manager is only reachable from the config once the server is initialized, so
    // the callout cannot be issued any earlier than this. Hop back onto the config to arm the
    // timer that sends it.
    envoy_log_info!("signed callout module: server initialized, scheduling callout");
    self.scheduler.commit(EVENT_SERVER_INITIALIZED);
  }
}
