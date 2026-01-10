//! Transport Socket module for Envoy dynamic modules.
//!
//! This module provides the Rust SDK for implementing transport sockets in dynamic modules.
//! Transport sockets handle the actual read/write operations on connections, typically used
//! for implementing TLS or other transport-level protocols.

use crate::abi;
use std::sync::OnceLock;

/// The function signature for creating a new transport socket factory config.
///
/// This is called when a new transport socket factory configuration is created.
/// It must return a new instance of the [`TransportSocketFactoryConfig`] object.
/// Returning `None` will cause the factory configuration to be rejected.
pub type NewTransportSocketFactoryConfigFunction<ETSFC> =
  fn(
    envoy_factory_config: &mut ETSFC,
    socket_name: &str,
    socket_config: &[u8],
    is_upstream: bool,
  ) -> Option<Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>>;

/// The global init function for transport socket factory configurations.
/// This is set via the `declare_transport_socket_init_functions` macro.
pub static NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION: OnceLock<
  NewTransportSocketFactoryConfigFunction<EnvoyTransportSocketFactoryConfigImpl>,
> = OnceLock::new();

/// Declare the init functions for transport socket dynamic modules.
///
/// # Example
///
/// ```ignore
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
/// use envoy_proxy_dynamic_modules_rust_sdk::transport_socket::*;
///
/// declare_transport_socket_init_functions!(my_program_init, my_new_factory_config);
///
/// fn my_program_init() -> bool {
///   true
/// }
///
/// fn my_new_factory_config<ETSFC: EnvoyTransportSocketFactoryConfig>(
///   _envoy_factory_config: &mut ETSFC,
///   socket_name: &str,
///   _socket_config: &[u8],
///   _is_upstream: bool,
/// ) -> Option<Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>> {
///   Some(Box::new(MyFactoryConfig {}))
/// }
/// ```
#[macro_export]
macro_rules! declare_transport_socket_init_functions {
  ($f:ident, $new_factory_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      $crate::transport_socket::NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION
        .get_or_init(|| $new_factory_config_fn);
      if ($f()) {
        $crate::abi::kAbiVersion.as_ptr() as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
}

/// Represents the result of an I/O operation.
#[derive(Debug, Clone, Copy)]
pub struct IoResult {
  /// The action to take after the I/O operation.
  pub action: PostIoAction,
  /// Number of bytes processed by the I/O event.
  pub bytes_processed: u64,
  /// True if an end-of-stream was read from a connection.
  pub end_stream_read: bool,
}

impl IoResult {
  /// Create a new IoResult indicating the socket should be kept open.
  pub fn keep_open(bytes_processed: u64, end_stream_read: bool) -> Self {
    Self {
      action: PostIoAction::KeepOpen,
      bytes_processed,
      end_stream_read,
    }
  }

  /// Create a new IoResult indicating the socket should be closed.
  pub fn close(bytes_processed: u64, end_stream_read: bool) -> Self {
    Self {
      action: PostIoAction::Close,
      bytes_processed,
      end_stream_read,
    }
  }
}

impl From<IoResult> for abi::envoy_dynamic_module_type_transport_socket_io_result {
  fn from(result: IoResult) -> Self {
    Self {
      action: result.action.into(),
      bytes_processed: result.bytes_processed,
      end_stream_read: result.end_stream_read,
    }
  }
}

/// The action to take after an I/O operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PostIoAction {
  /// Keep the socket open.
  KeepOpen,
  /// Close the socket.
  Close,
}

impl From<PostIoAction> for abi::envoy_dynamic_module_type_transport_socket_post_io_action {
  fn from(action: PostIoAction) -> Self {
    match action {
      PostIoAction::KeepOpen => {
        abi::envoy_dynamic_module_type_transport_socket_post_io_action::KeepOpen
      },
      PostIoAction::Close => abi::envoy_dynamic_module_type_transport_socket_post_io_action::Close,
    }
  }
}

/// Connection events.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionEvent {
  /// Remote closed the connection.
  RemoteClose,
  /// Local closed the connection.
  LocalClose,
  /// Connection established.
  Connected,
  /// Connection established with 0-RTT.
  ConnectedZeroRtt,
}

