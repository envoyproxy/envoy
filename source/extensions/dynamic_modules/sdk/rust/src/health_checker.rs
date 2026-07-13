//! Health checker support for dynamic modules.
//!
//! This module provides traits and types for implementing custom active health checkers as dynamic
//! modules. The entry point is the `health_checker:` arm of
//! [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_HEALTH_CHECKER_CONFIG_FUNCTION`] and lets a single module dispatch by
//! `health_checker_name`.
//!
//! Envoy drives the standard per-host interval/timeout timers and applies the common `interval`,
//! `timeout`, `healthy_threshold` and `unhealthy_threshold` settings. On each interval Envoy calls
//! [`HealthCheckerSession::on_interval`]; the module performs the check (optionally on its own
//! thread) and reports the host's [`HostHealth`] through the [`Reporter`], which is safe to move to
//! and call from any thread.

use crate::{abi, EnvoyBuffer};
use std::panic::{catch_unwind, AssertUnwindSafe};

/// Health status of a host, reported by the module back to Envoy.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HostHealth {
  /// The host failed the health check.
  Unhealthy,
  /// The host passed the health check but is degraded.
  Degraded,
  /// The host passed the health check.
  Healthy,
}

impl HostHealth {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_host_health {
    match self {
      HostHealth::Unhealthy => abi::envoy_dynamic_module_type_host_health::Unhealthy,
      HostHealth::Degraded => abi::envoy_dynamic_module_type_host_health::Degraded,
      HostHealth::Healthy => abi::envoy_dynamic_module_type_host_health::Healthy,
    }
  }

  fn from_abi(value: abi::envoy_dynamic_module_type_host_health) -> Self {
    match value {
      abi::envoy_dynamic_module_type_host_health::Degraded => HostHealth::Degraded,
      abi::envoy_dynamic_module_type_host_health::Healthy => HostHealth::Healthy,
      _ => HostHealth::Unhealthy,
    }
  }
}

/// Trait that the dynamic module must implement to provide the health checker configuration.
///
/// The trait is dyn-safe: the
/// [`NewHealthCheckerConfigFunction`](crate::NewHealthCheckerConfigFunction) factory returns
/// `Box<dyn HealthCheckerConfig>`, which lets a single module host multiple health checker
/// implementations and dispatch between them by `health_checker_name`.
pub trait HealthCheckerConfig: Send + Sync {
  /// Create a new per-host session. Called on the main thread, once for each checked host.
  ///
  /// The `host` provides access to host info (address, metadata, current health). It is only valid
  /// during this call; the module must copy out any values it needs to retain.
  fn new_session(&self, host: &HealthCheckHost) -> Box<dyn HealthCheckerSession>;
}

/// Per-host health check session.
pub trait HealthCheckerSession: Send {
  /// Called on the main thread each time the interval timer fires. The module should start a health
  /// check (it may perform the work on its own thread) and report the result through `reporter`.
  ///
  /// The `host` is only valid during this call. The `reporter` may be moved to another thread and
  /// used to report the result once the check completes.
  fn on_interval(&mut self, host: &HealthCheckHost, reporter: Reporter);

  /// Called on the main thread when the check times out before a result was reported. Envoy records
  /// the timeout failure automatically; override this to cancel any in-flight work.
  ///
  /// This is optional. The default implementation does nothing.
  fn on_timeout(&mut self) {}
}

/// Thread-safe handle used to report a health check result from any thread. Created by the SDK and
/// passed to [`HealthCheckerSession::on_interval`]. The underlying scheduler is released when the
/// reporter is dropped. Reporting after the session has been torn down is safe and is a no-op.
pub struct Reporter {
  scheduler: abi::envoy_dynamic_module_type_health_checker_scheduler_module_ptr,
}

// SAFETY: the scheduler is designed by the ABI to be used from any thread; reporting posts the
// result to Envoy's main thread internally.
unsafe impl Send for Reporter {}
unsafe impl Sync for Reporter {}

impl Reporter {
  /// Create a reporter for the session identified by `session_envoy_ptr`. Must be called on the
  /// main thread (i.e. from within an event hook).
  fn new(
    session_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_session_envoy_ptr,
  ) -> Self {
    let scheduler =
      unsafe { abi::envoy_dynamic_module_callback_health_checker_scheduler_new(session_envoy_ptr) };
    Self { scheduler }
  }

  /// Report the host's health status. May be called from any thread.
  pub fn report(&self, health: HostHealth) {
    unsafe {
      abi::envoy_dynamic_module_callback_health_checker_scheduler_report(
        self.scheduler,
        health.to_abi(),
      );
    }
  }
}

impl Drop for Reporter {
  fn drop(&mut self) {
    if !self.scheduler.is_null() {
      unsafe {
        abi::envoy_dynamic_module_callback_health_checker_scheduler_delete(self.scheduler);
      }
    }
  }
}

/// Accessor for the host being checked by a session. Only valid during the event hook that provided
/// it; the module must not store it.
pub struct HealthCheckHost {
  session_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_session_envoy_ptr,
}

