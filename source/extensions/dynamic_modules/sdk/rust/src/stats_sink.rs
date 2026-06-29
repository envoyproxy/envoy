//! Stats sink support for dynamic modules.
//!
//! A stats sink receives a snapshot of Envoy's metrics on every flush and an observation for
//! every completed histogram sample. Register a factory through the `stat_sink:` arm of
//! [`crate::declare_all_init_functions!`] (or [`crate::declare_stat_sink_init_functions!`]) and
//! return a [`StatSink`] from it.

use crate::{abi, EnvoyBuffer, EnvoyGaugeId, NewStatSinkConfigFunction};
use mockall::*;
use std::ffi::{c_char, c_void};
use std::marker::PhantomData;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

/// The values of a counter, returned by [`MetricSnapshot::counter`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CounterValue {
  /// The current accumulated value.
  pub value: u64,
  /// The increase since the previous flush.
  pub delta: u64,
}

/// Read-only view over the metrics captured for a single flush.
///
/// The snapshot is owned by Envoy and is valid only until the [`StatSink::on_flush`] call
/// returns. Stat names and text-readout values are decoded directly into a caller-provided
/// `Vec<u8>`, which the SDK clears and grows as needed, so a single buffer can be reused across
/// every entry to avoid allocating a string per metric. The bytes are the same UTF-8 form
/// produced by Envoy's stats subsystem.
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

  /// Reads the counter at `index`, writing its name into `name` and returning its values. On
  /// success `name` holds exactly the name bytes. Returns `None` and leaves `name` unchanged when
  /// the index is out of range.
  pub fn counter(&self, index: usize, name: &mut Vec<u8>) -> Option<CounterValue> {
    let mut value: u64 = 0;
    let mut delta: u64 = 0;
    let found = fill_buffer(name, |ptr, capacity, size| unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
        self.envoy_ptr,
        index,
        ptr,
        capacity,
        size,
        &mut value,
        &mut delta,
      )
    });
    found.then_some(CounterValue { value, delta })
  }

  /// The number of gauges in the snapshot.
  pub fn gauge_count(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(self.envoy_ptr) }
  }

  /// Reads the gauge at `index`, writing its name into `name` and returning its value. On success
  /// `name` holds exactly the name bytes. Returns `None` and leaves `name` unchanged when the index
  /// is out of range.
  pub fn gauge(&self, index: usize, name: &mut Vec<u8>) -> Option<u64> {
    let mut value: u64 = 0;
    let found = fill_buffer(name, |ptr, capacity, size| unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
        self.envoy_ptr,
        index,
        ptr,
        capacity,
        size,
        &mut value,
      )
    });
    found.then_some(value)
  }

  /// The number of text readouts in the snapshot.
  pub fn text_readout_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(self.envoy_ptr)
    }
  }

  /// Reads the text readout at `index`, writing its name into `name` and its value into `value`.
  /// Returns `true` on success with both buffers holding exactly their bytes. Returns `false` and
  /// leaves both buffers unchanged when the index is out of range.
  pub fn text_readout(&self, index: usize, name: &mut Vec<u8>, value: &mut Vec<u8>) -> bool {
    // Both outputs come from a single call, and either may be truncated independently, so grow
    // whichever did not fit and retry until both fit. The snapshot is stable for the duration of
    // the flush, so this converges in at most two iterations. As with `fill_buffer`, a
    // zero-capacity `Vec` passes a dangling-but-non-null pointer that Envoy leaves untouched.
    loop {
      let name_capacity = name.capacity();
      let value_capacity = value.capacity();
      let mut name_size: usize = 0;
      let mut value_size: usize = 0;
      let found = unsafe {
        abi::envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
          self.envoy_ptr,
          index,
          name.as_mut_ptr() as *mut c_char,
          name_capacity,
          &mut name_size,
          value.as_mut_ptr() as *mut c_char,
          value_capacity,
          &mut value_size,
        )
      };
      if !found {
        return false;
      }
      if name_size <= name_capacity && value_size <= value_capacity {
        // SAFETY: Envoy's snprintf-style contract guarantees that when each `size <= capacity` it
        // wrote exactly that many bytes into the buffer, so no uninitialized bytes are exposed.
        unsafe {
          name.set_len(name_size);
          value.set_len(value_size);
        }
        return true;
      }
      if name_size > name_capacity {
        name.clear();
        name.reserve(name_size);
      }
      if value_size > value_capacity {
        value.clear();
        value.reserve(value_size);
      }
    }
  }

  /// Copies the whole snapshot into an [`OwnedMetricSnapshot`].
  ///
  /// The returned value owns its data and is `Send`, so it can be moved to another thread to
  /// aggregate metrics off the main thread. Names and text-readout values are decoded as UTF-8,
  /// substituting `U+FFFD` for any invalid bytes. Scratch buffers are reused across entries, so the
  /// per-entry allocation is just the owned strings it returns.
  #[must_use]
  pub fn to_owned(&self) -> OwnedMetricSnapshot {
    let mut name = Vec::new();
    let mut value = Vec::new();
    let counters = (0..self.counter_count())
      .filter_map(|index| {
        self.counter(index, &mut name).map(|counter| OwnedCounter {
          name: String::from_utf8_lossy(&name).into_owned(),
          value: counter.value,
          delta: counter.delta,
        })
      })
      .collect();
    let gauges = (0..self.gauge_count())
      .filter_map(|index| {
        self.gauge(index, &mut name).map(|gauge_value| OwnedGauge {
          name: String::from_utf8_lossy(&name).into_owned(),
          value: gauge_value,
        })
      })
      .collect();
    let text_readouts = (0..self.text_readout_count())
      .filter_map(|index| {
        if self.text_readout(index, &mut name, &mut value) {
          Some(OwnedTextReadout {
            name: String::from_utf8_lossy(&name).into_owned(),
            value: String::from_utf8_lossy(&value).into_owned(),
          })
        } else {
          None
        }
      })
      .collect();
    OwnedMetricSnapshot {
      counters,
      gauges,
      text_readouts,
    }
  }
}