impl From<abi::envoy_dynamic_module_type_network_connection_event> for ConnectionEvent {
  fn from(event: abi::envoy_dynamic_module_type_network_connection_event) -> Self {
    match event {
      abi::envoy_dynamic_module_type_network_connection_event::RemoteClose => {
        ConnectionEvent::RemoteClose
      },
      abi::envoy_dynamic_module_type_network_connection_event::LocalClose => {
        ConnectionEvent::LocalClose
      },
      abi::envoy_dynamic_module_type_network_connection_event::Connected => {
        ConnectionEvent::Connected
      },
      abi::envoy_dynamic_module_type_network_connection_event::ConnectedZeroRtt => {
        ConnectionEvent::ConnectedZeroRtt
      },
    }
  }
}

impl From<ConnectionEvent> for abi::envoy_dynamic_module_type_network_connection_event {
  fn from(event: ConnectionEvent) -> Self {
    match event {
      ConnectionEvent::RemoteClose => {
        abi::envoy_dynamic_module_type_network_connection_event::RemoteClose
      },
      ConnectionEvent::LocalClose => {
        abi::envoy_dynamic_module_type_network_connection_event::LocalClose
      },
      ConnectionEvent::Connected => {
        abi::envoy_dynamic_module_type_network_connection_event::Connected
      },
      ConnectionEvent::ConnectedZeroRtt => {
        abi::envoy_dynamic_module_type_network_connection_event::ConnectedZeroRtt
      },
    }
  }
}

/// Trait representing the Envoy transport socket factory config.
pub trait EnvoyTransportSocketFactoryConfig {}

/// Implementation of EnvoyTransportSocketFactoryConfig.
pub struct EnvoyTransportSocketFactoryConfigImpl {
  raw_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr,
}

impl EnvoyTransportSocketFactoryConfig for EnvoyTransportSocketFactoryConfigImpl {}

impl EnvoyTransportSocketFactoryConfigImpl {
  fn new(
    raw_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr,
  ) -> Self {
    Self { raw_ptr }
  }

  #[allow(dead_code)]
  fn raw_ptr(&self) -> abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr {
    self.raw_ptr
  }
}

/// Trait representing the Envoy transport socket.
/// This provides methods to interact with the underlying Envoy transport socket.
pub trait EnvoyTransportSocket {
  /// Get the I/O handle for performing raw I/O operations.
  fn get_io_handle(&self) -> *mut std::ffi::c_void;

  /// Read data from the raw socket.
  fn io_read(&self, buffer: &mut [u8]) -> Result<usize, i64>;

  /// Write data to the raw socket.
  fn io_write(&self, buffer: &[u8]) -> Result<usize, i64>;

  /// Drain bytes from the read buffer.
  fn read_buffer_drain(&mut self, length: usize);

  /// Add data to the read buffer.
  fn read_buffer_add(&mut self, data: &[u8]);

  /// Get the length of the read buffer.
  fn read_buffer_length(&self) -> usize;

  /// Drain bytes from the write buffer.
  fn write_buffer_drain(&mut self, length: usize);

  /// Get the slices of the write buffer.
  fn write_buffer_get_slices(&self) -> Vec<&[u8]>;

  /// Get the length of the write buffer.
  fn write_buffer_length(&self) -> usize;

  /// Raise a connection event.
  fn raise_event(&mut self, event: ConnectionEvent);

  /// Check if the read buffer should be drained.
  fn should_drain_read_buffer(&self) -> bool;

  /// Mark the transport socket as readable.
  fn set_is_readable(&mut self);

  /// Flush the write buffer.
  fn flush_write_buffer(&mut self);
}

/// Implementation of EnvoyTransportSocket.
pub struct EnvoyTransportSocketImpl {
  raw_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
}

impl EnvoyTransportSocketImpl {
  fn new(raw_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr) -> Self {
    Self { raw_ptr }
  }
}

