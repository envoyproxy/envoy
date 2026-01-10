//! Access logger support for dynamic modules.
//!
//! This module provides traits and types for implementing access loggers as dynamic modules.

use crate::abi;
use std::ffi::c_void;
use std::{ptr, slice, str};

/// Trait that the dynamic module must implement to provide the access logger configuration.
pub trait AccessLoggerConfig: Sized + Send + Sync + 'static {
  /// Create a new configuration from the provided name and config bytes.
  fn new(name: &str, config: &[u8]) -> Result<Self, String>;

  /// Create a logger instance. Called per-thread for thread-local loggers.
  fn create_logger(&self) -> Box<dyn AccessLogger>;
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
    let mut code = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_access_logger_get_response_code(self.envoy_ptr, &mut code)
    } {
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
/// }
///
/// impl AccessLoggerConfig for MyLoggerConfig {
///     fn new(name: &str, config: &[u8]) -> Result<Self, String> {
///         Ok(Self {
///             format: String::from_utf8_lossy(config).to_string(),
///         })
///     }
///
///     fn create_logger(&self) -> Box<dyn AccessLogger> {
///         Box::new(MyLogger { format: self.format.clone() })
///     }
/// }
///
/// struct MyLogger {
///     format: String,
/// }
///
/// impl AccessLogger for MyLogger {
///     fn log(&mut self, ctx: &LogContext) {
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
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_config_new(
      _config_envoy_ptr: *mut ::std::ffi::c_void,
      name: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      config: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> *const ::std::ffi::c_void {
      let name_str = unsafe {
        let slice = ::std::slice::from_raw_parts(name.ptr as *const u8, name.length);
        ::std::str::from_utf8(slice).unwrap_or("")
      };
      let config_bytes =
        unsafe { ::std::slice::from_raw_parts(config.ptr as *const u8, config.length) };

      match <$config_type as $crate::access_log::AccessLoggerConfig>::new(name_str, config_bytes) {
        Ok(c) => Box::into_raw(Box::new(c)) as *const ::std::ffi::c_void,
        Err(_) => ::std::ptr::null(),
      }
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_config_destroy(
      config_ptr: *const ::std::ffi::c_void,
    ) {
      unsafe {
        drop(Box::from_raw(config_ptr as *mut $config_type));
      }
    }

    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_access_logger_new(
      config_ptr: *const ::std::ffi::c_void,
      _logger_envoy_ptr: *mut ::std::ffi::c_void,
    ) -> *const ::std::ffi::c_void {
      let config = unsafe { &*(config_ptr as *const $config_type) };
      let logger = config.create_logger();
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
