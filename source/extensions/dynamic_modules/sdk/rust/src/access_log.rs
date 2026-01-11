//! Access logger support for dynamic modules.
//!
//! This module provides traits and types for implementing access loggers as dynamic modules.

use crate::abi;
use std::ffi::c_void;
use std::{ptr, slice, str};

// -----------------------------------------------------------------------------
// Metrics Support
// -----------------------------------------------------------------------------

/// Handle for a counter metric. Counters can only be incremented.
#[derive(Debug, Clone, Copy)]
pub struct CounterHandle {
  id: usize,
}

/// Handle for a gauge metric. Gauges can be set, incremented, or decremented.
#[derive(Debug, Clone, Copy)]
pub struct GaugeHandle {
  id: usize,
}

/// Handle for a histogram metric. Histograms record value distributions.
#[derive(Debug, Clone, Copy)]
pub struct HistogramHandle {
  id: usize,
}

/// Provides access to metrics operations during configuration.
///
/// This is passed to `AccessLoggerConfig::new` to allow defining metrics.
pub struct ConfigContext {
  envoy_ptr: *mut c_void,
}

impl ConfigContext {
  /// Create a new ConfigContext. Used internally by the macro.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Define a counter metric.
  ///
  /// Counters are cumulative metrics that can only increase. They are reset on restart.
  /// Returns a handle that can be used to increment the counter.
  pub fn define_counter(&self, name: &str) -> Option<CounterHandle> {
    let name_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: name.as_ptr() as *const _,
      length: name.len(),
    };
    let mut id: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_config_define_counter(
        self.envoy_ptr,
        name_buf,
        &mut id,
      )
    };
    if result == abi::envoy_dynamic_module_type_metrics_result::Success {
      Some(CounterHandle { id })
    } else {
      None
    }
  }

  /// Define a gauge metric.
  ///
  /// Gauges are metrics that can go up and down. They represent a current value.
  /// Returns a handle that can be used to set/increment/decrement the gauge.
  pub fn define_gauge(&self, name: &str) -> Option<GaugeHandle> {
    let name_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: name.as_ptr() as *const _,
      length: name.len(),
    };
    let mut id: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_config_define_gauge(
        self.envoy_ptr,
        name_buf,
        &mut id,
      )
    };
    if result == abi::envoy_dynamic_module_type_metrics_result::Success {
      Some(GaugeHandle { id })
    } else {
      None
    }
  }

  /// Define a histogram metric.
  ///
  /// Histograms track the distribution of values. They are useful for measuring latencies.
  /// Returns a handle that can be used to record values in the histogram.
  pub fn define_histogram(&self, name: &str) -> Option<HistogramHandle> {
    let name_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: name.as_ptr() as *const _,
      length: name.len(),
    };
    let mut id: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_config_define_histogram(
        self.envoy_ptr,
        name_buf,
        &mut id,
      )
    };
    if result == abi::envoy_dynamic_module_type_metrics_result::Success {
      Some(HistogramHandle { id })
    } else {
      None
    }
  }

  /// Get the raw Envoy pointer. Used internally.
  #[doc(hidden)]
  pub fn envoy_ptr(&self) -> *mut c_void {
    self.envoy_ptr
  }
}

/// Provides access to metrics operations at runtime.
///
/// This is stored in the logger and used to update metric values during log events.
pub struct MetricsContext {
  envoy_ptr: *mut c_void,
}

// SAFETY: The envoy_ptr points to Envoy's DynamicModuleAccessLogConfig which is thread-safe.
// The metrics callbacks are designed to be called from any thread.
unsafe impl Send for MetricsContext {}