impl EnvoyTransportSocket for EnvoyTransportSocketImpl {
  fn get_io_handle(&self) -> *mut std::ffi::c_void {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_get_io_handle(self.raw_ptr) }
  }

  fn io_read(&self, buffer: &mut [u8]) -> Result<usize, i64> {
    let io_handle = self.get_io_handle();
    let mut bytes_read: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_read(
        io_handle,
        buffer.as_mut_ptr() as *mut std::os::raw::c_char,
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

  fn io_write(&self, buffer: &[u8]) -> Result<usize, i64> {
    let io_handle = self.get_io_handle();
    let mut bytes_written: usize = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_write(
        io_handle,
        buffer.as_ptr() as *const std::os::raw::c_char,
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

  fn read_buffer_drain(&mut self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_drain(self.raw_ptr, length);
    }
  }

  fn read_buffer_add(&mut self, data: &[u8]) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_add(
        self.raw_ptr,
        data.as_ptr() as *const std::os::raw::c_char,
        data.len(),
      );
    }
  }

  fn read_buffer_length(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_read_buffer_length(self.raw_ptr) }
  }

  fn write_buffer_drain(&mut self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_drain(self.raw_ptr, length);
    }
  }

  fn write_buffer_get_slices(&self) -> Vec<&[u8]> {
    const MAX_SLICES: usize = 64;
    let mut slices: [abi::envoy_dynamic_module_type_envoy_buffer; MAX_SLICES] =
      [abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      }; MAX_SLICES];
    let mut count = MAX_SLICES;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw_ptr,
        slices.as_mut_ptr(),
        &mut count,
      );
    }
    slices[.. count]
      .iter()
      .filter_map(|s| {
        if s.ptr.is_null() || s.length == 0 {
          None
        } else {
          Some(unsafe { std::slice::from_raw_parts(s.ptr as *const u8, s.length) })
        }
      })
      .collect()
  }

  fn write_buffer_length(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_write_buffer_length(self.raw_ptr) }
  }

  fn raise_event(&mut self, event: ConnectionEvent) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_raise_event(self.raw_ptr, event.into());
    }
  }

  fn should_drain_read_buffer(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(self.raw_ptr)
    }
  }

  fn set_is_readable(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_set_is_readable(self.raw_ptr);
    }
  }

  fn flush_write_buffer(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_flush_write_buffer(self.raw_ptr);
    }
  }
}

/// Trait representing the transport socket factory configuration.
/// This is responsible for creating new transport sockets.
pub trait TransportSocketFactoryConfig<ETS: EnvoyTransportSocket>: Sync {
  /// Create a new transport socket for a connection.
  fn new_transport_socket(&self, envoy_socket: &mut ETS) -> Box<dyn TransportSocket<ETS>>;
}

/// Trait representing a transport socket.
/// This handles the actual I/O operations on the connection.
pub trait TransportSocket<ETS: EnvoyTransportSocket> {
  /// Called when the transport socket callbacks are set.
  fn on_set_callbacks(&mut self, _envoy_socket: &mut ETS) {}

  /// Called when the underlying transport is connected.
  fn on_connected(&mut self, _envoy_socket: &mut ETS) {}

  /// Called to perform a read operation.
  /// Returns the result of the read operation.
  fn do_read(&mut self, envoy_socket: &mut ETS) -> IoResult;

  /// Called to perform a write operation.
  /// Returns the result of the write operation.
  fn do_write(
    &mut self,
    envoy_socket: &mut ETS,
    write_buffer_length: usize,
    end_stream: bool,
  ) -> IoResult;

  /// Called when the socket is being closed.
  fn on_close(&mut self, _envoy_socket: &mut ETS, _event: ConnectionEvent) {}

  /// Get the negotiated protocol (e.g., from ALPN).
  /// Returns an empty string if no protocol was negotiated.
  fn get_protocol(&self) -> &str {
    ""
  }

  /// Get the failure reason if the transport socket failed.
  /// Returns an empty string if no failure occurred.
  fn get_failure_reason(&self) -> &str {
    ""
  }

  /// Check if the socket can be flushed and closed.
  fn can_flush_close(&self) -> bool {
    true
  }
}

// FFI exports for the transport socket module.
// Note: Box<dyn Trait> is a fat pointer (16 bytes), so we wrap it in another Box to get a thin
// pointer for FFI. This is the same pattern used in lib.rs for HTTP filters.

/// Wrap a Box<dyn T> into a *const c_void pointer for FFI.
/// The returned pointer is a pointer to a Box<Box<dyn T>>.
macro_rules! wrap_into_c_void_ptr {
  ($t:expr) => {{
    let boxed = Box::new($t);
    Box::into_raw(boxed) as *const ::std::os::raw::c_void
  }};
}

// Type aliases for long ABI types to comply with line length requirements.
type FactoryConfigEnvoyPtr =
  abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr;
