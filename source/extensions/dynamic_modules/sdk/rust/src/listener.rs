use crate::buffer::EnvoyBuffer;
use crate::{
  abi, drop_wrapped_c_void_ptr, str_to_module_buffer, wrap_into_c_void_ptr, EnvoyCounterId,
  EnvoyGaugeId, EnvoyHistogramId, NewListenerFilterConfigFunction,
  NEW_LISTENER_FILTER_CONFIG_FUNCTION,
};
use mockall::*;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// The trait that represents the Envoy listener filter configuration.
/// This is used in [`NewListenerFilterConfigFunction`] to pass the Envoy filter configuration
/// to the dynamic module.
#[automock]
pub trait EnvoyListenerFilterConfig {
  /// Define a new counter scoped to this filter config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this filter config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this filter config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Create a new implementation of the [`EnvoyListenerFilterConfigScheduler`] trait.
  fn new_config_scheduler(&self) -> impl EnvoyListenerFilterConfigScheduler + 'static;
}

/// The trait that represents the configuration for an Envoy listener filter configuration.
/// This has one to one mapping with the [`EnvoyListenerFilterConfig`] object.
///
/// The object is created when the corresponding Envoy listener filter config is created, and it is
/// dropped when the corresponding Envoy listener filter config is destroyed. Therefore, the
/// implementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
///
/// Implementations must also be `Sync` since they are accessed from worker threads.
pub trait ListenerFilterConfig<ELF: EnvoyListenerFilter>: Sync {
  /// This is called from a worker thread when a new connection is accepted.
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>>;

