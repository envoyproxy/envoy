use crate::abi::envoy_dynamic_module_type_metrics_result;
use crate::buffer::EnvoyBuffer;
use crate::{
  abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, str_to_module_buffer, wrap_into_c_void_ptr,
  ClusterHostCount, EnvoyCounterId, EnvoyGaugeId, EnvoyHistogramId, NewNetworkFilterConfigFunction,
  NEW_NETWORK_FILTER_CONFIG_FUNCTION,
};
use mockall::*;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// The trait that represents the Envoy network filter configuration.
/// This is used in [`NewNetworkFilterConfigFunction`] to pass the Envoy filter configuration
/// to the dynamic module.
#[automock]
pub trait EnvoyNetworkFilterConfig {
  /// Define a new counter scoped to this filter config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this filter config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this filter config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result>;

  /// Create a new implementation of the [`EnvoyNetworkFilterConfigScheduler`] trait.
  ///
  /// This allows scheduling events to be delivered on the main thread.
  fn new_config_scheduler(&self) -> impl EnvoyNetworkFilterConfigScheduler + 'static;
}

/// The trait that represents the configuration for an Envoy network filter configuration.
/// This has one to one mapping with the [`EnvoyNetworkFilterConfig`] object.
///
/// The object is created when the corresponding Envoy network filter config is created, and it is
/// dropped when the corresponding Envoy network filter config is destroyed. Therefore, the
/// implementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
///
/// Implementations must also be `Sync` since they are accessed from worker threads.
pub trait NetworkFilterConfig<ENF: EnvoyNetworkFilter>: Sync {
  /// This is called from a worker thread when a new TCP connection is established.
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>>;

