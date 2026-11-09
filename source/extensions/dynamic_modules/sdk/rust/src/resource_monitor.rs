//! Resource monitor support for dynamic modules.
//!
//! A resource monitor reports to the overload manager the pressure of a resource, which is the
//! ratio of its usage to its limit. Register a factory through the `resource_monitor:` arm of
//! [`crate::declare_all_init_functions!`] and return a [`ResourceMonitor`] from it.

use crate::{abi, ffi_export, NewResourceMonitorConfigFunction};
#[cfg(any(test, feature = "mock"))]
use mockall::*;
use std::ffi::c_void;
use std::ptr;

/// The outcome of [`ResourceMonitor::update_resource_usage`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ResourceMonitorUpdate {
  /// The pressure of the resource, which is the ratio of its usage to its limit. Envoy treats a
  /// value above 1 as 1, and a negative or non-finite value as a failed update.
  Pressure(f64),
  /// The update failed. The overload manager counts it in the `failed_updates` statistic of the
  /// resource and keeps acting on the last reported pressure.
  Failure,
  /// The module reports the outcome later with
  /// [`EnvoyResourceMonitorConfigScheduler::report_resource_usage`] or
  /// [`EnvoyResourceMonitorConfigScheduler::report_failure`]. Envoy does not time out a pending
  /// update.
  Pending,
}

/// A resource monitor implemented by a dynamic module.
///
/// A single instance is created per resource monitor configuration. Its methods run on the main
/// thread, so they must not block. Resource cleanup belongs in a [`Drop`] implementation, which
/// must stop the threads that the monitor started, since the module may be unloaded once it
/// returns. In validation mode, its [`Drop`] must not call [`crate::get_server_concurrency`] or
/// [`crate::is_validation_mode`].
pub trait ResourceMonitor: Send + Sync {
  /// Called at each refresh interval of the overload manager to get the pressure of the resource.
  ///
  /// While an update is pending, the overload manager skips the following refreshes of the
  /// resource and counts them in the `skipped_updates` statistic of the resource.
  fn update_resource_usage(&self) -> ResourceMonitorUpdate;
}

/// The Envoy-side resource monitor configuration handle backed by an opaque Envoy pointer.
///
/// It is passed to the factory registered via [`crate::declare_all_init_functions!`], which runs on
/// the main thread.
pub struct EnvoyResourceMonitorConfig {
  raw_ptr: abi::envoy_dynamic_module_type_resource_monitor_config_envoy_ptr,
}

impl EnvoyResourceMonitorConfig {
  /// Wraps the opaque configuration handle passed by Envoy. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `raw_ptr` must be the configuration pointer that Envoy passed to the running event hook, and
  /// the returned value must not outlive that hook.
  #[doc(hidden)]
  pub unsafe fn new(
    raw_ptr: abi::envoy_dynamic_module_type_resource_monitor_config_envoy_ptr,
  ) -> Self {
    Self { raw_ptr }
  }

  /// Creates a scheduler that reports the outcome of a pending update from any thread.
  #[must_use]
  pub fn new_config_scheduler(&self) -> impl EnvoyResourceMonitorConfigScheduler + 'static {
    let scheduler_ptr = unsafe {
      abi::envoy_dynamic_module_callback_resource_monitor_config_scheduler_new(self.raw_ptr)
    };
    EnvoyResourceMonitorConfigSchedulerImpl {
      raw_ptr: scheduler_ptr,
    }
  }
}

/// This represents a thread-safe object that reports the outcome of a pending update from any
/// thread.
///
/// Envoy applies each report on the main thread to the update that is pending then, and drops it
/// if no update is pending or the resource monitor is destroyed or the server shuts down first. It
/// is created by [`EnvoyResourceMonitorConfig::new_config_scheduler`] and releases the Envoy-side
/// resource when dropped.
#[cfg_attr(any(test, feature = "mock"), automock)]
pub trait EnvoyResourceMonitorConfigScheduler: Send + Sync {
  /// Reports the pressure of the resource for the pending update. Envoy treats a value above 1 as
  /// 1, and a negative or non-finite value as a failed update.
  fn report_resource_usage(&self, resource_pressure: f64);