  /// This is called when the new event is scheduled via the
  /// [`EnvoyListenerFilterConfigScheduler::commit`] for this [`ListenerFilterConfig`].
  ///
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyListenerFilterConfigScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyListenerFilterConfig::new_config_scheduler`] for more details on how to use this.
  fn on_config_scheduled(&self, _event_id: u64) {}
}

/// The trait that corresponds to an Envoy listener filter for each accepted connection
/// created via the [`ListenerFilterConfig::new_listener_filter`] method.
///
/// All the event hooks are called on the same thread as the one that the [`ListenerFilter`] is
/// created via the [`ListenerFilterConfig::new_listener_filter`] method.
pub trait ListenerFilter<ELF: EnvoyListenerFilter> {
  /// This is called when a new connection is accepted on the listener.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_listener_filter_status`] to
  /// indicate the status of the connection acceptance processing.
  fn on_accept(
    &mut self,
    _envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }

  /// This is called when data is available to be read from the connection.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_listener_filter_status`] to
  /// indicate the status of the data processing.
  fn on_data(
    &mut self,
    _envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }

  /// This is called when the listener filter is destroyed for each accepted connection.
  fn on_close(&mut self, _envoy_filter: &mut ELF) {}

  /// This is called when the HTTP callout response is received initiated by a listener filter.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `callout_id` is the ID of the callout. This is used to differentiate between multiple calls.
  /// * `result` is the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers. This is optional.
  /// * `response_body` is the response body. This is optional.
  fn on_http_callout_done(
    &mut self,
    _envoy_filter: &mut ELF,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Vec<(EnvoyBuffer, EnvoyBuffer)>,
    _response_body: Vec<EnvoyBuffer>,
  ) {
  }

  /// This is called when the new event is scheduled via the
  /// [`EnvoyListenerFilterScheduler::commit`] for this [`ListenerFilter`].
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyListenerFilterScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyListenerFilter::new_scheduler`] for more details on how to use this.
  fn on_scheduled(&mut self, _envoy_filter: &mut ELF, _event_id: u64) {}
}

/// The trait that represents the Envoy listener filter.
/// This is used in [`ListenerFilter`] to interact with the underlying Envoy listener filter object.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyListenerFilter {
  /// Get the current buffer chunk available for reading.
  /// Returns None if no buffer is available.
  fn get_buffer_chunk<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Drain bytes from the beginning of the buffer.
  fn drain_buffer(&mut self, length: usize);

  /// Get the remote (client) address and port.
  /// Returns None if the address is not available or not an IP address.
  fn get_remote_address(&self) -> Option<(String, u32)>;

  /// Get the local address and port.
  /// Returns None if the address is not available or not an IP address.
  fn get_local_address(&self) -> Option<(String, u32)>;

  /// Get the direct remote (client) address and port without considering proxy protocol.
  /// Returns None if the address is not available or not an IP address.
  fn get_direct_remote_address(&self) -> Option<(String, u32)>;

  /// Get the direct local address and port without considering proxy protocol.
  /// Returns None if the address is not available or not an IP address.
  fn get_direct_local_address(&self) -> Option<(String, u32)>;

  /// Get the original destination address and port (e.g., from SO_ORIGINAL_DST).
  /// Returns None if not available.
  fn get_original_dst(&self) -> Option<(String, u32)>;

  /// Get the address type of the remote address.
  fn get_address_type(&self) -> abi::envoy_dynamic_module_type_address_type;

  /// Check if the local address has been restored (e.g., by original_dst filter).
  fn is_local_address_restored(&self) -> bool;

  /// Indicate to Envoy that the original destination address should be used.
  fn use_original_dst(&mut self);

  /// Set the downstream transport failure reason.
  fn set_downstream_transport_failure_reason(&mut self, reason: &str);

  /// Get the connection start time in milliseconds since epoch.
  fn get_connection_start_time_ms(&self) -> u64;

  /// Get the string-typed dynamic metadata value with the given namespace and key value.
  /// Returns None if the metadata is not found or is the wrong type.
  fn get_dynamic_metadata_string(&self, namespace: &str, key: &str) -> Option<String>;

  /// Set the string-typed dynamic metadata value with the given namespace and key value.
  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str);

  /// Get the number-typed dynamic metadata value with the given namespace and key value.
  /// Returns None if the metadata is not found or is the wrong type.
  fn get_dynamic_metadata_number(&self, namespace: &str, key: &str) -> Option<f64>;

  /// Set the number-typed dynamic metadata value with the given namespace and key value.
  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64);

  /// Get the maximum number of bytes to read from the socket.
  /// This is used to determine the buffer size for reading data.
  fn max_read_bytes(&self) -> usize;

  /// Get the requested server name (SNI) from the connection socket.
  /// Returns None if SNI is not available.
  fn get_requested_server_name<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get the detected transport protocol (e.g., "tls", "raw_buffer") from the connection socket.
  /// Returns None if the transport protocol is not available.
  fn get_detected_transport_protocol<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get the requested application protocols (ALPN) from the connection socket.
  /// Returns an empty vector if no application protocols are available.
  fn get_requested_application_protocols<'a>(&'a self) -> Vec<EnvoyBuffer<'a>>;

  /// Get the JA3 fingerprint hash from the connection socket.
  /// Returns None if the JA3 hash is not available.
  fn get_ja3_hash<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get the JA4 fingerprint hash from the connection socket.
  /// Returns None if the JA4 hash is not available.
  fn get_ja4_hash<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Check if SSL/TLS connection information is available on the socket.
  fn is_ssl(&self) -> bool;

  /// Get the SSL URI SANs from the peer certificate.
  /// Returns an empty vector if the connection is not SSL or no URI SANs are present.
  fn get_ssl_uri_sans<'a>(&'a self) -> Vec<EnvoyBuffer<'a>>;

  /// Get the SSL DNS SANs from the peer certificate.
  /// Returns an empty vector if the connection is not SSL or no DNS SANs are present.
  fn get_ssl_dns_sans<'a>(&'a self) -> Vec<EnvoyBuffer<'a>>;

  /// Get the SSL subject from the peer certificate.
  /// Returns None if the connection is not SSL or subject is not available.
  fn get_ssl_subject<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get the raw socket file descriptor.
  /// Returns -1 if the socket is not available.
  fn get_socket_fd(&self) -> i64;

  /// Set an integer socket option directly on the accepted socket via setsockopt().
  /// Returns true if the option was set successfully.
  fn set_socket_option_int(&mut self, level: i64, name: i64, value: i64) -> bool;

  /// Set a bytes socket option directly on the accepted socket via setsockopt().
  /// Returns true if the option was set successfully.
  fn set_socket_option_bytes(&mut self, level: i64, name: i64, value: &[u8]) -> bool;

  /// Get an integer socket option directly from the accepted socket via getsockopt().
  /// This reads the actual value from the socket, including options set by other filters.
  /// Returns None if the option could not be retrieved.
  fn get_socket_option_int(&self, level: i64, name: i64) -> Option<i64>;

  /// Get a bytes socket option directly from the accepted socket via getsockopt().
  /// This reads the actual value from the socket, including options set by other filters.
  /// Returns None if the option could not be retrieved.
  fn get_socket_option_bytes(&self, level: i64, name: i64, max_size: usize) -> Option<Vec<u8>>;

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Send an HTTP callout to the given cluster with the given headers and optional body.
  ///
  /// Headers must contain the `:method`, `:path`, and `host` headers.
  ///
  /// This returns the status and callout id of the callout. The id is used to
  /// distinguish different callouts made from the same filter and is generated by Envoy.
  /// The meaning of the status is:
  ///
  ///   * Success: The callout was sent successfully.
  ///   * MissingRequiredHeaders: One of the required headers is missing: `:method`, `:path`, or
  ///     `host`.
  ///   * ClusterNotFound: The cluster with the given name was not found.
  ///   * CannotCreateRequest: The request could not be created. This happens when, for example,
  ///     there's no healthy upstream host in the cluster.
  ///
  /// The callout result will be delivered to the [`ListenerFilter::on_http_callout_done`] method.
  fn send_http_callout<'a>(
    &mut self,
    _cluster_name: &'a str,
    _headers: Vec<(&'a str, &'a [u8])>,
    _body: Option<&'a [u8]>,
    _timeout_milliseconds: u64,
  ) -> (
    abi::envoy_dynamic_module_type_http_callout_init_result,
    u64, // callout handle
  );

  /// Create a new implementation of the [`EnvoyListenerFilterScheduler`] trait.
  ///
  /// ## Example Usage
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  /// use std::thread;
  ///
  /// struct TestFilter;
  /// impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for TestFilter {
  ///   fn on_accept(
  ///     &mut self,
  ///     envoy_filter: &mut ELF,
  ///   ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
  ///     let scheduler = envoy_filter.new_scheduler();
  ///     let _ = std::thread::spawn(move || {
  ///       // Do some work in a separate thread.
  ///       // ...
  ///       // Then schedule the event to continue processing.
  ///       scheduler.commit(123);
  ///     });
  ///     abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
  ///   }
  ///
  ///   fn on_scheduled(&mut self, _envoy_filter: &mut ELF, event_id: u64) {
  ///     // Handle the scheduled event.
  ///     assert_eq!(event_id, 123);
  ///   }
  /// }
  /// ```
  fn new_scheduler(&self) -> impl EnvoyListenerFilterScheduler + 'static;

  /// Get the index of the current worker thread.
  fn get_worker_index(&self) -> u32;

  /// Close the socket immediately. If details is provided, the termination reason is set on the
  /// connection's stream info before closing.
  fn close_socket<'a>(&mut self, details: Option<&'a str>);

  /// Write data directly to the raw socket.
  /// This is useful for protocol negotiation at the listener filter level,
  /// such as writing SSL support responses in Postgres or MySQL handshake packets.
  /// Returns the number of bytes written, or -1 if the write failed.
  fn write_to_socket(&mut self, data: &[u8]) -> i64;
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy listener filter on the worker thread where the listener filter is running.
///
/// The scheduler is created by the [`EnvoyListenerFilter::new_scheduler`] method. When calling
/// [`EnvoyListenerFilterScheduler::commit`] with an event id, eventually
/// [`ListenerFilter::on_scheduled`] is called with the same event id on the worker thread where the
/// listener filter is running, IF the filter is still alive by the time the event is committed.
///
/// Since this is primarily designed to be used from a different thread than the one
/// where the [`ListenerFilter`] instance was created, it is marked as `Send` so that
/// the [`Box<dyn EnvoyListenerFilterScheduler>`] can be sent across threads.
///
/// It is also safe to be called concurrently, so it is marked as `Sync` as well.
#[automock]
pub trait EnvoyListenerFilterScheduler: Send + Sync {
  /// Commit the scheduled event to the worker thread where [`ListenerFilter`] is running.
  ///
  /// It accepts an `event_id` which can be used to distinguish different events
  /// scheduled by the same filter. The `event_id` can be any value.
  ///
  /// Once this is called, [`ListenerFilter::on_scheduled`] will be called with
  /// the same `event_id` on the worker thread where the filter is running IF
  /// by the time the event is committed, the filter is still alive.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyListenerFilterScheduler`] trait with the given raw pointer to the
/// Envoy listener filter scheduler object.
struct EnvoyListenerFilterSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_listener_filter_scheduler_module_ptr,
}