  /// This is called when the new event is scheduled via the
  /// [`EnvoyNetworkFilterConfigScheduler::commit`] for this [`NetworkFilterConfig`].
  ///
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyNetworkFilterConfigScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyNetworkFilterConfig::new_config_scheduler`] for more details on how to use this.
  fn on_config_scheduled(&self, _event_id: u64) {}
}

/// The trait that corresponds to an Envoy network filter for each TCP connection
/// created via the [`NetworkFilterConfig::new_network_filter`] method.
///
/// All the event hooks are called on the same thread as the one that the [`NetworkFilter`] is
/// created via the [`NetworkFilterConfig::new_network_filter`] method.
pub trait NetworkFilter<ENF: EnvoyNetworkFilter> {
  /// This is called when a new TCP connection is established.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_network_filter_data_status`] to
  /// indicate the status of the new connection processing.
  fn on_new_connection(
    &mut self,
    _envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  /// This is called when data is read from the connection (downstream -> upstream direction).
  ///
  /// The `data_length` is the total length of the read data buffer.
  /// The `end_stream` indicates whether this is the last data (half-close from downstream).
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_network_filter_data_status`] to
  /// indicate the status of the read data processing.
  fn on_read(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  /// This is called when data is to be written to the connection (upstream -> downstream
  /// direction).
  ///
  /// The `data_length` is the total length of the write data buffer.
  /// The `end_stream` indicates whether this is the last data.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_network_filter_data_status`] to
  /// indicate the status of the write data processing.
  fn on_write(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  /// This is called when a connection event occurs.
  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
  }

  /// This is called when an HTTP callout response is received.
  ///
  /// @param _callout_id is the ID of the callout returned by send_http_callout.
  /// @param _result is the result of the callout.
  /// @param _headers is the headers of the response. Empty if the callout failed.
  /// @param _body_chunks is the body chunks of the response. Empty if the callout failed.
  fn on_http_callout_done(
    &mut self,
    _envoy_filter: &mut ENF,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _headers: Vec<(EnvoyBuffer, EnvoyBuffer)>,
    _body_chunks: Vec<EnvoyBuffer>,
  ) {
  }

  /// This is called when the network filter is destroyed for each TCP connection.
  fn on_destroy(&mut self, _envoy_filter: &mut ENF) {}

  /// This is called when an event is scheduled via the [`EnvoyNetworkFilterScheduler::commit`]
  /// for this [`NetworkFilter`].
  ///
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyNetworkFilterScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyNetworkFilter::new_scheduler`] for more details on how to use this.
  fn on_scheduled(&mut self, _envoy_filter: &mut ENF, _event_id: u64) {}

  /// This is called when the write buffer for the connection goes over its high watermark.
  /// This can be used to implement flow control by disabling reads when the write buffer is full.
  ///
  /// A typical implementation would call `envoy_filter.read_disable(true)` to stop reading
  /// from the downstream connection until the write buffer drains.
  fn on_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut ENF) {}

  /// This is called when the write buffer for the connection goes from over its high watermark
  /// to under its low watermark. This can be used to re-enable reads after flow control was
  /// applied.
  ///
  /// A typical implementation would call `envoy_filter.read_disable(false)` to resume reading
  /// from the downstream connection.
  fn on_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut ENF) {}
}

/// The trait that represents the Envoy network filter.
/// This is used in [`NetworkFilter`] to interact with the underlying Envoy network filter object.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyNetworkFilter {
  /// Get the read buffer chunks. This is valid after the first on_read callback for the lifetime
  /// of the connection.
  fn get_read_buffer_chunks<'a>(&'a mut self) -> (Vec<EnvoyBuffer<'a>>, usize);

  /// Get the write buffer chunks. This is valid after the first on_write callback for the lifetime
  /// of the connection.
  fn get_write_buffer_chunks<'a>(&'a mut self) -> (Vec<EnvoyBuffer<'a>>, usize);

  /// Drain bytes from the beginning of the read buffer.
  fn drain_read_buffer(&mut self, length: usize);

  /// Drain bytes from the beginning of the write buffer.
  fn drain_write_buffer(&mut self, length: usize);

  /// Prepend data to the beginning of the read buffer.
  fn prepend_read_buffer(&mut self, data: &[u8]);

  /// Append data to the end of the read buffer.
  fn append_read_buffer(&mut self, data: &[u8]);

  /// Prepend data to the beginning of the write buffer.
  fn prepend_write_buffer(&mut self, data: &[u8]);

  /// Append data to the end of the write buffer.
  fn append_write_buffer(&mut self, data: &[u8]);

  /// Write data directly to the connection (downstream).
  fn write(&mut self, data: &[u8], end_stream: bool);

  /// Inject data into the read filter chain (after this filter).
  fn inject_read_data(&mut self, data: &[u8], end_stream: bool);

  /// Inject data into the write filter chain (after this filter).
  fn inject_write_data(&mut self, data: &[u8], end_stream: bool);

  /// Continue reading after returning StopIteration.
  fn continue_reading(&mut self);

  /// Close the connection.
  fn close(&mut self, close_type: abi::envoy_dynamic_module_type_network_connection_close_type);

  /// Get the unique connection ID.
  fn get_connection_id(&self) -> u64;

  /// Get the remote (client) address and port.
  fn get_remote_address(&self) -> (String, u32);

  /// Get the local address and port.
  fn get_local_address(&self) -> (String, u32);

  /// Check if the connection uses SSL/TLS.
  fn is_ssl(&self) -> bool;

  /// Disable or enable connection close handling for this filter.
  fn disable_close(&mut self, disabled: bool);

  /// Close the connection with termination details.
  fn close_with_details(
    &mut self,
    close_type: abi::envoy_dynamic_module_type_network_connection_close_type,
    details: &str,
  );

  /// Get the requested server name (SNI).
  /// Returns None if SNI is not available.
  fn get_requested_server_name<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get the direct remote (client) address and port without considering proxy protocol.
  /// Returns None if the address is not available or not an IP address.
  fn get_direct_remote_address<'a>(&'a self) -> Option<(EnvoyBuffer<'a>, u32)>;

  /// Get the SSL URI SANs from the peer certificate.
  /// Returns an empty vector if the connection is not SSL or no URI SANs are present.
  fn get_ssl_uri_sans<'a>(&'a self) -> Vec<EnvoyBuffer<'a>>;

  /// Get the SSL DNS SANs from the peer certificate.
  /// Returns an empty vector if the connection is not SSL or no DNS SANs are present.
  fn get_ssl_dns_sans<'a>(&'a self) -> Vec<EnvoyBuffer<'a>>;

  /// Get the SSL subject from the peer certificate.
  /// Returns None if the connection is not SSL or subject is not available.
  fn get_ssl_subject<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Set the filter state with the given key and byte value.
  /// Returns true if the operation is successful.
  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool;

  /// Get the filter state bytes with the given key.
  /// Returns None if the filter state is not found.
  fn get_filter_state_bytes<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>>;

  /// Set a typed filter state value with the given key. The value is deserialized by a
  /// registered `StreamInfo::FilterState::ObjectFactory` on the Envoy side. This is useful for
  /// setting filter state objects that other Envoy filters expect to read as specific C++ types
  /// (e.g., `PerConnectionCluster` used by TCP Proxy).
  ///
  /// Returns true if the operation is successful. This can fail if no ObjectFactory is registered
  /// for the key, if deserialization fails, or if the key already exists and is read-only.
  fn set_filter_state_typed(&mut self, key: &[u8], value: &[u8]) -> bool;

  /// Get the serialized value of a typed filter state object with the given key. The object must
  /// support `serializeAsString()` on the Envoy side.
  ///
  /// Returns None if the key does not exist, the object does not support serialization, or the
  /// filter state is not accessible.
  fn get_filter_state_typed<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>>;

  /// Set the string-typed dynamic metadata value with the given namespace and key value.
  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str);

  /// Get the string-typed dynamic metadata value with the given namespace and key value.
  /// Returns None if the metadata is not found or is the wrong type.
  fn get_dynamic_metadata_string(&self, namespace: &str, key: &str) -> Option<String>;

  /// Set the number-typed dynamic metadata value with the given namespace and key value.
  /// Returns true if the operation is successful.
  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64);

  /// Get the number-typed dynamic metadata value with the given namespace and key value.
  /// Returns None if the metadata is not found or is the wrong type.
  fn get_dynamic_metadata_number(&self, namespace: &str, key: &str) -> Option<f64>;

  /// Set the bool-typed dynamic metadata value with the given namespace and key value.
  fn set_dynamic_metadata_bool(&mut self, namespace: &str, key: &str, value: bool);

  /// Get the bool-typed dynamic metadata value with the given namespace and key value.
  /// Returns None if the metadata is not found or is the wrong type.
  fn get_dynamic_metadata_bool(&self, namespace: &str, key: &str) -> Option<bool>;

  /// Set an integer socket option with the given level, name, and state.
  fn set_socket_option_int(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    value: i64,
  );

  /// Set a bytes socket option with the given level, name, and state.
  fn set_socket_option_bytes(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    value: &[u8],
  );

  /// Get an integer socket option value.
  fn get_socket_option_int(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
  ) -> Option<i64>;

  /// Get a bytes socket option value.
  fn get_socket_option_bytes(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
  ) -> Option<Vec<u8>>;

  /// List all socket options stored on the connection.
  fn get_socket_options(&self) -> Vec<SocketOption>;

  /// Send an HTTP callout to the given cluster with the given headers and body.
  /// Multiple callouts can be made from the same filter. Different callouts can be
  /// distinguished by the returned callout id.
  ///
  /// Headers must contain the `:method`, ":path", and `host` headers.
  ///
  /// This returns the status and callout id of the callout. The id is used to
  /// distinguish different callouts made from the same filter and is generated by Envoy.
  /// The meaning of the status is:
  ///
  ///   * Success: The callout was sent successfully.
  ///   * MissingRequiredHeaders: One of the required headers is missing: `:method`, `:path`, or
  ///     `host`.
  ///   * ClusterNotFound: The cluster with the given name was not found.
  ///   * DuplicateCalloutId: The callout ID is already in use.
  ///   * CannotCreateRequest: The request could not be created. This happens when, for example,
  ///     there's no healthy upstream host in the cluster.
  ///
  /// The callout result will be delivered to the [`NetworkFilter::on_http_callout_done`] method.
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

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Retrieve the host counts for a cluster by name at the given priority level.
  /// Returns None if the cluster is not found or the priority level does not exist.
  ///
  /// This is useful for implementing scale-to-zero logic or custom load balancing decisions.
  fn get_cluster_host_count(&self, cluster_name: &str, priority: u32) -> Option<ClusterHostCount>;

  /// Get the upstream host address and port if an upstream host is selected.
  /// Returns None if no upstream host is set or the address is not an IP.
  fn get_upstream_host_address(&self) -> Option<(String, u32)>;

  /// Get the upstream host hostname if an upstream host is selected.
  /// Returns None if no upstream host is set or hostname is empty.
  fn get_upstream_host_hostname(&self) -> Option<String>;

  /// Get the upstream host cluster name if an upstream host is selected.
  /// Returns None if no upstream host is set.
  fn get_upstream_host_cluster(&self) -> Option<String>;

  /// Check if an upstream host has been selected for this connection.
  fn has_upstream_host(&self) -> bool;

  /// Signal to the filter manager to enable secure transport mode in upstream connection.
  /// This is done when upstream connection's transport socket is of startTLS type.
  /// Returns true if the upstream transport was successfully converted to secure mode.
  fn start_upstream_secure_transport(&mut self) -> bool;

  /// Get the current state of the connection (Open, Closing, or Closed).
  fn get_connection_state(&self) -> abi::envoy_dynamic_module_type_network_connection_state;

  /// Disable or enable reading from the connection. This is the primary mechanism for
  /// implementing back-pressure in TCP filters.
  ///
  /// When reads are disabled, no more data will be read from the socket. When re-enabled,
  /// if there is data in the input buffer, it will be re-dispatched through the filter chain.
  ///
  /// Note that this function reference counts calls. For example:
  /// ```text
  /// read_disable(true);  // Disables reading
  /// read_disable(true);  // Notes the connection is blocked by two sources
  /// read_disable(false); // Notes the connection is blocked by one source
  /// read_disable(false); // Marks the connection as unblocked, so resumes reading
  /// ```
  fn read_disable(
    &mut self,
    disable: bool,
  ) -> abi::envoy_dynamic_module_type_network_read_disable_status;

  /// Check if reading is currently enabled on the connection.
  fn read_enabled(&self) -> bool;

  /// Check if half-close semantics are enabled on this connection.
  /// When half-close is enabled, reading a remote half-close will not fully close the connection.
  fn is_half_close_enabled(&self) -> bool;

  /// Enable or disable half-close semantics on the connection.
  /// When half-close is enabled, reading a remote half-close will not fully close the connection,
  /// allowing the filter to continue writing data.
  fn enable_half_close(&mut self, enabled: bool);

  /// Get the current buffer limit set on the connection.
  fn get_buffer_limit(&self) -> u32;

  /// Set a soft limit on the size of buffers for the connection.
  ///
  /// For the read buffer, this limits the bytes read prior to flushing to further stages in the
  /// processing pipeline. For the write buffer, it sets watermarks. When enough data is buffered,
  /// [`NetworkFilter::on_above_write_buffer_high_watermark`] is called. When enough data is
  /// drained, [`NetworkFilter::on_below_write_buffer_low_watermark`] is called.
  fn set_buffer_limits(&mut self, limit: u32);

  /// Check if the connection is currently above the high watermark.
  fn above_high_watermark(&self) -> bool;

  /// Create a new implementation of the [`EnvoyNetworkFilterScheduler`] trait.
  ///
  /// ## Example Usage
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  /// use std::thread;
  ///
  /// struct TestFilter;
  /// impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestFilter {
  ///   fn on_new_connection(
  ///     &mut self,
  ///     envoy_filter: &mut ENF,
  ///   ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
  ///     let scheduler = envoy_filter.new_scheduler();
  ///     let _ = std::thread::spawn(move || {
  ///       // Do some work in a separate thread.
  ///       // ...
  ///       // Then schedule the event to continue processing.
  ///       scheduler.commit(123);
  ///     });
  ///     abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
  ///   }
  ///
  ///   fn on_scheduled(&mut self, _envoy_filter: &mut ENF, event_id: u64) {
  ///     // Handle the scheduled event.
  ///     assert_eq!(event_id, 123);
  ///   }
  /// }
  /// ```
  fn new_scheduler(&self) -> impl EnvoyNetworkFilterScheduler + 'static;

  /// Get the index of the current worker thread.
  fn get_worker_index(&self) -> u32;
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy network filter on the worker thread where the network filter is running.
///
/// The scheduler is created by the [`EnvoyNetworkFilter::new_scheduler`] method. When calling
/// [`EnvoyNetworkFilterScheduler::commit`] with an event id, eventually
/// [`NetworkFilter::on_scheduled`] is called with the same event id on the worker thread where the
/// network filter is running, IF the filter is still alive by the time the event is committed.
///
/// Since this is primarily designed to be used from a different thread than the one
/// where the [`NetworkFilter`] instance was created, it is marked as `Send` so that
/// the [`Box<dyn EnvoyNetworkFilterScheduler>`] can be sent across threads.
///
/// It is also safe to be called concurrently, so it is marked as `Sync` as well.
#[automock]
pub trait EnvoyNetworkFilterScheduler: Send + Sync {
  /// Commit the scheduled event to the worker thread where [`NetworkFilter`] is running.
  ///
  /// It accepts an `event_id` which can be used to distinguish different events
  /// scheduled by the same filter. The `event_id` can be any value.
  ///
  /// Once this is called, [`NetworkFilter::on_scheduled`] will be called with
  /// the same `event_id` on the worker thread where the filter is running IF
  /// by the time the event is committed, the filter is still alive.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyNetworkFilterScheduler`] trait with the given raw pointer to the
/// Envoy network filter scheduler object.
struct EnvoyNetworkFilterSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_network_filter_scheduler_module_ptr,
}