type FactoryConfigModulePtr =
  abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr;

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_new(
  factory_config_envoy_ptr: FactoryConfigEnvoyPtr,
  socket_name: abi::envoy_dynamic_module_type_envoy_buffer,
  socket_config: abi::envoy_dynamic_module_type_envoy_buffer,
  is_upstream: bool,
) -> FactoryConfigModulePtr {
  let init_fn = match NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION.get() {
    Some(f) => f,
    None => return std::ptr::null(),
  };

  let name_str = if socket_name.ptr.is_null() || socket_name.length == 0 {
    ""
  } else {
    unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        socket_name.ptr as *const u8,
        socket_name.length,
      ))
    }
  };

  let config_bytes = if socket_config.ptr.is_null() || socket_config.length == 0 {
    &[]
  } else {
    unsafe { std::slice::from_raw_parts(socket_config.ptr as *const u8, socket_config.length) }
  };

  let mut envoy_factory_config =
    EnvoyTransportSocketFactoryConfigImpl::new(factory_config_envoy_ptr);

  match init_fn(
    &mut envoy_factory_config,
    name_str,
    config_bytes,
    is_upstream,
  ) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_destroy(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
) {
  if factory_config_ptr.is_null() {
    return;
  }
  let _ = unsafe {
    Box::from_raw(
      factory_config_ptr as *mut Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>,
    )
  };
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_new(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_module_ptr {
  if factory_config_ptr.is_null() {
    return std::ptr::null();
  }

  let factory_config = unsafe {
    &**(factory_config_ptr
      as *const Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>)
  };

  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  let socket = factory_config.new_transport_socket(&mut envoy_socket);
  wrap_into_c_void_ptr!(socket)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_destroy(
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  if transport_socket_module_ptr.is_null() {
    return;
  }
  let _ = unsafe {
    Box::from_raw(
      transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>,
    )
  };
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_set_callbacks(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  if transport_socket_module_ptr.is_null() {
    return;
  }

  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut **socket };
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  socket.on_set_callbacks(&mut envoy_socket);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_on_connected(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  if transport_socket_module_ptr.is_null() {
    return;
  }

  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut **socket };
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  socket.on_connected(&mut envoy_socket);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_do_read(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  if transport_socket_module_ptr.is_null() {
    return IoResult::close(0, false).into();
  }

  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut **socket };
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  socket.do_read(&mut envoy_socket).into()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_do_write(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  write_buffer_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  if transport_socket_module_ptr.is_null() {
    return IoResult::close(0, false).into();
  }

  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut **socket };
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  socket
    .do_write(&mut envoy_socket, write_buffer_length, end_stream)
    .into()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_close(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  event: abi::envoy_dynamic_module_type_network_connection_event,
) {
  if transport_socket_module_ptr.is_null() {
    return;
  }

  let socket =
    transport_socket_module_ptr as *mut Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &mut **socket };
  let mut envoy_socket = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
  socket.on_close(&mut envoy_socket, event.into());
}

/// # Safety
///
/// This function is called from C code and the caller must ensure that:
/// - `transport_socket_module_ptr` is a valid pointer to a `Box<dyn TransportSocket>`.
/// - `result` is a valid pointer to a `envoy_dynamic_module_type_module_buffer`.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_get_protocol(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let _ = transport_socket_envoy_ptr;
  if transport_socket_module_ptr.is_null() || result.is_null() {
    return;
  }

  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = &**socket;
  let protocol = socket.get_protocol();
  (*result).ptr = protocol.as_ptr() as *const std::os::raw::c_char;
  (*result).length = protocol.len();
}

/// # Safety
///
/// This function is called from C code and the caller must ensure that:
/// - `transport_socket_module_ptr` is a valid pointer to a `Box<dyn TransportSocket>`.
/// - `result` is a valid pointer to a `envoy_dynamic_module_type_module_buffer`.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_get_failure_reason(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let _ = transport_socket_envoy_ptr;
  if transport_socket_module_ptr.is_null() || result.is_null() {
    return;
  }

  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = &**socket;
  let reason = socket.get_failure_reason();
  (*result).ptr = reason.as_ptr() as *const std::os::raw::c_char;
  (*result).length = reason.len();
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_transport_socket_can_flush_close(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  let _ = transport_socket_envoy_ptr;
  if transport_socket_module_ptr.is_null() {
    return true;
  }

  let socket =
    transport_socket_module_ptr as *const Box<dyn TransportSocket<EnvoyTransportSocketImpl>>;
  let socket = unsafe { &**socket };
  socket.can_flush_close()
}