impl MetricsContext {
  /// Create a new MetricsContext. Used internally by the macro.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Increment a counter by the given value.
  pub fn increment_counter(&self, handle: CounterHandle, value: u64) -> bool {
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_increment_counter(
        self.envoy_ptr,
        handle.id,
        value,
      )
    };
    result == abi::envoy_dynamic_module_type_metrics_result::Success
  }

  /// Set a gauge to the given value.
  pub fn set_gauge(&self, handle: GaugeHandle, value: u64) -> bool {
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_set_gauge(self.envoy_ptr, handle.id, value)
    };
    result == abi::envoy_dynamic_module_type_metrics_result::Success
  }

  /// Increment a gauge by the given value.
  pub fn increment_gauge(&self, handle: GaugeHandle, value: u64) -> bool {
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_increment_gauge(
        self.envoy_ptr,
        handle.id,
        value,
      )
    };
    result == abi::envoy_dynamic_module_type_metrics_result::Success
  }

  /// Decrement a gauge by the given value.
  pub fn decrement_gauge(&self, handle: GaugeHandle, value: u64) -> bool {
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_decrement_gauge(
        self.envoy_ptr,
        handle.id,
        value,
      )
    };
    result == abi::envoy_dynamic_module_type_metrics_result::Success
  }

  /// Record a value in a histogram.
  pub fn record_histogram(&self, handle: HistogramHandle, value: u64) -> bool {
    let result = unsafe {
      abi::envoy_dynamic_module_callback_access_logger_record_histogram_value(
        self.envoy_ptr,
        handle.id,
        value,
      )
    };
    result == abi::envoy_dynamic_module_type_metrics_result::Success
  }
}

/// Trait that the dynamic module must implement to provide the access logger configuration.
pub trait AccessLoggerConfig: Sized + Send + Sync + 'static {
  /// Create a new configuration from the provided name and config bytes.
  ///
  /// The `ctx` provides access to metrics definition APIs. Metrics should be defined
  /// during configuration creation and the handles stored in the config for later use.
  fn new(ctx: &ConfigContext, name: &str, config: &[u8]) -> Result<Self, String>;

  /// Create a logger instance. Called per-thread for thread-local loggers.
  ///
  /// The `metrics` context is stored and can be used to update metrics during log events.
  fn create_logger(&self, metrics: MetricsContext) -> Box<dyn AccessLogger>;
}

/// Logger trait that handles individual log events.
pub trait AccessLogger: Send {
  /// Called when a log event occurs.
  fn log(&mut self, ctx: &LogContext);

  /// Called to flush buffered logs before the logger is destroyed.
  ///
  /// This is called during shutdown when the thread-local logger is being destroyed.
  /// Modules that buffer log entries should implement this to ensure no logs are lost.
  ///
  /// This is optional. The default implementation does nothing.
  fn flush(&mut self) {}
}

/// Timing information from the stream info.
#[derive(Debug, Clone, Default)]
pub struct TimingInfo {
  /// Request start time as Unix timestamp in nanoseconds.
  pub start_time_unix_ns: i64,
  /// Duration from start to request complete in nanoseconds, or -1 if not available.
  pub request_complete_duration_ns: i64,
  /// Time of first upstream TX byte sent in nanoseconds, or -1 if not available.
  pub first_upstream_tx_byte_sent_ns: i64,
  /// Time of last upstream TX byte sent in nanoseconds, or -1 if not available.
  pub last_upstream_tx_byte_sent_ns: i64,
  /// Time of first upstream RX byte received in nanoseconds, or -1 if not available.
  pub first_upstream_rx_byte_received_ns: i64,
  /// Time of last upstream RX byte received in nanoseconds, or -1 if not available.
  pub last_upstream_rx_byte_received_ns: i64,
  /// Time of first downstream TX byte sent in nanoseconds, or -1 if not available.
  pub first_downstream_tx_byte_sent_ns: i64,
  /// Time of last downstream TX byte sent in nanoseconds, or -1 if not available.
  pub last_downstream_tx_byte_sent_ns: i64,
}