unsafe impl Send for EnvoyNetworkFilterSchedulerImpl {}
unsafe impl Sync for EnvoyNetworkFilterSchedulerImpl {}

impl Drop for EnvoyNetworkFilterSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyNetworkFilterScheduler for EnvoyNetworkFilterSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

// Box<dyn EnvoyNetworkFilterScheduler> is returned by mockall, so we need to implement
// EnvoyNetworkFilterScheduler for it as well.
impl EnvoyNetworkFilterScheduler for Box<dyn EnvoyNetworkFilterScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy network filter config on the main thread.
#[automock]
pub trait EnvoyNetworkFilterConfigScheduler: Send + Sync {
  /// Commit the scheduled event to the main thread.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyNetworkFilterConfigScheduler`] trait.
struct EnvoyNetworkFilterConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr,
}

unsafe impl Send for EnvoyNetworkFilterConfigSchedulerImpl {}
unsafe impl Sync for EnvoyNetworkFilterConfigSchedulerImpl {}

impl Drop for EnvoyNetworkFilterConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyNetworkFilterConfigScheduler for EnvoyNetworkFilterConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
        self.raw_ptr,
        event_id,
      );
    }
  }
}

// Box<dyn EnvoyNetworkFilterConfigScheduler> is returned by mockall, so we need to implement
// EnvoyNetworkFilterConfigScheduler for it as well.
impl EnvoyNetworkFilterConfigScheduler for Box<dyn EnvoyNetworkFilterConfigScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

