use crate::{
  abi, bytes_to_module_buffer, str_to_module_buffer, wrap_into_c_void_ptr,
  NEW_TRACER_CONFIG_FUNCTION,
};
use std::ffi::c_void;

// -----------------------------------------------------------------------------
// Metrics Support
// -----------------------------------------------------------------------------

/// Handle for a counter metric without labels defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerCounterHandle {
  id: usize,
}

/// Handle for a labeled counter metric defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerCounterVecHandle {
  id: usize,
}

/// Handle for a gauge metric without labels defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerGaugeHandle {
  id: usize,
}

/// Handle for a labeled gauge metric defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerGaugeVecHandle {
  id: usize,
}

/// Handle for a histogram metric without labels defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerHistogramHandle {
  id: usize,
}

/// Handle for a labeled histogram metric defined by the tracer module.
#[derive(Debug, Clone, Copy)]
pub struct TracerHistogramVecHandle {
  id: usize,
}

/// Provides access to metrics operations for the tracer module.
///
/// This is passed to the tracer config factory function to allow defining metrics
/// during configuration. The context should be stored in the tracer config and used
/// to update metric values during span operations.
pub struct TracerConfigContext {
  envoy_ptr: *mut c_void,
}

// SAFETY: The envoy_ptr points to Envoy's DynamicModuleTracerConfig which is thread-safe.
// The metrics callbacks are designed to be called from any thread.
unsafe impl Send for TracerConfigContext {}
unsafe impl Sync for TracerConfigContext {}

impl TracerConfigContext {
  /// Define a counter metric.
  ///
  /// Returns a handle that can be used to increment the counter later.
  pub fn define_counter(
    &self,
    name: &str,
  ) -> Result<TracerCounterHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_counter(
        self.envoy_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(TracerCounterHandle { id })
  }

  /// Define a labeled counter metric.
  ///
  /// Returns a handle that can be used with `increment_counter_vec` together with label values.
  pub fn define_counter_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<TracerCounterVecHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_counter(
        self.envoy_ptr,
        str_to_module_buffer(name),
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        &mut id,
      )
    })?;
    Ok(TracerCounterVecHandle { id })
  }

  /// Define a gauge metric.
  ///
  /// Returns a handle that can be used to set the gauge later.
  pub fn define_gauge(
    &self,
    name: &str,
  ) -> Result<TracerGaugeHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_gauge(
        self.envoy_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(TracerGaugeHandle { id })
  }

  /// Define a labeled gauge metric.
  ///
  /// Returns a handle that can be used with `set_gauge_vec` together with label values.
  pub fn define_gauge_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<TracerGaugeVecHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_gauge(
        self.envoy_ptr,
        str_to_module_buffer(name),
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        &mut id,
      )
    })?;
    Ok(TracerGaugeVecHandle { id })
  }

  /// Define a histogram metric.
  ///
  /// Returns a handle that can be used to record histogram values later.
  pub fn define_histogram(
    &self,
    name: &str,
  ) -> Result<TracerHistogramHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_histogram(
        self.envoy_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(TracerHistogramHandle { id })
  }

  /// Define a labeled histogram metric.
  ///
  /// Returns a handle that can be used with `record_histogram_vec` together with label values.
  pub fn define_histogram_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<TracerHistogramVecHandle, abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_define_histogram(
        self.envoy_ptr,
        str_to_module_buffer(name),
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        &mut id,
      )
    })?;
    Ok(TracerHistogramVecHandle { id })
  }

  /// Increment a counter by the given value.
  pub fn increment_counter(
    &self,
    handle: TracerCounterHandle,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_increment_counter(
        self.envoy_ptr,
        handle.id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  /// Increment a labeled counter by the given value.
  pub fn increment_counter_vec(
    &self,
    handle: TracerCounterVecHandle,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_increment_counter(
        self.envoy_ptr,
        handle.id,
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        value,
      )
    })
  }

  /// Set a gauge to the given value.
  pub fn set_gauge(
    &self,
    handle: TracerGaugeHandle,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_set_gauge(
        self.envoy_ptr,
        handle.id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  /// Set a labeled gauge to the given value.
  pub fn set_gauge_vec(
    &self,
    handle: TracerGaugeVecHandle,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_set_gauge(
        self.envoy_ptr,
        handle.id,
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        value,
      )
    })
  }

  /// Record a value in a histogram.
  pub fn record_histogram(
    &self,
    handle: TracerHistogramHandle,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_record_histogram_value(
        self.envoy_ptr,
        handle.id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  /// Record a value in a labeled histogram.
  pub fn record_histogram_vec(
    &self,
    handle: TracerHistogramVecHandle,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let label_bufs: Vec<_> = labels.iter().map(|l| str_to_module_buffer(l)).collect();
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_tracer_record_histogram_value(
        self.envoy_ptr,
        handle.id,
        label_bufs.as_ptr() as *mut _,
        label_bufs.len(),
        value,
      )
    })
  }
}