/// Byte count information from the stream info.
#[derive(Debug, Clone, Default)]
pub struct BytesInfo {
  /// Total bytes received from downstream.
  pub bytes_received: u64,
  /// Total bytes sent to downstream.
  pub bytes_sent: u64,
  /// Wire bytes received (including TLS overhead).
  pub wire_bytes_received: u64,
  /// Wire bytes sent (including TLS overhead).
  pub wire_bytes_sent: u64,
}

/// Read-only context for accessing log event data.
pub struct LogContext {
  // Private field - only accessible within this crate.
  pub(crate) envoy_ptr: *mut c_void,
}

impl LogContext {
  /// Create a new LogContext. Used internally by the macro.
  #[doc(hidden)]
  pub fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get the HTTP response code.
  pub fn response_code(&self) -> Option<u32> {
    let code =
      unsafe { abi::envoy_dynamic_module_callback_access_logger_get_response_code(self.envoy_ptr) };

    if code != 0 {
      Some(code)
    } else {
      None
    }
  }

  /// Get the response code details string.
  pub fn response_code_details(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_response_code_details(
        self.envoy_ptr,
        &mut buffer,
      )
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Get the request protocol (e.g., "HTTP/1.1", "HTTP/2").
  pub fn protocol(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_protocol(self.envoy_ptr, &mut buffer)
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Get timing information.
  pub fn timing_info(&self) -> TimingInfo {
    let mut info = abi::envoy_dynamic_module_type_timing_info {
      start_time_unix_ns: 0,
      request_complete_duration_ns: -1,
      first_upstream_tx_byte_sent_ns: -1,
      last_upstream_tx_byte_sent_ns: -1,
      first_upstream_rx_byte_received_ns: -1,
      last_upstream_rx_byte_received_ns: -1,
      first_downstream_tx_byte_sent_ns: -1,
      last_downstream_tx_byte_sent_ns: -1,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_timing_info(self.envoy_ptr, &mut info);
    }
    TimingInfo {
      start_time_unix_ns: info.start_time_unix_ns,
      request_complete_duration_ns: info.request_complete_duration_ns,
      first_upstream_tx_byte_sent_ns: info.first_upstream_tx_byte_sent_ns,
      last_upstream_tx_byte_sent_ns: info.last_upstream_tx_byte_sent_ns,
      first_upstream_rx_byte_received_ns: info.first_upstream_rx_byte_received_ns,
      last_upstream_rx_byte_received_ns: info.last_upstream_rx_byte_received_ns,
      first_downstream_tx_byte_sent_ns: info.first_downstream_tx_byte_sent_ns,
      last_downstream_tx_byte_sent_ns: info.last_downstream_tx_byte_sent_ns,
    }
  }

  /// Get byte count information.
  pub fn bytes_info(&self) -> BytesInfo {
    let mut info = abi::envoy_dynamic_module_type_bytes_info {
      bytes_received: 0,
      bytes_sent: 0,
      wire_bytes_received: 0,
      wire_bytes_sent: 0,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_bytes_info(self.envoy_ptr, &mut info);
    }
    BytesInfo {
      bytes_received: info.bytes_received,
      bytes_sent: info.bytes_sent,
      wire_bytes_received: info.wire_bytes_received,
      wire_bytes_sent: info.wire_bytes_sent,
    }
  }

  /// Get the route name.
  pub fn route_name(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_route_name(self.envoy_ptr, &mut buffer)
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Check if this is a health check request.
  pub fn is_health_check(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_access_logger_is_health_check(self.envoy_ptr) }
  }

  /// Get the upstream cluster name.
  pub fn upstream_cluster(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_upstream_cluster(
        self.envoy_ptr,
        &mut buffer,
      )
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Get the upstream host hostname.
  pub fn upstream_host(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_upstream_host(
        self.envoy_ptr,
        &mut buffer,
      )
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Get the connection ID, or 0 if not available.
  pub fn connection_id(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_access_logger_get_connection_id(self.envoy_ptr) }
  }

  /// Check if mTLS was used for the connection.
  pub fn is_mtls(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_access_logger_is_mtls(self.envoy_ptr) }
  }

  /// Get the requested server name (SNI).
  pub fn requested_server_name(&self) -> Option<&str> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_requested_server_name(
        self.envoy_ptr,
        &mut buffer,
      )
    } {
      unsafe {
        let slice = slice::from_raw_parts(buffer.ptr as *const u8, buffer.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Check if the request was sampled for tracing.
  pub fn is_trace_sampled(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_access_logger_is_trace_sampled(self.envoy_ptr) }
  }

  /// Get a request header value by key.
  ///
  /// For headers with multiple values, use `index` to access subsequent values.
  /// Returns the total count of values in `total_count` if provided.
  pub fn get_request_header(&self, key: &str) -> Option<&[u8]> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
      key,
      0,
    )
  }

  /// Get a response header value by key.
  pub fn get_response_header(&self, key: &str) -> Option<&[u8]> {
    self.get_header_value(
      abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
      key,
      0,
    )
  }

  /// Get a header value by type and key.
  fn get_header_value(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
    key: &str,
    index: usize,
  ) -> Option<&[u8]> {
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_header_value(
        self.envoy_ptr,
        header_type,
        key_buf,
        &mut result,
        index,
        ptr::null_mut(),
      )
    } {
      unsafe {
        Some(slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
      }
    } else {
      None
    }
  }

  /// Get a value from dynamic metadata.
  ///
  /// # Arguments
  /// * `filter_name` - The filter namespace (e.g., "envoy.filters.http.dynamic_module")
  /// * `key` - The key within the filter namespace (e.g., "rbac_policy")
  ///
  /// # Returns
  /// The string value if it exists, None otherwise.
  /// Note: Only string values are currently supported.
  pub fn get_dynamic_metadata(&self, filter_name: &str, key: &str) -> Option<&str> {
    let filter_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: filter_name.as_ptr() as *const _,
      length: filter_name.len(),
    };
    let key_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: key.as_ptr() as *const _,
      length: key.len(),
    };
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
        self.envoy_ptr,
        filter_buf,
        key_buf,
        &mut result,
      )
    } {
      unsafe {
        let slice = slice::from_raw_parts(result.ptr as *const u8, result.length);
        str::from_utf8(slice).ok()
      }
    } else {
      None
    }
  }

  /// Get the local reply body (if this was a local response).
  pub fn local_reply_body(&self) -> Option<&[u8]> {
    let mut buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_local_reply_body(
        self.envoy_ptr,
        &mut buffer,
      )
    } {
      unsafe {
        Some(slice::from_raw_parts(
          buffer.ptr as *const u8,
          buffer.length,
        ))
      }
    } else {
      None
    }
  }
}