pub enum SocketOptionValue {
  Int(i64),
  Bytes(Vec<u8>),
}

pub struct SocketOption {
  pub level: i64,
  pub name: i64,
  pub state: abi::envoy_dynamic_module_type_socket_option_state,
  pub value: SocketOptionValue,
}

/// The implementation of [`EnvoyNetworkFilterConfig`] for the Envoy network filter configuration.
pub struct EnvoyNetworkFilterConfigImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_network_filter_config_envoy_ptr,
}

impl EnvoyNetworkFilterConfigImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_network_filter_config_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyNetworkFilterConfig for EnvoyNetworkFilterConfigImpl {
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_network_filter_config_define_counter(
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
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_network_filter_config_define_gauge(
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
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_network_filter_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn new_config_scheduler(&self) -> impl EnvoyNetworkFilterConfigScheduler + 'static {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_network_filter_config_scheduler_new(self.raw);
      EnvoyNetworkFilterConfigSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }
}

/// The implementation of [`EnvoyNetworkFilter`] for the Envoy network filter.
pub struct EnvoyNetworkFilterImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
}

impl EnvoyNetworkFilterImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_network_filter_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyNetworkFilter for EnvoyNetworkFilterImpl {
  fn get_read_buffer_chunks(&mut self) -> (Vec<EnvoyBuffer<'_>>, usize) {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(self.raw)
    };
    if size == 0 {
      return (Vec::new(), 0);
    }