  /// Reports that the pending update failed.
  fn report_failure(&self);
}

/// This implements the [`EnvoyResourceMonitorConfigScheduler`] trait with the given raw pointer to
/// the Envoy resource monitor configuration scheduler object.
struct EnvoyResourceMonitorConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr,
}

// SAFETY: the scheduler wraps an opaque handle to the Envoy-side scheduler. Its reports and its
// delete() are safe to call from any thread, and concurrent reports are data-race-free, by ABI
// contract.
unsafe impl Send for EnvoyResourceMonitorConfigSchedulerImpl {}
unsafe impl Sync for EnvoyResourceMonitorConfigSchedulerImpl {}

impl Drop for EnvoyResourceMonitorConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_resource_monitor_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyResourceMonitorConfigScheduler for EnvoyResourceMonitorConfigSchedulerImpl {
  fn report_resource_usage(&self, resource_pressure: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(
        self.raw_ptr,
        resource_pressure,
      );
    }
  }

  fn report_failure(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(
        self.raw_ptr,
      );
    }
  }
}

// Implement the trait for the boxed form too so a boxed scheduler can be used where the trait is
// expected.
impl EnvoyResourceMonitorConfigScheduler for Box<dyn EnvoyResourceMonitorConfigScheduler> {
  fn report_resource_usage(&self, resource_pressure: f64) {
    (**self).report_resource_usage(resource_pressure);
  }

  fn report_failure(&self) {
    (**self).report_failure();
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_resource_monitor_config_new(
    config_envoy_ptr: abi::envoy_dynamic_module_type_resource_monitor_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> *const c_void {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes. The
    // helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed UTF-8.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    let new_fn = crate::NEW_RESOURCE_MONITOR_CONFIG_FUNCTION
      .get()
      .expect("NEW_RESOURCE_MONITOR_CONFIG_FUNCTION must be set");
    // SAFETY: `config_envoy_ptr` is the pointer Envoy passed to this hook, and the handle is only
    // lent to the factory for the duration of the hook.
    let mut envoy_config = unsafe { EnvoyResourceMonitorConfig::new(config_envoy_ptr) };
    envoy_dynamic_module_on_resource_monitor_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      &mut envoy_config,
      new_fn,
    )
  }
  on_panic = ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_resource_monitor_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_resource_monitor_config_new_impl(
  name: &str,
  config: &[u8],
  envoy_config: &mut EnvoyResourceMonitorConfig,
  new_fn: &NewResourceMonitorConfigFunction,
) -> *const c_void {
  match new_fn(name, config, envoy_config) {
    Some(monitor) => crate::wrap_into_c_void_ptr!(monitor),
    None => ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_resource_monitor_config_destroy(
    config_ptr: *const c_void,
  ) {
    crate::drop_wrapped_c_void_ptr!(config_ptr, ResourceMonitor);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_resource_monitor_update_resource_usage(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_resource_monitor_config_envoy_ptr,
    config_ptr: *const c_void,
    resource_pressure: *mut f64,
  ) -> abi::envoy_dynamic_module_type_resource_monitor_update_status {
    let monitor = &*(config_ptr as *const Box<dyn ResourceMonitor>);
    match monitor.update_resource_usage() {
      ResourceMonitorUpdate::Pressure(pressure) => {
        *resource_pressure = pressure;
        abi::envoy_dynamic_module_type_resource_monitor_update_status::Success
      },
      ResourceMonitorUpdate::Failure => {
        abi::envoy_dynamic_module_type_resource_monitor_update_status::Failure
      },
      ResourceMonitorUpdate::Pending => {
        abi::envoy_dynamic_module_type_resource_monitor_update_status::Pending
      },
    }
  }
  on_panic = abi::envoy_dynamic_module_type_resource_monitor_update_status::Failure
}