unsafe impl Send for EnvoyListenerFilterSchedulerImpl {}
unsafe impl Sync for EnvoyListenerFilterSchedulerImpl {}

impl Drop for EnvoyListenerFilterSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyListenerFilterScheduler for EnvoyListenerFilterSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

// Box<dyn EnvoyListenerFilterScheduler> is returned by mockall, so we need to implement
// EnvoyListenerFilterScheduler for it as well.
impl EnvoyListenerFilterScheduler for Box<dyn EnvoyListenerFilterScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy listener filter config on the main thread.
#[automock]
pub trait EnvoyListenerFilterConfigScheduler: Send + Sync {
  /// Commit the scheduled event to the main thread.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyListenerFilterConfigScheduler`] trait.
struct EnvoyListenerFilterConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr,
}

unsafe impl Send for EnvoyListenerFilterConfigSchedulerImpl {}
unsafe impl Sync for EnvoyListenerFilterConfigSchedulerImpl {}

impl Drop for EnvoyListenerFilterConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyListenerFilterConfigScheduler for EnvoyListenerFilterConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
        self.raw_ptr,
        event_id,
      );
    }
  }
}

// Box<dyn EnvoyListenerFilterConfigScheduler> is returned by mockall, so we need to implement
// EnvoyListenerFilterConfigScheduler for it as well.
impl EnvoyListenerFilterConfigScheduler for Box<dyn EnvoyListenerFilterConfigScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// The implementation of [`EnvoyListenerFilterConfig`] for the Envoy listener filter
/// configuration.
pub struct EnvoyListenerFilterConfigImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
}

impl EnvoyListenerFilterConfigImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyListenerFilterConfig for EnvoyListenerFilterConfigImpl {
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn new_config_scheduler(&self) -> impl EnvoyListenerFilterConfigScheduler + 'static {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_listener_filter_config_scheduler_new(self.raw);
      EnvoyListenerFilterConfigSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }
}

/// The implementation of [`EnvoyListenerFilter`] for the Envoy listener filter.
pub struct EnvoyListenerFilterImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
}

impl EnvoyListenerFilterImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyListenerFilter for EnvoyListenerFilterImpl {
  fn get_buffer_chunk(&self) -> Option<EnvoyBuffer<'_>> {
    let mut chunk = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
        self.raw,
        &mut chunk as *mut _ as *mut _,
      )
    };
    if success && !chunk.ptr.is_null() && chunk.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(chunk.ptr as *const _, chunk.length) })
    } else {
      None
    }
  }

  fn drain_buffer(&mut self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_drain_buffer(self.raw, length);
    }
  }

  fn get_remote_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_remote_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_local_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_local_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_direct_remote_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_direct_local_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_original_dst(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_original_dst(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_address_type(&self) -> abi::envoy_dynamic_module_type_address_type {
    unsafe { abi::envoy_dynamic_module_callback_listener_filter_get_address_type(self.raw) }
  }

  fn is_local_address_restored(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_is_local_address_restored(self.raw)
    }
  }

  fn use_original_dst(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_use_original_dst(self.raw, true);
    }
  }

  fn set_downstream_transport_failure_reason(&mut self, reason: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
        self.raw,
        str_to_module_buffer(reason),
      );
    }
  }

  fn get_connection_start_time_ms(&self) -> u64 {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(self.raw)
    }
  }

  fn get_dynamic_metadata_string(&self, namespace: &str, key: &str) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let value_str =
        unsafe { crate::ffi_helpers::str_lossy_from_raw(result.ptr as *const u8, result.length) };
      Some(value_str.into_owned())
    } else {
      None
    }
  }

  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      )
    }
  }

  fn get_dynamic_metadata_number(&self, namespace: &str, key: &str) -> Option<f64> {
    let mut result: f64 = 0.0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut result,
      )
    };
    if success {
      Some(result)
    } else {
      None
    }
  }

  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn max_read_bytes(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_listener_filter_max_read_bytes(self.raw) }
  }

  fn get_requested_server_name(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
        self.raw,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_detected_transport_protocol(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
        self.raw,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_requested_application_protocols(&self) -> Vec<EnvoyBuffer<'_>> {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
        self.raw,
      )
    };
    if size == 0 {
      return Vec::new();
    }

    let mut protocol_buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
        self.raw,
        protocol_buffers.as_mut_ptr(),
      )
    };
    if !ok {
      return Vec::new();
    }

    protocol_buffers
      .iter()
      .take(size)
      .map(|buf| {
        if !buf.ptr.is_null() && buf.length > 0 {
          unsafe { EnvoyBuffer::new_from_raw(buf.ptr as *const _, buf.length) }
        } else {
          EnvoyBuffer::default()
        }
      })
      .collect()
  }

  fn get_ja3_hash(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
        self.raw,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_ja4_hash(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
        self.raw,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn is_ssl(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_listener_filter_is_ssl(self.raw) }
  }

  fn get_ssl_uri_sans(&self) -> Vec<EnvoyBuffer<'_>> {
    let size =
      unsafe { abi::envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(self.raw) };
    if size == 0 {
      return Vec::new();
    }

    let mut sans_buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
        self.raw,
        sans_buffers.as_mut_ptr(),
      )
    };
    if !ok {
      return Vec::new();
    }

    sans_buffers
      .iter()
      .take(size)
      .map(|buf| {
        if !buf.ptr.is_null() && buf.length > 0 {
          unsafe { EnvoyBuffer::new_from_raw(buf.ptr as *const _, buf.length) }
        } else {
          EnvoyBuffer::default()
        }
      })
      .collect()
  }

  fn get_ssl_dns_sans(&self) -> Vec<EnvoyBuffer<'_>> {
    let size =
      unsafe { abi::envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(self.raw) };
    if size == 0 {
      return Vec::new();
    }

    let mut sans_buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
        self.raw,
        sans_buffers.as_mut_ptr(),
      )
    };
    if !ok {
      return Vec::new();
    }

    sans_buffers
      .iter()
      .take(size)
      .map(|buf| {
        if !buf.ptr.is_null() && buf.length > 0 {
          unsafe { EnvoyBuffer::new_from_raw(buf.ptr as *const _, buf.length) }
        } else {
          EnvoyBuffer::default()
        }
      })
      .collect()
  }

  fn get_ssl_subject(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
        self.raw,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_socket_fd(&self) -> i64 {
    unsafe { abi::envoy_dynamic_module_callback_listener_filter_get_socket_fd(self.raw) }
  }

  fn set_socket_option_int(&mut self, level: i64, name: i64, value: i64) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
        self.raw, level, name, value,
      )
    }
  }

  fn set_socket_option_bytes(&mut self, level: i64, name: i64, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
        self.raw,
        level,
        name,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: value.as_ptr() as *const _,
          length: value.len(),
        },
      )
    }
  }

  fn get_socket_option_int(&self, level: i64, name: i64) -> Option<i64> {
    let mut value: i64 = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
        self.raw, level, name, &mut value,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn get_socket_option_bytes(&self, level: i64, name: i64, max_size: usize) -> Option<Vec<u8>> {
    let mut buffer: Vec<u8> = vec![0; max_size];
    let mut actual_size: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
        self.raw,
        level,
        name,
        buffer.as_mut_ptr() as *mut _,
        max_size,
        &mut actual_size,
      )
    };
    if success {
      buffer.truncate(actual_size);
      Some(buffer)
    } else {
      None
    }
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_increment_counter(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res =
      unsafe { abi::envoy_dynamic_module_callback_listener_filter_set_gauge(self.raw, id, value) };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_increment_gauge(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_decrement_gauge(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_record_histogram_value(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn send_http_callout<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: Vec<(&'a str, &'a [u8])>,
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let mut callout_id: u64 = 0;

    // Convert headers to module HTTP headers.
    let module_headers: Vec<abi::envoy_dynamic_module_type_module_http_header> = headers
      .iter()
      .map(|(k, v)| abi::envoy_dynamic_module_type_module_http_header {
        key_ptr: k.as_ptr() as *const _,
        key_length: k.len(),
        value_ptr: v.as_ptr() as *const _,
        value_length: v.len(),
      })
      .collect();

    let body_buffer = match body {
      Some(b) => abi::envoy_dynamic_module_type_module_buffer {
        ptr: b.as_ptr() as *const _,
        length: b.len(),
      },
      None => abi::envoy_dynamic_module_type_module_buffer {
        ptr: std::ptr::null(),
        length: 0,
      },
    };

    let result = unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_http_callout(
        self.raw,
        &mut callout_id,
        str_to_module_buffer(cluster_name),
        module_headers.as_ptr() as *mut _,
        module_headers.len(),
        body_buffer,
        timeout_milliseconds,
      )
    };

    (result, callout_id)
  }

  fn new_scheduler(&self) -> impl EnvoyListenerFilterScheduler + 'static {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_listener_filter_scheduler_new(self.raw);
      EnvoyListenerFilterSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }

  fn get_worker_index(&self) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_listener_filter_get_worker_index(self.raw) }
  }

  fn close_socket(&mut self, details: Option<&str>) {
    unsafe {
      abi::envoy_dynamic_module_callback_listener_filter_close_socket(
        self.raw,
        details
          .map(str_to_module_buffer)
          .unwrap_or(abi::envoy_dynamic_module_type_module_buffer {
            ptr: std::ptr::null_mut(),
            length: 0,
          }),
      );
    }
  }

  fn write_to_socket(&mut self, data: &[u8]) -> i64 {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_listener_filter_write_to_socket(self.raw, buffer)
    }
  }
}