    let total_length =
      unsafe { abi::envoy_dynamic_module_callback_network_filter_get_read_buffer_size(self.raw) };
    if total_length == 0 {
      return (Vec::new(), 0);
    }

    let mut buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null_mut(),
        length: 0,
      };
      size
    ];
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
        self.raw,
        buffers.as_mut_ptr(),
      )
    };
    let envoy_buffers: Vec<EnvoyBuffer> = buffers
      .iter()
      .map(|s| unsafe { EnvoyBuffer::new_from_raw(s.ptr as *const _, s.length) })
      .collect();
    (envoy_buffers, total_length)
  }

  fn get_write_buffer_chunks(&mut self) -> (Vec<EnvoyBuffer<'_>>, usize) {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(self.raw)
    };
    if size == 0 {
      return (Vec::new(), 0);
    }

    let total_length =
      unsafe { abi::envoy_dynamic_module_callback_network_filter_get_write_buffer_size(self.raw) };
    if total_length == 0 {
      return (Vec::new(), 0);
    }

    let mut buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null_mut(),
        length: 0,
      };
      size
    ];
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
        self.raw,
        buffers.as_mut_ptr(),
      )
    };
    let envoy_buffers: Vec<EnvoyBuffer> = buffers
      .iter()
      .map(|s| unsafe { EnvoyBuffer::new_from_raw(s.ptr as *const _, s.length) })
      .collect();
    (envoy_buffers, total_length)
  }

  fn drain_read_buffer(&mut self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_drain_read_buffer(self.raw, length);
    }
  }

  fn drain_write_buffer(&mut self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_drain_write_buffer(self.raw, length);
    }
  }

  fn prepend_read_buffer(&mut self, data: &[u8]) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_prepend_read_buffer(self.raw, buffer);
    }
  }

  fn append_read_buffer(&mut self, data: &[u8]) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_append_read_buffer(self.raw, buffer);
    }
  }

  fn prepend_write_buffer(&mut self, data: &[u8]) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_prepend_write_buffer(self.raw, buffer);
    }
  }

  fn append_write_buffer(&mut self, data: &[u8]) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_append_write_buffer(self.raw, buffer);
    }
  }

  fn write(&mut self, data: &[u8], end_stream: bool) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_write(self.raw, buffer, end_stream);
    }
  }

  fn inject_read_data(&mut self, data: &[u8], end_stream: bool) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_inject_read_data(
        self.raw, buffer, end_stream,
      );
    }
  }

  fn inject_write_data(&mut self, data: &[u8], end_stream: bool) {
    unsafe {
      let buffer = abi::envoy_dynamic_module_type_module_buffer {
        ptr: data.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
        length: data.len(),
      };
      abi::envoy_dynamic_module_callback_network_filter_inject_write_data(
        self.raw, buffer, end_stream,
      );
    }
  }

  fn continue_reading(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_continue_reading(self.raw);
    }
  }

  fn close(&mut self, close_type: abi::envoy_dynamic_module_type_network_connection_close_type) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_close(self.raw, close_type);
    }
  }

  fn get_connection_id(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_get_connection_id(self.raw) }
  }

  fn get_remote_address(&self) -> (String, u32) {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_remote_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return (String::new(), 0);
    }
    let address =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    (address.into_owned(), port)
  }

  fn get_local_address(&self) -> (String, u32) {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_local_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return (String::new(), 0);
    }
    let address =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    (address.into_owned(), port)
  }

  fn is_ssl(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_is_ssl(self.raw) }
  }

  fn disable_close(&mut self, disabled: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_disable_close(self.raw, disabled);
    }
  }

  fn close_with_details(
    &mut self,
    close_type: abi::envoy_dynamic_module_type_network_connection_close_type,
    details: &str,
  ) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_close_with_details(
        self.raw,
        close_type,
        str_to_module_buffer(details),
      );
    }
  }

  fn get_requested_server_name(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_requested_server_name(
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

  fn get_direct_remote_address(&self) -> Option<(EnvoyBuffer<'_>, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    Some((
      unsafe { EnvoyBuffer::new_from_raw(address.ptr as *const _, address.length) },
      port,
    ))
  }

  fn get_ssl_uri_sans(&self) -> Vec<EnvoyBuffer<'_>> {
    let size =
      unsafe { abi::envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(self.raw) };
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
      abi::envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
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
      unsafe { abi::envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(self.raw) };
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
      abi::envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
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
      abi::envoy_dynamic_module_callback_network_filter_get_ssl_subject(
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

  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_filter_state_bytes(
        self.raw,
        bytes_to_module_buffer(key),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: value.as_ptr() as abi::envoy_dynamic_module_type_buffer_module_ptr,
          length: value.len(),
        },
      )
    }
  }

  fn get_filter_state_bytes(&self, key: &[u8]) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_filter_state_bytes(
        self.raw,
        bytes_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn set_filter_state_typed(&mut self, key: &[u8], value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_filter_state_typed(
        self.raw,
        bytes_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn get_filter_state_typed(&self, key: &[u8]) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_filter_state_typed(
        self.raw,
        bytes_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      )
    }
  }

  fn get_dynamic_metadata_string(&self, namespace: &str, key: &str) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
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

  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn get_dynamic_metadata_number(&self, namespace: &str, key: &str) -> Option<f64> {
    let mut result: f64 = 0.0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
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

  fn set_dynamic_metadata_bool(&mut self, namespace: &str, key: &str, value: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
        self.raw,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn get_dynamic_metadata_bool(&self, namespace: &str, key: &str) -> Option<bool> {
    let mut result: bool = false;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
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

  fn set_socket_option_int(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    value: i64,
  ) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_socket_option_int(
        self.raw, level, name, state, value,
      )
    }
  }

  fn set_socket_option_bytes(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    value: &[u8],
  ) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_set_socket_option_bytes(
        self.raw,
        level,
        name,
        state,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: value.as_ptr() as *const _,
          length: value.len(),
        },
      )
    }
  }

  fn get_socket_option_int(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
  ) -> Option<i64> {
    let mut value: i64 = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_socket_option_int(
        self.raw, level, name, state, &mut value,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn get_socket_option_bytes(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
  ) -> Option<Vec<u8>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_get_socket_option_bytes(
        self.raw,
        level,
        name,
        state,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(slice.to_vec())
    } else {
      None
    }
  }

  fn get_socket_options(&self) -> Vec<SocketOption> {
    let size =
      unsafe { abi::envoy_dynamic_module_callback_network_get_socket_options_size(self.raw) };
    if size == 0 {
      return Vec::new();
    }
    let mut options: Vec<abi::envoy_dynamic_module_type_socket_option> = vec![
      abi::envoy_dynamic_module_type_socket_option {
        level: 0,
        name: 0,
        state: abi::envoy_dynamic_module_type_socket_option_state::Prebind,
        value_type: abi::envoy_dynamic_module_type_socket_option_value_type::Int,
        int_value: 0,
        byte_value: abi::envoy_dynamic_module_type_envoy_buffer {
          ptr: std::ptr::null(),
          length: 0,
        },
      };
      size
    ];
    unsafe {
      abi::envoy_dynamic_module_callback_network_get_socket_options(self.raw, options.as_mut_ptr())
    };

    options
      .into_iter()
      .map(|opt| {
        let value = match opt.value_type {
          abi::envoy_dynamic_module_type_socket_option_value_type::Int => {
            SocketOptionValue::Int(opt.int_value)
          },
          abi::envoy_dynamic_module_type_socket_option_value_type::Bytes => {
            if !opt.byte_value.ptr.is_null() && opt.byte_value.length > 0 {
              let bytes = unsafe {
                crate::ffi_helpers::slice_from_raw_or_empty(
                  opt.byte_value.ptr as *const u8,
                  opt.byte_value.length,
                )
                .to_vec()
              };
              SocketOptionValue::Bytes(bytes)
            } else {
              SocketOptionValue::Bytes(Vec::new())
            }
          },
        };
        SocketOption {
          level: opt.level,
          name: opt.name,
          state: opt.state,
          value,
        }
      })
      .collect()
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
      abi::envoy_dynamic_module_callback_network_filter_http_callout(
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

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_increment_counter(self.raw, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res =
      unsafe { abi::envoy_dynamic_module_callback_network_filter_set_gauge(self.raw, id, value) };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_increment_gauge(self.raw, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_decrement_gauge(self.raw, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_record_histogram_value(self.raw, id, value)
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn get_cluster_host_count(&self, cluster_name: &str, priority: u32) -> Option<ClusterHostCount> {
    let mut total: usize = 0;
    let mut healthy: usize = 0;
    let mut degraded: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
        self.raw,
        str_to_module_buffer(cluster_name),
        priority,
        &mut total as *mut _,
        &mut healthy as *mut _,
        &mut degraded as *mut _,
      )
    };
    if success {
      Some(ClusterHostCount {
        total,
        healthy,
        degraded,
      })
    } else {
      None
    }
  }

  fn get_upstream_host_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
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

  fn get_upstream_host_hostname(&self) -> Option<String> {
    let mut hostname = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
        self.raw,
        &mut hostname as *mut _ as *mut _,
      )
    };
    if !result || hostname.length == 0 || hostname.ptr.is_null() {
      return None;
    }
    let hostname_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(hostname.ptr as *const u8, hostname.length) };
    Some(hostname_str.into_owned())
  }

  fn get_upstream_host_cluster(&self) -> Option<String> {
    let mut cluster_name = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let result = unsafe {
      abi::envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
        self.raw,
        &mut cluster_name as *mut _ as *mut _,
      )
    };
    if !result || cluster_name.length == 0 || cluster_name.ptr.is_null() {
      return None;
    }
    let cluster_str = unsafe {
      crate::ffi_helpers::str_lossy_from_raw(cluster_name.ptr as *const u8, cluster_name.length)
    };
    Some(cluster_str.into_owned())
  }

  fn has_upstream_host(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_has_upstream_host(self.raw) }
  }

  fn start_upstream_secure_transport(&mut self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(self.raw)
    }
  }

  fn get_connection_state(&self) -> abi::envoy_dynamic_module_type_network_connection_state {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_get_connection_state(self.raw) }
  }

  fn read_disable(
    &mut self,
    disable: bool,
  ) -> abi::envoy_dynamic_module_type_network_read_disable_status {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_read_disable(self.raw, disable) }
  }

  fn read_enabled(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_read_enabled(self.raw) }
  }

  fn is_half_close_enabled(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_is_half_close_enabled(self.raw) }
  }

  fn enable_half_close(&mut self, enabled: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_network_filter_enable_half_close(self.raw, enabled)
    }
  }

  fn get_buffer_limit(&self) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_get_buffer_limit(self.raw) }
  }

  fn set_buffer_limits(&mut self, limit: u32) {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_set_buffer_limits(self.raw, limit) }
  }

  fn above_high_watermark(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_above_high_watermark(self.raw) }
  }

  fn new_scheduler(&self) -> impl EnvoyNetworkFilterScheduler + 'static {
    unsafe {
      let scheduler_ptr = abi::envoy_dynamic_module_callback_network_filter_scheduler_new(self.raw);
      EnvoyNetworkFilterSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }

  fn get_worker_index(&self) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_network_filter_get_worker_index(self.raw) }
  }
}