/// An owned counter entry copied from a counter in a [`MetricSnapshot`].
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OwnedCounter {
  /// The counter name.
  pub name: String,
  /// The current accumulated value.
  pub value: u64,
  /// The increase since the previous flush.
  pub delta: u64,
}

/// An owned gauge entry copied from a gauge in a [`MetricSnapshot`].
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OwnedGauge {
  /// The gauge name.
  pub name: String,
  /// The current value.
  pub value: u64,
}

/// An owned text readout entry copied from a text readout in a [`MetricSnapshot`].
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OwnedTextReadout {
  /// The text readout name.
  pub name: String,
  /// The text readout value.
  pub value: String,
}

/// An owned, `Send` copy of a [`MetricSnapshot`] produced by [`MetricSnapshot::to_owned`].
///
/// Unlike [`MetricSnapshot`], which borrows from Envoy and is valid only during
/// [`StatSink::on_flush`], this owns all of its data and can outlive the call. Move it to another
/// thread to aggregate metrics off the main thread, then publish the results back via an
/// [`EnvoyStatSinkConfigScheduler`].
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OwnedMetricSnapshot {
  /// The counters captured for the flush.
  pub counters: Vec<OwnedCounter>,
  /// The gauges captured for the flush.
  pub gauges: Vec<OwnedGauge>,
  /// The text readouts captured for the flush.
  pub text_readouts: Vec<OwnedTextReadout>,
}