impl HealthCheckHost {
  fn new(
    session_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_session_envoy_ptr,
  ) -> Self {
    Self { session_envoy_ptr }
  }

  /// The resolved address (ip:port) of the host, or None if not available.
  pub fn address(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_health_checker_get_host_address(
        self.session_envoy_ptr,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Look up a string value in the host's endpoint metadata.
  pub fn metadata_string(&self, filter_name: &str, key: &str) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_health_checker_get_host_metadata_string(
        self.session_envoy_ptr,
        module_buffer(filter_name),
        module_buffer(key),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Look up a number value in the host's endpoint metadata.
  pub fn metadata_number(&self, filter_name: &str, key: &str) -> Option<f64> {
    let mut result: f64 = 0.0;
    if unsafe {
      abi::envoy_dynamic_module_callback_health_checker_get_host_metadata_number(
        self.session_envoy_ptr,
        module_buffer(filter_name),
        module_buffer(key),
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Look up a bool value in the host's endpoint metadata.
  pub fn metadata_bool(&self, filter_name: &str, key: &str) -> Option<bool> {
    let mut result: bool = false;
    if unsafe {
      abi::envoy_dynamic_module_callback_health_checker_get_host_metadata_bool(
        self.session_envoy_ptr,
        module_buffer(filter_name),
        module_buffer(key),
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// The host's current health status as known to Envoy.
  pub fn health(&self) -> HostHealth {
    HostHealth::from_abi(unsafe {
      abi::envoy_dynamic_module_callback_health_checker_get_host_health(self.session_envoy_ptr)
    })
  }
}

fn module_buffer(s: &str) -> abi::envoy_dynamic_module_type_module_buffer {
  abi::envoy_dynamic_module_type_module_buffer {
    ptr: s.as_ptr() as *const _,
    length: s.len(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_health_checker_config_module_ptr {
  let result = catch_unwind(AssertUnwindSafe(|| {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes. The
    // helpers tolerate `(nullptr, 0)` and substitute `U+FFFD` for malformed UTF-8.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    let new_fn = crate::NEW_HEALTH_CHECKER_CONFIG_FUNCTION
      .get()
      .expect("NEW_HEALTH_CHECKER_CONFIG_FUNCTION must be set");
    match new_fn(name_str.as_ref(), config_slice) {
      Some(config) => crate::wrap_into_c_void_ptr!(config),
      None => std::ptr::null(),
    }
  }));
  match result {
    Ok(ptr) => ptr,
    Err(panic) => {
      crate::log_ffi_panic("envoy_dynamic_module_on_health_checker_config_new", panic);
      std::ptr::null()
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_health_checker_config_module_ptr,
) {
  if let Err(panic) = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, HealthCheckerConfig);
  })) {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_health_checker_config_destroy",
      panic,
    );
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_session_new(
  config_ptr: abi::envoy_dynamic_module_type_health_checker_config_module_ptr,
  session_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_session_envoy_ptr,
) -> abi::envoy_dynamic_module_type_health_checker_session_module_ptr {
  let result = catch_unwind(AssertUnwindSafe(|| {
    let config = &*(config_ptr as *const Box<dyn HealthCheckerConfig>);
    let host = HealthCheckHost::new(session_envoy_ptr);
    let session: Box<dyn HealthCheckerSession> = config.new_session(&host);
    crate::wrap_into_c_void_ptr!(session)
  }));
  match result {
    Ok(ptr) => ptr,
    Err(panic) => {
      crate::log_ffi_panic("envoy_dynamic_module_on_health_checker_session_new", panic);
      std::ptr::null()
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_session_on_interval(
  session_ptr: abi::envoy_dynamic_module_type_health_checker_session_module_ptr,
  session_envoy_ptr: abi::envoy_dynamic_module_type_health_checker_session_envoy_ptr,
) {
  if let Err(panic) = catch_unwind(AssertUnwindSafe(|| {
    let session = &mut *(session_ptr as *mut Box<dyn HealthCheckerSession>);
    let host = HealthCheckHost::new(session_envoy_ptr);
    let reporter = Reporter::new(session_envoy_ptr);
    session.on_interval(&host, reporter);
  })) {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_health_checker_session_on_interval",
      panic,
    );
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_session_on_timeout(
  session_ptr: abi::envoy_dynamic_module_type_health_checker_session_module_ptr,
) {
  if let Err(panic) = catch_unwind(AssertUnwindSafe(|| {
    let session = &mut *(session_ptr as *mut Box<dyn HealthCheckerSession>);
    session.on_timeout();
  })) {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_health_checker_session_on_timeout",
      panic,
    );
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_health_checker_session_destroy(
  session_ptr: abi::envoy_dynamic_module_type_health_checker_session_module_ptr,
) {
  if let Err(panic) = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(session_ptr, HealthCheckerSession);
  })) {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_health_checker_session_destroy",
      panic,
    );
  }
}