/// Macro to declare access logger entry points.
///
/// This macro generates the required C ABI functions that Envoy calls to interact with
/// the access logger implementation.
///
/// # Example
///
/// ```ignore
/// use envoy_dynamic_modules_rust_sdk::{access_log::*, declare_access_logger};
///
/// struct MyLoggerConfig {
///     format: String,
///     logs_counter: CounterHandle,
///     config_envoy_ptr: *mut std::ffi::c_void,
/// }
///
/// unsafe impl Send for MyLoggerConfig {}
/// unsafe impl Sync for MyLoggerConfig {}
///
/// impl AccessLoggerConfig for MyLoggerConfig {
///     fn new(ctx: &ConfigContext, name: &str, config: &[u8]) -> Result<Self, String> {
///         let logs_counter = ctx.define_counter("logs_total")
///             .ok_or("Failed to define counter")?;
///         Ok(Self {
///             format: String::from_utf8_lossy(config).to_string(),
///             logs_counter,
///             config_envoy_ptr: ctx.envoy_ptr(),
///         })
///     }
///
///     fn create_logger(&self, metrics: MetricsContext) -> Box<dyn AccessLogger> {
///         Box::new(MyLogger {
///             format: self.format.clone(),
///             logs_counter: self.logs_counter,
///             metrics,
///         })
///     }
/// }
///
/// struct MyLogger {
///     format: String,
///     logs_counter: CounterHandle,
///     metrics: MetricsContext,
/// }
///
/// impl AccessLogger for MyLogger {
///     fn log(&mut self, ctx: &LogContext) {
///         self.metrics.increment_counter(self.logs_counter, 1);
///         if let Some(code) = ctx.response_code() {
///             println!("Response: {}", code);
///         }
///     }
/// }
///
/// declare_access_logger!(MyLoggerConfig);
/// ```
#[macro_export]
macro_rules! declare_access_logger {
  ($config_type:ty) => {
    /// Wrapper that stores both the config and the envoy pointer for metrics access.
    struct AccessLoggerConfigWrapper {
      config: $config_type,
      config_envoy_ptr: *mut ::std::ffi::c_void,
    }

    unsafe impl Send for AccessLoggerConfigWrapper {}
    unsafe impl Sync for AccessLoggerConfigWrapper {}

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_config_new(
      config_envoy_ptr: *mut ::std::ffi::c_void,
      name: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      config: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> *const ::std::ffi::c_void {
      let name_str = unsafe {
        let slice = ::std::slice::from_raw_parts(name.ptr as *const u8, name.length);
        ::std::str::from_utf8(slice).unwrap_or("")
      };
      let config_bytes =
        unsafe { ::std::slice::from_raw_parts(config.ptr as *const u8, config.length) };

      let ctx = $crate::access_log::ConfigContext::new(config_envoy_ptr);
      match <$config_type as $crate::access_log::AccessLoggerConfig>::new(
        &ctx,
        name_str,
        config_bytes,
      ) {
        Ok(c) => {
          let wrapper = AccessLoggerConfigWrapper {
            config: c,
            config_envoy_ptr,
          };
          Box::into_raw(Box::new(wrapper)) as *const ::std::ffi::c_void
        },
        Err(_) => ::std::ptr::null(),
      }
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_config_destroy(
      config_ptr: *const ::std::ffi::c_void,
    ) {
      unsafe {
        drop(Box::from_raw(config_ptr as *mut AccessLoggerConfigWrapper));
      }
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_new(
      config_ptr: *const ::std::ffi::c_void,
      _logger_envoy_ptr: *mut ::std::ffi::c_void,
    ) -> *const ::std::ffi::c_void {
      let wrapper = unsafe { &*(config_ptr as *const AccessLoggerConfigWrapper) };
      let metrics = $crate::access_log::MetricsContext::new(wrapper.config_envoy_ptr);
      let logger = wrapper.config.create_logger(metrics);
      Box::into_raw(Box::new(logger)) as *const ::std::ffi::c_void
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_log(
      envoy_ptr: *mut ::std::ffi::c_void,
      logger_ptr: *mut ::std::ffi::c_void,
      _log_type: $crate::abi::envoy_dynamic_module_type_access_log_type,
    ) {
      let logger = unsafe { &mut *(logger_ptr as *mut Box<dyn $crate::access_log::AccessLogger>) };
      let ctx = $crate::access_log::LogContext::new(envoy_ptr);
      logger.log(&ctx);
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_destroy(
      logger_ptr: *mut ::std::ffi::c_void,
    ) {
      unsafe {
        drop(Box::from_raw(
          logger_ptr as *mut Box<dyn $crate::access_log::AccessLogger>,
        ));
      }
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_flush(
      logger_ptr: *mut ::std::ffi::c_void,
    ) {
      let logger = unsafe { &mut *(logger_ptr as *mut Box<dyn $crate::access_log::AccessLogger>) };
      logger.flush();
    }
  };
}