/// Invokes `fill` to decode a single stat name into `buffer`, growing and retrying if the name did
/// not fit. `fill` receives the buffer pointer, its capacity, and an out-parameter for the full
/// name length. On success `buffer` holds exactly the bytes written and this returns `true`. If the
/// entry does not exist `buffer` is left unchanged and this returns `false`. The reported size is
/// stable during a flush, so this converges in at most two iterations. A zero-capacity `Vec` yields
/// a dangling-but-non-null pointer that Envoy never dereferences because the capacity is 0.
fn fill_buffer<F>(buffer: &mut Vec<u8>, mut fill: F) -> bool
where
  F: FnMut(*mut c_char, usize, &mut usize) -> bool,
{
  loop {
    let capacity = buffer.capacity();
    let mut size: usize = 0;
    if !fill(buffer.as_mut_ptr() as *mut c_char, capacity, &mut size) {
      return false;
    }
    if size <= capacity {
      // SAFETY: Envoy's snprintf-style contract guarantees that when `size <= capacity` it wrote
      // exactly `size` bytes into the buffer's allocation, so no uninitialized bytes are exposed.
      unsafe {
        buffer.set_len(size);
      }
      return true;
    }
    // The name was truncated, so grow to the reported size and retry.
    buffer.clear();
    buffer.reserve(size);
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

  /// Called on the main thread when an event committed via
  /// [`EnvoyStatSinkConfigScheduler::commit`] is dispatched.
  ///
  /// This is where a sink that aggregates metrics off the main thread publishes the results, using
  /// `envoy_config` to set gauges defined during configuration. `event_id` is the value passed to
  /// [`EnvoyStatSinkConfigScheduler::commit`]. The default implementation does nothing.
  fn on_config_scheduled(&self, _envoy_config: &mut EnvoyStatSinkConfig, _event_id: u64) {}
}

/// The Envoy-side stats sink configuration handle backed by an opaque Envoy pointer.
///
/// It is passed to the factory registered via [`crate::declare_stat_sink_init_functions!`] and to
/// [`StatSink::on_config_scheduled`], both of which run on the main thread. Gauges must be defined
/// while the configuration is being created and can be set later from the main thread.
pub struct EnvoyStatSinkConfig {
  raw_ptr: abi::envoy_dynamic_module_type_stat_sink_config_envoy_ptr,
}

impl EnvoyStatSinkConfig {
  /// Wraps the opaque configuration handle passed by Envoy. Used internally by the SDK.
  #[doc(hidden)]
  pub fn new(raw_ptr: abi::envoy_dynamic_module_type_stat_sink_config_envoy_ptr) -> Self {
    Self { raw_ptr }
  }

  /// Defines a gauge with the given name and returns its identifier.
  ///
  /// This must be called from the factory while the configuration is being created. Defining a
  /// gauge afterwards returns [`abi::envoy_dynamic_module_type_metrics_result::Frozen`].
  pub fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_config_define_gauge(
        self.raw_ptr,
        crate::str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  /// Sets a previously defined gauge to `value`.
  ///
  /// This must be called on the main thread, typically from [`StatSink::on_config_scheduled`].
  pub fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_config_set_gauge(self.raw_ptr, id, value)
    })
  }

  /// Creates a scheduler that posts events to this configuration on the main thread.
  #[must_use]
  pub fn new_config_scheduler(&self) -> impl EnvoyStatSinkConfigScheduler + 'static {
    let scheduler_ptr =
      unsafe { abi::envoy_dynamic_module_callback_stat_sink_config_scheduler_new(self.raw_ptr) };
    EnvoyStatSinkConfigSchedulerImpl {
      raw_ptr: scheduler_ptr,
    }
  }
}

/// This represents a thread-safe object that can be used to post an event to a stats sink
/// configuration on the main thread.
///
/// The module can hold it on any thread, for example a Tokio worker, and call
/// [`EnvoyStatSinkConfigScheduler::commit`] to run [`StatSink::on_config_scheduled`] on the main
/// thread. It is created by [`EnvoyStatSinkConfig::new_config_scheduler`] and releases the
/// Envoy-side resource when dropped.
#[automock]
pub trait EnvoyStatSinkConfigScheduler: Send + Sync {
  /// Schedules [`StatSink::on_config_scheduled`] to run on the main thread with the given `event_id`.
  ///
  /// This can be called multiple times to schedule multiple events. The configuration is guaranteed
  /// to be alive when the event runs.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyStatSinkConfigScheduler`] trait with the given raw pointer to the
/// Envoy stats sink configuration scheduler object.
struct EnvoyStatSinkConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr,
}

// SAFETY: the scheduler wraps an opaque handle to the Envoy-side scheduler. Its commit() and
// delete() are safe to call from any thread, and concurrent commits are data-race-free, by ABI
// contract.
unsafe impl Send for EnvoyStatSinkConfigSchedulerImpl {}
unsafe impl Sync for EnvoyStatSinkConfigSchedulerImpl {}

impl Drop for EnvoyStatSinkConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyStatSinkConfigScheduler for EnvoyStatSinkConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_stat_sink_config_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

// Implement the trait for the boxed form too so a boxed scheduler can be used where the trait is
// expected, matching the other dynamic module schedulers.
impl EnvoyStatSinkConfigScheduler for Box<dyn EnvoyStatSinkConfigScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_config_new(
  config_envoy_ptr: abi::envoy_dynamic_module_type_stat_sink_config_envoy_ptr,
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
    let mut envoy_config = EnvoyStatSinkConfig::new(config_envoy_ptr);
    envoy_dynamic_module_on_stat_sink_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      &mut envoy_config,
      new_fn,
    )
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
  envoy_config: &mut EnvoyStatSinkConfig,
  new_fn: &NewStatSinkConfigFunction,
) -> *const c_void {
  match new_fn(name, config, envoy_config) {
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

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_stat_sink_config_scheduled(
  config_envoy_ptr: abi::envoy_dynamic_module_type_stat_sink_config_envoy_ptr,
  config_ptr: *const c_void,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let sink = &*(config_ptr as *const Box<dyn StatSink>);
    let mut envoy_config = EnvoyStatSinkConfig::new(config_envoy_ptr);
    sink.on_config_scheduled(&mut envoy_config, event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_stat_sink_config_scheduled", panic);
  });
}