// Listener Filter Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_config_new(
  envoy_filter_config_ptr: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_listener_filter_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter_config = EnvoyListenerFilterConfigImpl::new(envoy_filter_config_ptr);
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    init_listener_filter_config(
      &mut envoy_filter_config,
      name_str.as_ref(),
      config_slice,
      NEW_LISTENER_FILTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_LISTENER_FILTER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_config_new", panic);
    std::ptr::null()
  })
}

pub(crate) fn init_listener_filter_config<
  EC: EnvoyListenerFilterConfig,
  ELF: EnvoyListenerFilter,
>(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
  new_listener_filter_config_fn: &NewListenerFilterConfigFunction<EC, ELF>,
) -> abi::envoy_dynamic_module_type_listener_filter_config_module_ptr {
  let listener_filter_config = new_listener_filter_config_fn(envoy_filter_config, name, config);
  match listener_filter_config {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_listener_filter_config_destroy(
  filter_config_ptr: abi::envoy_dynamic_module_type_listener_filter_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(
      filter_config_ptr,
      ListenerFilterConfig<EnvoyListenerFilterImpl>
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_listener_filter_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_listener_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_listener_filter_config_module_ptr,
  envoy_filter_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_listener_filter_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter = EnvoyListenerFilterImpl::new(envoy_filter_ptr);
    let filter_config = {
      let raw =
        filter_config_ptr as *const *const dyn ListenerFilterConfig<EnvoyListenerFilterImpl>;
      &**raw
    };
    envoy_dynamic_module_on_listener_filter_new_impl(&mut envoy_filter, filter_config)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_new", panic);
    std::ptr::null()
  })
}

pub(crate) fn envoy_dynamic_module_on_listener_filter_new_impl(
  envoy_filter: &mut EnvoyListenerFilterImpl,
  filter_config: &dyn ListenerFilterConfig<EnvoyListenerFilterImpl>,
) -> abi::envoy_dynamic_module_type_listener_filter_module_ptr {
  let filter = filter_config.new_listener_filter(envoy_filter);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_on_accept(
  envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_accept(&mut EnvoyListenerFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_on_accept", panic);
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_on_data(
  envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_data(&mut EnvoyListenerFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_on_data", panic);
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_on_close(
  envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_close(&mut EnvoyListenerFilterImpl::new(envoy_ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_on_close", panic);
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let _ =
      unsafe { Box::from_raw(filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>) };
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_destroy", panic);
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_scheduled(
  envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_scheduled(&mut EnvoyListenerFilterImpl::new(envoy_ptr), event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_listener_filter_scheduled", panic);
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_listener_filter_config_scheduled(
  _filter_config_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
  filter_config_module_ptr: abi::envoy_dynamic_module_type_listener_filter_config_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter_config =
      filter_config_module_ptr as *const *const dyn ListenerFilterConfig<EnvoyListenerFilterImpl>;
    let filter_config = unsafe { &**filter_config };
    filter_config.on_config_scheduled(event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_listener_filter_config_scheduled",
      panic,
    );
  });
}

/// # Safety
///
/// Caller must ensure `filter_ptr`, `headers`, and `body_chunks` point to valid memory for the
/// provided sizes, and that the pointed-to data lives for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_listener_filter_http_callout_done(
  envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_listener_filter_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn ListenerFilter<EnvoyListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };

    // Convert headers to Vec<(EnvoyBuffer, EnvoyBuffer)>.
    let headers_slice =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(headers, headers_size) };
    let header_vec: Vec<(EnvoyBuffer, EnvoyBuffer)> = headers_slice
      .iter()
      .map(|h| {
        (
          unsafe { EnvoyBuffer::new_from_raw(h.key_ptr as *const _, h.key_length) },
          unsafe { EnvoyBuffer::new_from_raw(h.value_ptr as *const _, h.value_length) },
        )
      })
      .collect();

    // Convert body chunks to Vec<EnvoyBuffer>.
    let chunks_slice =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(body_chunks, body_chunks_size) };
    let body_vec: Vec<EnvoyBuffer> = chunks_slice
      .iter()
      .map(|c| unsafe { EnvoyBuffer::new_from_raw(c.ptr as *const _, c.length) })
      .collect();

    filter.on_http_callout_done(
      &mut EnvoyListenerFilterImpl::new(envoy_ptr),
      callout_id,
      result,
      header_vec,
      body_vec,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_listener_filter_http_callout_done",
      panic,
    );
  });
}
