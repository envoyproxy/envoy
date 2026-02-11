//! Transport socket support for dynamic modules.
//!
//! This module provides traits and types for implementing transport sockets as dynamic modules.
//! Transport sockets handle the actual read/write operations on network connections, typically
//! for implementing TLS or other transport-level protocols.

use crate::abi;
use std::ffi::c_void;

/// The action to take after an I/O operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PostIoAction {
  /// Keep the socket open.
  KeepOpen,
  /// Close the socket.
  Close,
}

/// The result of an I/O operation on a transport socket.
#[derive(Debug, Clone, Copy)]
pub struct IoResult {
  /// The action to take after the I/O operation.
  pub action: PostIoAction,
  /// Number of bytes processed by the I/O event.
  pub bytes_processed: u64,
  /// True if an end-of-stream was read from a connection. This can only be true for read
  /// operations.
  pub end_stream_read: bool,
}

impl IoResult {
  /// Create a new IoResult indicating keep-open with zero bytes processed.
  pub fn keep_open(bytes_processed: u64) -> Self {
    Self {
      action: PostIoAction::KeepOpen,
      bytes_processed,
      end_stream_read: false,
    }
  }

  /// Create a new IoResult indicating close with zero bytes processed.
  pub fn close(bytes_processed: u64) -> Self {
    Self {
      action: PostIoAction::Close,
      bytes_processed,
      end_stream_read: false,
    }
  }

  /// Create a new IoResult indicating keep-open with end-of-stream.
  pub fn end_of_stream(bytes_processed: u64) -> Self {
    Self {
      action: PostIoAction::KeepOpen,
      bytes_processed,
      end_stream_read: true,
    }
  }
}

impl From<IoResult> for abi::envoy_dynamic_module_type_transport_socket_io_result {
  fn from(result: IoResult) -> Self {
    Self {
      action: match result.action {
        PostIoAction::KeepOpen => {
          abi::envoy_dynamic_module_type_transport_socket_post_io_action::KeepOpen
        }
        PostIoAction::Close => {
          abi::envoy_dynamic_module_type_transport_socket_post_io_action::Close
        }
      },
      bytes_processed: result.bytes_processed,
      end_stream_read: result.end_stream_read,
    }
  }
}

/// Connection events that can occur on a transport socket.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionEvent {
  /// Remote peer closed the connection.
  RemoteClose,
  /// Local side closed the connection.
  LocalClose,
  /// Connection was established.
  Connected,
  /// Connection was established with 0-RTT.
  ConnectedZeroRtt,
}

impl From<abi::envoy_dynamic_module_type_network_connection_event> for ConnectionEvent {
  fn from(event: abi::envoy_dynamic_module_type_network_connection_event) -> Self {
    match event {
      abi::envoy_dynamic_module_type_network_connection_event::RemoteClose => {
        ConnectionEvent::RemoteClose
      }
      abi::envoy_dynamic_module_type_network_connection_event::LocalClose => {
        ConnectionEvent::LocalClose
      }
      abi::envoy_dynamic_module_type_network_connection_event::Connected => {
        ConnectionEvent::Connected
      }
      abi::envoy_dynamic_module_type_network_connection_event::ConnectedZeroRtt => {
        ConnectionEvent::ConnectedZeroRtt
      }
    }
  }
}

impl From<ConnectionEvent> for abi::envoy_dynamic_module_type_network_connection_event {
  fn from(event: ConnectionEvent) -> Self {
    match event {
      ConnectionEvent::RemoteClose => {
        abi::envoy_dynamic_module_type_network_connection_event::RemoteClose
      }
      ConnectionEvent::LocalClose => {
        abi::envoy_dynamic_module_type_network_connection_event::LocalClose
      }
      ConnectionEvent::Connected => {
        abi::envoy_dynamic_module_type_network_connection_event::Connected
      }
      ConnectionEvent::ConnectedZeroRtt => {
        abi::envoy_dynamic_module_type_network_connection_event::ConnectedZeroRtt
      }
    }
  }
}

/// The trait that represents the Envoy transport socket interface.
///
/// This provides callbacks into Envoy for the transport socket module to interact with
/// the underlying connection.
pub trait EnvoyTransportSocket {
  /// Get an opaque handle to the I/O handle for performing raw I/O operations.
  fn get_io_handle(&self) -> *mut c_void;

  /// Read data from the raw socket into the provided buffer.
  ///
  /// Returns `Ok(bytes_read)` on success, `Err(error_code)` on failure.
  fn io_handle_read(&self, buffer: &mut [u8]) -> Result<usize, i64>;