// -----------------------------------------------------------------------------
// Tracer Traits
// -----------------------------------------------------------------------------

/// The module-side tracer configuration.
///
/// This trait must be implemented by the module to handle tracer configuration.
/// The object is created when the corresponding Envoy tracer config is loaded, and
/// it is dropped when the corresponding Envoy config is destroyed.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait TracerConfig: Send + Sync {
  /// Create a new span for an incoming request.
  ///
  /// Called when Envoy needs to start tracing a new request. The module can read
  /// incoming trace context headers via `envoy_span` and return a span instance
  /// that will handle the trace lifecycle.
  ///
  /// # Arguments
  /// * `envoy_span` - The Envoy span context for trace context access.
  /// * `operation_name` - The operation name for this span.
  /// * `traced` - Whether Envoy decided this request should be traced.
  /// * `reason` - The reason for the tracing decision.
  fn start_span(
    &self,
    envoy_span: &dyn EnvoyTracerSpan,
    operation_name: &str,
    traced: bool,
    reason: TraceReason,
  ) -> Option<Box<dyn TracerSpan>>;
}

/// The module-side span instance.
///
/// This trait must be implemented by the module to handle span lifecycle operations.
/// One instance is created per traced request.
pub trait TracerSpan: Send {
  /// Set the operation name for this span.
  fn set_operation(&mut self, operation: &str);

  /// Set a tag on this span.
  fn set_tag(&mut self, key: &str, value: &str);

  /// Record a log event on this span.
  fn log(&mut self, timestamp_ns: i64, event: &str);

  /// Finish this span and report it.
  fn finish(&mut self);

  /// Inject trace context into outgoing request headers for propagation.
  ///
  /// The module should use `envoy_span` to set propagation headers (e.g., traceparent).
  fn inject_context(&mut self, envoy_span: &dyn EnvoyTracerSpan);

  /// Create a child span from this span.
  fn spawn_child(&mut self, name: &str, start_time_ns: i64) -> Option<Box<dyn TracerSpan>>;

  /// Override the sampling decision for this span.
  fn set_sampled(&mut self, sampled: bool);

  /// Whether this span uses Envoy's local sampling decision.
  fn use_local_decision(&self) -> bool {
    true
  }

  /// Get a baggage value by key.
  fn get_baggage(&self, _key: &str) -> Option<String> {
    None
  }

  /// Set a baggage key/value pair.
  fn set_baggage(&mut self, _key: &str, _value: &str) {}

  /// Get the trace ID for this span.
  fn get_trace_id(&self) -> Option<String> {
    None
  }

  /// Get the span ID for this span.
  fn get_span_id(&self) -> Option<String> {
    None
  }
}

/// The tracing decision reason.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TraceReason {
  NotTraceable,
  HealthCheck,
  Sampling,
  ServiceForced,
  ClientForced,
}

impl From<abi::envoy_dynamic_module_type_trace_reason> for TraceReason {
  fn from(reason: abi::envoy_dynamic_module_type_trace_reason) -> Self {
    match reason {
      abi::envoy_dynamic_module_type_trace_reason::NotTraceable => TraceReason::NotTraceable,
      abi::envoy_dynamic_module_type_trace_reason::HealthCheck => TraceReason::HealthCheck,
      abi::envoy_dynamic_module_type_trace_reason::Sampling => TraceReason::Sampling,
      abi::envoy_dynamic_module_type_trace_reason::ServiceForced => TraceReason::ServiceForced,
      abi::envoy_dynamic_module_type_trace_reason::ClientForced => TraceReason::ClientForced,
    }
  }
}

