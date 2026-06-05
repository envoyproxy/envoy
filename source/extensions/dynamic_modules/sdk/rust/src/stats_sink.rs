//! Stats sink support for dynamic modules.
//!
//! A stats sink receives a snapshot of Envoy's metrics on every flush and an observation for
//! every completed histogram sample. Register a factory through the `stat_sink:` arm of
//! [`crate::declare_all_init_functions!`] (or [`crate::declare_stat_sink_init_functions!`]) and
//! return a [`StatSink`] from it.

use crate::{abi, EnvoyBuffer, NewStatSinkConfigFunction};
use std::ffi::c_void;
use std::marker::PhantomData;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

/// A counter entry in a [`MetricSnapshot`].
#[derive(Debug, Clone, Copy)]
pub struct CounterSnapshot<'a> {
  /// The counter name. Valid only for the duration of the [`StatSink::on_flush`] call.
  pub name: EnvoyBuffer<'a>,
  /// The current accumulated value.
  pub value: u64,
  /// The increase since the previous flush.
  pub delta: u64,
}

/// A gauge entry in a [`MetricSnapshot`].
#[derive(Debug, Clone, Copy)]
pub struct GaugeSnapshot<'a> {
  /// The gauge name. Valid only for the duration of the [`StatSink::on_flush`] call.
  pub name: EnvoyBuffer<'a>,
  /// The current value.
  pub value: u64,
}

/// A text readout entry in a [`MetricSnapshot`].
#[derive(Debug, Clone, Copy)]
pub struct TextReadoutSnapshot<'a> {
  /// The text readout name. Valid only for the duration of the [`StatSink::on_flush`] call.
  pub name: EnvoyBuffer<'a>,
  /// The text readout value. Valid only for the duration of the [`StatSink::on_flush`] call.
  pub value: EnvoyBuffer<'a>,
}

/// Read-only view over the metrics captured for a single flush.
///
/// The snapshot and every buffer it returns are owned by Envoy and remain valid only until the
/// [`StatSink::on_flush`] call returns. Copy any data that must outlive the call.
pub struct MetricSnapshot<'a> {
  envoy_ptr: *mut c_void,
  _marker: PhantomData<&'a ()>,
}

impl<'a> MetricSnapshot<'a> {
  /// Wrap the opaque snapshot handle passed by Envoy. Used internally by the SDK.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self {
      envoy_ptr,
      _marker: PhantomData,
    }
  }

  /// The number of counters in the snapshot.
  pub fn counter_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(self.envoy_ptr)
    }
  }

  /// The counter at `index`, or `None` if the index is out of range.
  pub fn counter(&self, index: usize) -> Option<CounterSnapshot<'a>> {
    let mut name = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    let mut value: u64 = 0;
    let mut delta: u64 = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
        self.envoy_ptr,
        index,
        &mut name,
        &mut value,
        &mut delta,
      )
    };
    found.then(|| CounterSnapshot {
      name: unsafe { EnvoyBuffer::new_from_raw(name.ptr as *const u8, name.length) },
      value,
      delta,
    })
  }

  /// An iterator over all counters in the snapshot.
  pub fn counters(&self) -> impl Iterator<Item = CounterSnapshot<'a>> + '_ {
    (0..self.counter_count()).filter_map(|index| self.counter(index))
  }

  /// The number of gauges in the snapshot.
  pub fn gauge_count(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(self.envoy_ptr) }
  }

  /// The gauge at `index`, or `None` if the index is out of range.
  pub fn gauge(&self, index: usize) -> Option<GaugeSnapshot<'a>> {
    let mut name = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    let mut value: u64 = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
        self.envoy_ptr,
        index,
        &mut name,
        &mut value,
      )
    };
    found.then(|| GaugeSnapshot {
      name: unsafe { EnvoyBuffer::new_from_raw(name.ptr as *const u8, name.length) },
      value,
    })
  }

  /// An iterator over all gauges in the snapshot.
  pub fn gauges(&self) -> impl Iterator<Item = GaugeSnapshot<'a>> + '_ {
    (0..self.gauge_count()).filter_map(|index| self.gauge(index))
  }

  /// The number of text readouts in the snapshot.
  pub fn text_readout_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(self.envoy_ptr)
    }
  }

  /// The text readout at `index`, or `None` if the index is out of range.
  pub fn text_readout(&self, index: usize) -> Option<TextReadoutSnapshot<'a>> {
    let mut name = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    let mut value = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
        self.envoy_ptr,
        index,
        &mut name,
        &mut value,
      )
    };
    found.then(|| TextReadoutSnapshot {
      name: unsafe { EnvoyBuffer::new_from_raw(name.ptr as *const u8, name.length) },
      value: unsafe { EnvoyBuffer::new_from_raw(value.ptr as *const u8, value.length) },
    })
  }

  /// An iterator over all text readouts in the snapshot.
  pub fn text_readouts(&self) -> impl Iterator<Item = TextReadoutSnapshot<'a>> + '_ {
    (0..self.text_readout_count()).filter_map(|index| self.text_readout(index))
  }
}

/// A stats sink implemented by a dynamic module.
///
/// A single instance is created per sink configuration and shared across threads. [`on_flush`]
/// runs on the main thread during the periodic stats flush, while [`on_histogram_complete`] runs
/// synchronously on the worker thread that recorded the sample, so implementations must be
/// thread-safe and must not block. Resource cleanup belongs in a [`Drop`] implementation.
///
/// [`on_flush`]: StatSink::on_flush
/// [`on_histogram_complete`]: StatSink::on_histogram_complete
pub trait StatSink: Send + Sync {
  /// Called with a snapshot of the metrics that passed the configured sink predicates.
  fn on_flush(&self, snapshot: &MetricSnapshot<'_>);

  /// Called for every completed histogram sample. The name is valid only for this call.
  fn on_histogram_complete(&self, name: EnvoyBuffer<'_>, value: u64);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_config_new(
  _config_envoy_ptr: *mut c_void,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> *const c_void {
  catch_unwind(AssertUnwindSafe(|| {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes. The
    // helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed UTF-8.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    let new_fn = crate::NEW_STAT_SINK_CONFIG_FUNCTION
      .get()
      .expect("NEW_STAT_SINK_CONFIG_FUNCTION must be set");
    envoy_dynamic_module_on_stat_sink_config_new_impl(name_str.as_ref(), config_bytes, new_fn)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_stat_sink_config_new", panic);
    ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_stat_sink_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_stat_sink_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &NewStatSinkConfigFunction,
) -> *const c_void {
  match new_fn(name, config) {
    Some(sink) => crate::wrap_into_c_void_ptr!(sink),
    None => ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_config_destroy(
  config_ptr: *const c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, StatSink);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_stat_sink_config_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_flush(
  config_ptr: *const c_void,
  snapshot_envoy_ptr: *mut c_void,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let sink = &*(config_ptr as *const Box<dyn StatSink>);
    let snapshot = MetricSnapshot::new(snapshot_envoy_ptr);
    sink.on_flush(&snapshot);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_stat_sink_flush", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_on_histogram_complete(
  config_ptr: *const c_void,
  histogram_name: abi::envoy_dynamic_module_type_envoy_buffer,
  value: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let sink = &*(config_ptr as *const Box<dyn StatSink>);
    let name =
      unsafe { EnvoyBuffer::new_from_raw(histogram_name.ptr as *const u8, histogram_name.length) };
    sink.on_histogram_complete(name, value);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_stat_sink_on_histogram_complete",
      panic,
    );
  });
}