// Network Filter Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_config_new(
  envoy_filter_config_ptr: abi::envoy_dynamic_module_type_network_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_network_filter_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter_config = EnvoyNetworkFilterConfigImpl::new(envoy_filter_config_ptr);
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    init_network_filter_config(
      &mut envoy_filter_config,
      name_str.as_ref(),
      config_slice,
      NEW_NETWORK_FILTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_NETWORK_FILTER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_config_new", panic);
    std::ptr::null()
  })
}

pub(crate) fn init_network_filter_config<EC: EnvoyNetworkFilterConfig, ENF: EnvoyNetworkFilter>(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
  new_network_filter_config_fn: &NewNetworkFilterConfigFunction<EC, ENF>,
) -> abi::envoy_dynamic_module_type_network_filter_config_module_ptr {
  let network_filter_config = new_network_filter_config_fn(envoy_filter_config, name, config);
  match network_filter_config {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_network_filter_config_destroy(
  filter_config_ptr: abi::envoy_dynamic_module_type_network_filter_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(
      filter_config_ptr,
      NetworkFilterConfig<EnvoyNetworkFilterImpl>
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_network_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_network_filter_config_module_ptr,
  envoy_filter_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_network_filter_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter = EnvoyNetworkFilterImpl::new(envoy_filter_ptr);
    let filter_config = {
      let raw = filter_config_ptr as *const *const dyn NetworkFilterConfig<EnvoyNetworkFilterImpl>;
      &**raw
    };
    envoy_dynamic_module_on_network_filter_new_impl(&mut envoy_filter, filter_config)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_new", panic);
    std::ptr::null()
  })
}

pub(crate) fn envoy_dynamic_module_on_network_filter_new_impl(
  envoy_filter: &mut EnvoyNetworkFilterImpl,
  filter_config: &dyn NetworkFilterConfig<EnvoyNetworkFilterImpl>,
) -> abi::envoy_dynamic_module_type_network_filter_module_ptr {
  let filter = filter_config.new_network_filter(envoy_filter);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_new_connection(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_new_connection(&mut EnvoyNetworkFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_new_connection",
      panic,
    );
    abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_read(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
  data_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_read(
      &mut EnvoyNetworkFilterImpl::new(envoy_ptr),
      data_length,
      end_stream,
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_read", panic);
    abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_write(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
  data_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_write(
      &mut EnvoyNetworkFilterImpl::new(envoy_ptr),
      data_length,
      end_stream,
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_write", panic);
    abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_event(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
  event: abi::envoy_dynamic_module_type_network_connection_event,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_event(&mut EnvoyNetworkFilterImpl::new(envoy_ptr), event);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_event", panic);
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let _ =
      unsafe { Box::from_raw(filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>) };
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_destroy", panic);
  });
}

#[no_mangle]
/// # Safety
/// Caller must ensure `filter_ptr`, `headers`, and `body_chunks` point to valid memory for the
/// provided sizes, and that the pointed-to data lives for the duration of this call.
pub unsafe extern "C" fn envoy_dynamic_module_on_network_filter_http_callout_done(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
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
      &mut EnvoyNetworkFilterImpl::new(envoy_ptr),
      callout_id,
      result,
      header_vec,
      body_vec,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_http_callout_done",
      panic,
    );
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_scheduled(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_scheduled(&mut EnvoyNetworkFilterImpl::new(envoy_ptr), event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_network_filter_scheduled", panic);
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_config_scheduled(
  filter_config_ptr: abi::envoy_dynamic_module_type_network_filter_config_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter_config = {
      let raw = filter_config_ptr as *const *const dyn NetworkFilterConfig<EnvoyNetworkFilterImpl>;
      unsafe { &**raw }
    };
    filter_config.on_config_scheduled(event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_config_scheduled",
      panic,
    );
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_above_write_buffer_high_watermark(&mut EnvoyNetworkFilterImpl::new(envoy_ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark",
      panic,
    );
  });
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_network_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn NetworkFilter<EnvoyNetworkFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_below_write_buffer_low_watermark(&mut EnvoyNetworkFilterImpl::new(envoy_ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark",
      panic,
    );
  });
}