/// Envoy-side trace context operations available to the module.
pub trait EnvoyTracerSpan: Send {
  /// Get a trace context header value by key.
  fn get_context_value(&self, key: &str) -> Option<Vec<u8>>;

  /// Set a trace context header value.
  fn set_context_value(&self, key: &str, value: &[u8]);

  /// Remove a trace context header.
  fn remove_context_value(&self, key: &str);

  /// Get the protocol of the traceable stream (e.g., "HTTP/1.1").
  fn get_protocol(&self) -> Option<Vec<u8>>;

  /// Get the host of the traceable stream.
  fn get_host(&self) -> Option<Vec<u8>>;

  /// Get the path of the traceable stream.
  fn get_path(&self) -> Option<Vec<u8>>;

  /// Get the method of the traceable stream.
  fn get_method(&self) -> Option<Vec<u8>>;
}

struct EnvoyTracerSpanImpl {
  raw: abi::envoy_dynamic_module_type_tracer_span_envoy_ptr,
}

unsafe impl Send for EnvoyTracerSpanImpl {}

impl EnvoyTracerSpanImpl {
  fn new(raw: abi::envoy_dynamic_module_type_tracer_span_envoy_ptr) -> Self {
    Self { raw }
  }

  fn get_string_value(
    &self,
    getter: unsafe extern "C" fn(
      abi::envoy_dynamic_module_type_tracer_span_envoy_ptr,
      *mut abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> bool,
  ) -> Option<Vec<u8>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let found = unsafe { getter(self.raw, &mut result) };
    if found && !result.ptr.is_null() {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(slice.to_vec())
    } else {
      None
    }
  }
}

impl EnvoyTracerSpan for EnvoyTracerSpanImpl {
  fn get_context_value(&self, key: &str) -> Option<Vec<u8>> {
    let key_buf = str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_tracer_get_trace_context_value(
        self.raw,
        key_buf,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(slice.to_vec())
    } else {
      None
    }
  }

  fn set_context_value(&self, key: &str, value: &[u8]) {
    let key_buf = str_to_module_buffer(key);
    let val_buf = bytes_to_module_buffer(value);
    unsafe {
      abi::envoy_dynamic_module_callback_tracer_set_trace_context_value(self.raw, key_buf, val_buf);
    }
  }

  fn remove_context_value(&self, key: &str) {
    let key_buf = str_to_module_buffer(key);
    unsafe {
      abi::envoy_dynamic_module_callback_tracer_remove_trace_context_value(self.raw, key_buf);
    }
  }

  fn get_protocol(&self) -> Option<Vec<u8>> {
    self.get_string_value(abi::envoy_dynamic_module_callback_tracer_get_trace_context_protocol)
  }

  fn get_host(&self) -> Option<Vec<u8>> {
    self.get_string_value(abi::envoy_dynamic_module_callback_tracer_get_trace_context_host)
  }

  fn get_path(&self) -> Option<Vec<u8>> {
    self.get_string_value(abi::envoy_dynamic_module_callback_tracer_get_trace_context_path)
  }

  fn get_method(&self) -> Option<Vec<u8>> {
    self.get_string_value(abi::envoy_dynamic_module_callback_tracer_get_trace_context_method)
  }
}

// -----------------------------------------------------------------------------
// Span Wrapper
// -----------------------------------------------------------------------------

/// Internal wrapper around a `Box<dyn TracerSpan>` that holds scratch storage for values
/// returned to C++ via module_buffer pointers. The C++ side copies the data immediately,
/// and the scratch string is freed when the wrapper (and thus the span) is destroyed.
struct SpanWrapper {
  inner: Box<dyn TracerSpan>,
  /// Scratch buffer for the last value returned by get_baggage, get_trace_id, or get_span_id.
  /// Kept alive so the pointer returned via module_buffer remains valid until the C++ side
  /// copies it.
  scratch: String,
}

impl SpanWrapper {
  fn new(inner: Box<dyn TracerSpan>) -> Self {
    Self {
      inner,
      scratch: String::new(),
    }
  }

  /// Store `val` in the scratch buffer and write a module_buffer pointing to it.
  fn write_scratch_to_out(
    &mut self,
    val: String,
    value_out: *mut abi::envoy_dynamic_module_type_module_buffer,
  ) {
    self.scratch = val;
    unsafe {
      (*value_out).ptr = self.scratch.as_ptr() as *const _;
      (*value_out).length = self.scratch.len();
    }
  }
}