  /// Write data to the raw socket from the provided buffer.
  ///
  /// Returns `Ok(bytes_written)` on success, `Err(error_code)` on failure.
  fn io_handle_write(&self, buffer: &[u8]) -> Result<usize, i64>;

  /// Drain bytes from the read buffer.
  fn read_buffer_drain(&self, length: usize);

  /// Add data to the read buffer.
  fn read_buffer_add(&self, data: &[u8]);

  /// Get the length of the read buffer.
  fn read_buffer_length(&self) -> usize;

  /// Drain bytes from the write buffer.
  fn write_buffer_drain(&self, length: usize);

  /// Get slices of the write buffer. Returns the actual data as a vector of byte slices.
  fn write_buffer_get_slices(&self) -> Vec<&[u8]>;

  /// Get the length of the write buffer.
  fn write_buffer_length(&self) -> usize;

  /// Raise a connection event.
  fn raise_event(&self, event: ConnectionEvent);

  /// Check if the read buffer should be drained.
  fn should_drain_read_buffer(&self) -> bool;

  /// Mark the transport socket as readable.
  fn set_is_readable(&self);

  /// Flush the write buffer.
  fn flush_write_buffer(&self);
}

/// The concrete implementation of [`EnvoyTransportSocket`].
pub struct EnvoyTransportSocketImpl {
  raw: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
}

impl EnvoyTransportSocketImpl {
  /// Create a new EnvoyTransportSocketImpl from a raw pointer.
  pub fn new(raw: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyTransportSocket for EnvoyTransportSocketImpl {
  fn get_io_handle(&self) -> *mut c_void {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_get_io_handle(self.raw) }
  }

  fn io_handle_read(&self, buffer: &mut [u8]) -> Result<usize, i64> {
    let io_handle = self.get_io_handle();
    if io_handle.is_null() {
      return Err(-1);
    }
    let mut bytes_read: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_read(
        io_handle,
        buffer.as_mut_ptr() as *mut _,
        buffer.len(),
        &mut bytes_read,
      )
    };
    if result == 0 {
      Ok(bytes_read)
    } else {
      Err(result)
    }
  }

  fn io_handle_write(&self, buffer: &[u8]) -> Result<usize, i64> {
    let io_handle = self.get_io_handle();
    if io_handle.is_null() {
      return Err(-1);
    }
    let mut bytes_written: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_write(
        io_handle,
        buffer.as_ptr() as *const _,
        buffer.len(),
        &mut bytes_written,
      )
    };
    if result == 0 {
      Ok(bytes_written)
    } else {
      Err(result)
    }
  }

  fn read_buffer_drain(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_drain(self.raw, length)
    }
  }

  fn read_buffer_add(&self, data: &[u8]) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_add(
        self.raw,
        data.as_ptr() as *const _,
        data.len(),
      )
    }
  }

  fn read_buffer_length(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_length(self.raw)
    }
  }

  fn write_buffer_drain(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_drain(self.raw, length)
    }
  }

  fn write_buffer_get_slices(&self) -> Vec<&[u8]> {
    // First, get the count by passing a zero-length array.
    let mut count: usize = 0;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        std::ptr::null_mut(),
        &mut count,
      );
    }
    if count == 0 {
      return Vec::new();
    }
    let mut slices = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      count
    ];
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        slices.as_mut_ptr(),
        &mut count,
      );
    }
    slices
      .iter()
      .take(count)
      .filter(|s| !s.ptr.is_null() && s.length > 0)
      .map(|s| unsafe { std::slice::from_raw_parts(s.ptr as *const u8, s.length) })
      .collect()
  }

  fn write_buffer_length(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_length(self.raw)
    }
  }

  fn raise_event(&self, event: ConnectionEvent) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_raise_event(self.raw, event.into())
    }
  }

  fn should_drain_read_buffer(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(self.raw)
    }
  }

  fn set_is_readable(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_set_is_readable(self.raw)
    }
  }

  fn flush_write_buffer(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_flush_write_buffer(self.raw)
    }
  }
}

/// The trait for transport socket factory configuration.
///
/// This is responsible for creating new transport sockets for each connection.
/// Implementations must be `Sync` since they are accessed from worker threads.
pub trait TransportSocketFactoryConfig<ETS: EnvoyTransportSocket>: Sync {
  /// Create a new transport socket for a connection.
  ///
  /// Returns `Some(transport_socket)` on success, `None` on failure.
  fn new_transport_socket(&self, envoy: &mut ETS) -> Option<Box<dyn TransportSocket<ETS>>>;
}

/// The trait for a transport socket implementation.
///
/// All methods are called on the same thread as the one that created the transport socket.
pub trait TransportSocket<ETS: EnvoyTransportSocket> {
  /// Called when the transport socket callbacks are set. This is called before any I/O operations.
  fn on_set_callbacks(&mut self, _envoy: &mut ETS) {}

  /// Called when the underlying transport is established.
  fn on_connected(&mut self, _envoy: &mut ETS) {}

  /// Called when data is to be read from the connection.
  ///
  /// Returns the I/O result indicating the outcome of the read operation.
  fn on_do_read(&mut self, envoy: &mut ETS) -> IoResult;

  /// Called when data is to be written to the connection.
  ///
  /// `write_buffer_length` is the length of the write buffer.
  /// `end_stream` indicates if this is the end of the stream.
  ///
  /// Returns the I/O result indicating the outcome of the write operation.
  fn on_do_write(&mut self, envoy: &mut ETS, write_buffer_length: usize, end_stream: bool)
    -> IoResult;

  /// Called when the socket is being closed.
  fn on_close(&mut self, _envoy: &mut ETS, _event: ConnectionEvent) {}

  /// Get the negotiated protocol (e.g., ALPN). Returns `None` if not available.
  fn get_protocol(&self) -> Option<&str> {
    None
  }

  /// Get the failure reason. Returns `None` if no failure occurred.
  fn get_failure_reason(&self) -> Option<&str> {
    None
  }

  /// Check if the socket can be flushed and closed.
  fn can_flush_close(&self) -> bool {
    true
  }
}

/// The function signature for the new transport socket factory configuration function.
///
/// This is called when a new transport socket factory configuration is created, and it must return
/// a new [`TransportSocketFactoryConfig`] object. Returning `None` will cause the factory
/// configuration to be rejected.
///
/// The first argument `name` is the name of the transport socket implementation.
/// The second argument `config` is the raw configuration bytes.
/// The third argument `is_upstream` indicates if this is for upstream or downstream connections.
pub type NewTransportSocketFactoryConfigFunction<ETS> =
  fn(name: &str, config: &[u8], is_upstream: bool) -> Option<Box<dyn TransportSocketFactoryConfig<ETS>>>;

// ---------------------------------------------------------------------------
// FFI exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_new(
  _factory_config_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr,
  socket_name: abi::envoy_dynamic_module_type_envoy_buffer,
  socket_config: abi::envoy_dynamic_module_type_envoy_buffer,
  is_upstream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr {
  let name = if socket_name.ptr.is_null() || socket_name.length == 0 {
    ""
  } else {
    unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        socket_name.ptr as *const u8,
        socket_name.length,
      ))
    }
  };
  let config = if socket_config.ptr.is_null() || socket_config.length == 0 {
    &[]
  } else {
    unsafe { std::slice::from_raw_parts(socket_config.ptr as *const u8, socket_config.length) }
  };

  let new_fn = match crate::NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION.get() {
    Some(f) => f,
    None => return std::ptr::null(),
  };

  let factory_config = new_fn(name, config, is_upstream);
  match factory_config {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_destroy(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
) {
  crate::drop_wrapped_c_void_ptr!(
    factory_config_ptr,
    TransportSocketFactoryConfig<EnvoyTransportSocketImpl>
  );
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_new(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_module_ptr {
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  let factory_config = unsafe {
    let raw =
      factory_config_ptr as *const *const dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>;
    &**raw
  };
  match factory_config.new_transport_socket(&mut envoy_socket) {
    Some(socket) => crate::wrap_into_c_void_ptr!(socket),
    None => std::ptr::null(),
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_destroy(
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  crate::drop_wrapped_c_void_ptr!(
    transport_socket_module_ptr,
    TransportSocket<EnvoyTransportSocketImpl>
  );
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_set_callbacks(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut *socket };
  socket.on_set_callbacks(&mut EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_on_connected(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut *socket };
  socket.on_connected(&mut EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_do_read(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut *socket };
  let result = socket.on_do_read(&mut EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr));
  result.into()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_do_write(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  write_buffer_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut *socket };
  let result = socket.on_do_write(
    &mut EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr),
    write_buffer_length,
    end_stream,
  );
  result.into()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_close(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  event: abi::envoy_dynamic_module_type_network_connection_event,
) {
  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut *socket };
  socket.on_close(
    &mut EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr),
    event.into(),
  );
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_get_protocol(
  _transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &*socket };
  if let Some(protocol) = socket.get_protocol() {
    unsafe {
      (*result).ptr = protocol.as_ptr() as *mut _;
      (*result).length = protocol.len();
    }
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_get_failure_reason(
  _transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &*socket };
  if let Some(reason) = socket.get_failure_reason() {
    unsafe {
      (*result).ptr = reason.as_ptr() as *mut _;
      (*result).length = reason.len();
    }
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_can_flush_close(
  _transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &*socket };
  socket.can_flush_close()
}