// -----------------------------------------------------------------------------
// Panic-safe FFI helper
// -----------------------------------------------------------------------------

/// Decode an envoy_buffer into a `Cow<'static, str>` with lossy UTF-8 fallback.
///
/// Routes through [`crate::ffi_helpers::str_lossy_from_raw`] so a malformed input on the FFI
/// seam produces a lossy decode rather than undefined behaviour.
///
/// # Safety
///
/// Same constraints as [`crate::ffi_helpers::str_lossy_from_raw`].
unsafe fn envoy_buffer_to_str(
  buf: abi::envoy_dynamic_module_type_envoy_buffer,
) -> std::borrow::Cow<'static, str> {
  unsafe { crate::ffi_helpers::str_lossy_from_raw(buf.ptr as *const u8, buf.length) }
}

// -----------------------------------------------------------------------------
// Tracer Event Hook Implementations
// -----------------------------------------------------------------------------

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_config_new(
  config_envoy_ptr: abi::envoy_dynamic_module_type_tracer_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_tracer_config_module_ptr {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let name_str = unsafe { envoy_buffer_to_str(name) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    let ctx = TracerConfigContext {
      envoy_ptr: config_envoy_ptr,
    };
    let new_config_fn = NEW_TRACER_CONFIG_FUNCTION
      .get()
      .expect("NEW_TRACER_CONFIG_FUNCTION must be set");
    match new_config_fn(ctx, name_str.as_ref(), config_slice) {
      Some(config) => wrap_into_c_void_ptr!(config),
      None => std::ptr::null(),
    }
  }));
  match result {
    Ok(ptr) => ptr,
    Err(panic) => {
      crate::log_ffi_panic("envoy_dynamic_module_on_tracer_config_new", panic);
      std::ptr::null()
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_tracer_config_module_ptr,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let config = config_module_ptr as *mut *mut dyn TracerConfig;
    unsafe {
      let _outer = Box::from_raw(config);
      let _inner = Box::from_raw(*config);
    }
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_tracer_config_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_start_span(
  config_module_ptr: abi::envoy_dynamic_module_type_tracer_config_module_ptr,
  span_envoy_ptr: abi::envoy_dynamic_module_type_tracer_span_envoy_ptr,
  operation_name: abi::envoy_dynamic_module_type_envoy_buffer,
  traced: bool,
  reason: abi::envoy_dynamic_module_type_trace_reason,
) -> abi::envoy_dynamic_module_type_tracer_span_module_ptr {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let config = config_module_ptr as *const *const dyn TracerConfig;
    let config = unsafe { &**config };
    let envoy_span = EnvoyTracerSpanImpl::new(span_envoy_ptr);
    let op_name = unsafe { envoy_buffer_to_str(operation_name) };
    match config.start_span(&envoy_span, op_name.as_ref(), traced, reason.into()) {
      Some(span) => {
        let wrapper = SpanWrapper::new(span);
        wrap_into_c_void_ptr!(wrapper)
      },
      None => std::ptr::null(),
    }
  }));
  match result {
    Ok(ptr) => ptr,
    Err(panic) => {
      crate::log_ffi_panic("envoy_dynamic_module_on_tracer_start_span", panic);
      std::ptr::null()
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_set_operation(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  operation: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let op = unsafe { envoy_buffer_to_str(operation) };
    wrapper.inner.set_operation(op.as_ref());
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_set_tag(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  key: abi::envoy_dynamic_module_type_envoy_buffer,
  value: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let k = unsafe { envoy_buffer_to_str(key) };
    let v = unsafe { envoy_buffer_to_str(value) };
    wrapper.inner.set_tag(k.as_ref(), v.as_ref());
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_log(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  timestamp_ns: i64,
  event: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let e = unsafe { envoy_buffer_to_str(event) };
    wrapper.inner.log(timestamp_ns, e.as_ref());
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_finish(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    wrapper.inner.finish();
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_inject_context(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  span_envoy_ptr: abi::envoy_dynamic_module_type_tracer_span_envoy_ptr,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let envoy_span = EnvoyTracerSpanImpl::new(span_envoy_ptr);
    wrapper.inner.inject_context(&envoy_span);
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_spawn_child(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  start_time_ns: i64,
) -> abi::envoy_dynamic_module_type_tracer_span_module_ptr {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let n = unsafe { envoy_buffer_to_str(name) };
    match wrapper.inner.spawn_child(n.as_ref(), start_time_ns) {
      Some(child) => {
        let child_wrapper = SpanWrapper::new(child);
        wrap_into_c_void_ptr!(child_wrapper)
      },
      None => std::ptr::null(),
    }
  }));
  match result {
    Ok(ptr) => ptr,
    Err(panic) => {
      crate::log_ffi_panic("envoy_dynamic_module_on_tracer_span_spawn_child", panic);
      std::ptr::null()
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_set_sampled(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  sampled: bool,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    wrapper.inner.set_sampled(sampled);
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_use_local_decision(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
) -> bool {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(span_module_ptr as *const SpanWrapper) };
    wrapper.inner.use_local_decision()
  }));
  result.unwrap_or(true)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_get_baggage(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  key: abi::envoy_dynamic_module_type_envoy_buffer,
  value_out: *mut abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let k = unsafe { envoy_buffer_to_str(key) };
    match wrapper.inner.get_baggage(k.as_ref()) {
      Some(val) => {
        wrapper.write_scratch_to_out(val, value_out);
        true
      },
      None => {
        unsafe {
          (*value_out).ptr = std::ptr::null();
          (*value_out).length = 0;
        }
        false
      },
    }
  }));
  match result {
    Ok(found) => found,
    Err(_) => {
      unsafe {
        (*value_out).ptr = std::ptr::null();
        (*value_out).length = 0;
      }
      false
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_set_baggage(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  key: abi::envoy_dynamic_module_type_envoy_buffer,
  value: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    let k = unsafe { envoy_buffer_to_str(key) };
    let v = unsafe { envoy_buffer_to_str(value) };
    wrapper.inner.set_baggage(k.as_ref(), v.as_ref());
  }));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_get_trace_id(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  value_out: *mut abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    match wrapper.inner.get_trace_id() {
      Some(val) => {
        wrapper.write_scratch_to_out(val, value_out);
        true
      },
      None => {
        unsafe {
          (*value_out).ptr = std::ptr::null();
          (*value_out).length = 0;
        }
        false
      },
    }
  }));
  match result {
    Ok(found) => found,
    Err(_) => {
      unsafe {
        (*value_out).ptr = std::ptr::null();
        (*value_out).length = 0;
      }
      false
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_get_span_id(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
  value_out: *mut abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(span_module_ptr as *mut SpanWrapper) };
    match wrapper.inner.get_span_id() {
      Some(val) => {
        wrapper.write_scratch_to_out(val, value_out);
        true
      },
      None => {
        unsafe {
          (*value_out).ptr = std::ptr::null();
          (*value_out).length = 0;
        }
        false
      },
    }
  }));
  match result {
    Ok(found) => found,
    Err(_) => {
      unsafe {
        (*value_out).ptr = std::ptr::null();
        (*value_out).length = 0;
      }
      false
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_tracer_span_destroy(
  span_module_ptr: abi::envoy_dynamic_module_type_tracer_span_module_ptr,
) {
  let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    let wrapper = span_module_ptr as *mut SpanWrapper;
    unsafe {
      let _ = Box::from_raw(wrapper);
    }
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_tracer_span_destroy", panic);
  });
}

/// Declare the init functions for the tracer dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with [`NewTracerConfigFunction`] type.
#[macro_export]
macro_rules! declare_tracer_init_functions {
  ($f:ident, $new_tracer_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| {
        envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
          envoy_proxy_dynamic_modules_rust_sdk::NEW_TRACER_CONFIG_FUNCTION,
          $new_tracer_config_fn,
          "NEW_TRACER_CONFIG_FUNCTION"
        );
        if ($f()) {
          envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
            as *const ::std::os::raw::c_char
        } else {
          ::std::ptr::null()
        }
      })) {
        ::std::result::Result::Ok(v) => v,
        ::std::result::Result::Err(payload) => {
          $crate::log_ffi_panic("envoy_dynamic_module_on_program_init", payload);
          ::std::ptr::null()
        },
      }
    }
  };
}
