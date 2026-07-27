//! Transport socket support for dynamic modules.
//!
//! This module provides traits and types for implementing custom transport sockets as dynamic
//! modules. A transport socket performs I/O and participates in connection lifecycle for TCP
//! connections in Envoy.

use crate::{abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, wrap_into_c_void_ptr};
use mockall::*;
use std::cell::RefCell;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// What should happen to the connection after a transport socket read or write completes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PostIoAction {
  KeepOpen,
  Close,
}

/// Result of a transport socket read or write operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IoResult {
  /// Whether the connection should stay open or close after this I/O.
  pub action: PostIoAction,
  /// Number of bytes consumed from the relevant buffer or written to the transport.
  pub bytes_processed: usize,
  /// True when the read side observed end-of-stream.
  pub end_stream_read: bool,
}

impl IoResult {
  /// Builds a result that keeps the connection open.
  pub fn keep_open(bytes_processed: usize, end_stream_read: bool) -> Self {
    Self {
      action: PostIoAction::KeepOpen,
      bytes_processed,
      end_stream_read,
    }
  }

  /// Builds a result that closes the connection after this I/O.
  pub fn close(bytes_processed: usize, end_stream_read: bool) -> Self {
    Self {
      action: PostIoAction::Close,
      bytes_processed,
      end_stream_read,
    }
  }
}

impl From<IoResult> for abi::envoy_dynamic_module_type_transport_socket_io_result {
  fn from(value: IoResult) -> Self {
    Self {
      action: value.action.into(),
      bytes_processed: value.bytes_processed as u64,
      end_stream_read: value.end_stream_read,
    }
  }
}

impl From<abi::envoy_dynamic_module_type_transport_socket_io_result> for IoResult {
  fn from(value: abi::envoy_dynamic_module_type_transport_socket_io_result) -> Self {
    Self {
      action: value.action.into(),
      bytes_processed: value.bytes_processed as usize,
      end_stream_read: value.end_stream_read,
    }
  }
}

impl From<PostIoAction> for abi::envoy_dynamic_module_type_transport_socket_post_io_action {
  fn from(value: PostIoAction) -> Self {
    match value {
      PostIoAction::KeepOpen => {
        abi::envoy_dynamic_module_type_transport_socket_post_io_action::KeepOpen
      },
      PostIoAction::Close => abi::envoy_dynamic_module_type_transport_socket_post_io_action::Close,
    }
  }
}

impl From<abi::envoy_dynamic_module_type_transport_socket_post_io_action> for PostIoAction {
  fn from(value: abi::envoy_dynamic_module_type_transport_socket_post_io_action) -> Self {
    match value {
      abi::envoy_dynamic_module_type_transport_socket_post_io_action::KeepOpen => {
        PostIoAction::KeepOpen
      },
      abi::envoy_dynamic_module_type_transport_socket_post_io_action::Close => PostIoAction::Close,
    }
  }
}

/// Outcome of a raw socket read or write performed via [`EnvoyTransportSocket::io_read`] and
/// [`EnvoyTransportSocket::io_write`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IoStatus {
  /// Bytes were transferred. For a read with a non-empty buffer, zero bytes means the peer closed
  /// the read side.
  Success,
  /// The socket would block. The caller should stop and retry later.
  Again,
  /// The operation failed. The caller should close the connection.
  Error,
}

impl From<abi::envoy_dynamic_module_type_transport_socket_io_status> for IoStatus {
  fn from(value: abi::envoy_dynamic_module_type_transport_socket_io_status) -> Self {
    match value {
      abi::envoy_dynamic_module_type_transport_socket_io_status::Success => IoStatus::Success,
      abi::envoy_dynamic_module_type_transport_socket_io_status::Again => IoStatus::Again,
      abi::envoy_dynamic_module_type_transport_socket_io_status::Error => IoStatus::Error,
    }
  }
}

/// Connection lifecycle events forwarded through the transport socket surface.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionEvent {
  RemoteClose,
  LocalClose,
  Connected,
  ConnectedZeroRtt,
}

impl From<ConnectionEvent> for abi::envoy_dynamic_module_type_network_connection_event {
  fn from(value: ConnectionEvent) -> Self {
    match value {
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

impl From<abi::envoy_dynamic_module_type_network_connection_event> for ConnectionEvent {
  fn from(value: abi::envoy_dynamic_module_type_network_connection_event) -> Self {
    match value {
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

/// Envoy-side operations available to an in-module transport socket implementation.
#[automock]
pub trait EnvoyTransportSocket {
  /// Reads raw bytes from the underlying socket into `buffer`. Returns the status and the number of
  /// bytes read. When `buffer` is non-empty, a `Success` status with zero bytes means the peer
  /// closed the read side.
  fn io_read(&self, buffer: &mut [u8]) -> (IoStatus, usize);
  /// Writes raw bytes to the underlying socket from `data`. Returns the status and the number of
  /// bytes written.
  fn io_write(&self, data: &[u8]) -> (IoStatus, usize);
  /// Shuts down the write side of the underlying socket so the peer observes end of stream.
  fn io_shutdown_write(&self);
  /// Returns the native OS file descriptor of the underlying socket, or `None` when unavailable.
  /// The descriptor is owned by Envoy and is only valid while the connection is open, so the module
  /// must not close it.
  fn get_fd(&self) -> Option<i32>;
  /// Appends `data` to the read buffer.
  fn read_buffer_add(&self, data: &[u8]);
  /// Appends the current contents of the write buffer to `out`.
  fn copy_write_buffer(&self, out: &mut Vec<u8>);
  /// Drains `length` bytes from the start of the write buffer.
  fn write_buffer_drain(&self, length: usize);
  /// Returns the number of bytes currently queued in the write buffer.
  fn write_buffer_length(&self) -> usize;
  /// Raises `event` on the underlying connection.
  fn raise_event(&self, event: ConnectionEvent);
  /// Returns whether Envoy expects the read buffer to be drained for flow-control reasons.
  fn should_drain_read_buffer(&self) -> bool;
  /// Requests that a future event-loop iteration schedules a read.
  fn set_is_readable(&self);
  /// Schedules a write on a future event-loop iteration. A module calls this when it defers
  /// buffered bytes for its own reasons rather than because the socket reported it would block.
  fn set_is_writable(&self);
  /// Requests that the connection flush its write buffer, for example after queuing handshake
  /// bytes outside of a write step.
  fn flush_write_buffer(&self);
  /// Returns the remote (peer) address as `(address, port)`. The address is empty when unavailable.
  fn get_remote_address(&self) -> (String, u32);
  /// Returns the local address as `(address, port)`. The address is empty when unavailable.
  fn get_local_address(&self) -> (String, u32);
}

/// Envoy transport socket handle implemented with ABI callbacks into Envoy.
pub struct EnvoyTransportSocketImpl {
  raw: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
}

impl EnvoyTransportSocketImpl {
  /// Wraps an Envoy-provided transport socket pointer.
  pub fn new(raw: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr) -> Self {
    Self { raw }
  }

  /// Shared body for the local and remote address callbacks.
  fn get_address(
    &self,
    callback: unsafe extern "C" fn(
      abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
      *mut abi::envoy_dynamic_module_type_envoy_buffer,
      *mut u32,
    ) -> bool,
  ) -> (String, u32) {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let available = unsafe { callback(self.raw, &mut address, &mut port) };
    if !available || address.length == 0 || address.ptr.is_null() {
      return (String::new(), 0);
    }
    let address =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    (address.into_owned(), port)
  }
}

impl EnvoyTransportSocket for EnvoyTransportSocketImpl {
  fn io_read(&self, buffer: &mut [u8]) -> (IoStatus, usize) {
    let mut bytes_read: usize = 0;
    let status = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_read(
        self.raw,
        buffer.as_mut_ptr().cast(),
        buffer.len(),
        &mut bytes_read,
      )
    };
    (status.into(), bytes_read)
  }

  fn io_write(&self, data: &[u8]) -> (IoStatus, usize) {
    let mut bytes_written: usize = 0;
    let status = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_write(
        self.raw,
        data.as_ptr().cast(),
        data.len(),
        &mut bytes_written,
      )
    };
    (status.into(), bytes_written)
  }

  fn io_shutdown_write(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_shutdown_write(self.raw);
    }
  }

  fn get_fd(&self) -> Option<i32> {
    let fd = unsafe { abi::envoy_dynamic_module_callback_transport_socket_get_fd(self.raw) };
    if fd < 0 {
      None
    } else {
      Some(fd)
    }
  }

  fn read_buffer_add(&self, data: &[u8]) {
    if data.is_empty() {
      return;
    }
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_add(
        self.raw,
        data.as_ptr().cast(),
        data.len(),
      );
    }
  }

  fn copy_write_buffer(&self, out: &mut Vec<u8>) {
    let mut count: usize = 0;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        std::ptr::null_mut(),
        &mut count,
      );
    }
    if count == 0 {
      return;
    }
    let mut slices = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      count
    ];
    let mut filled = count;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        slices.as_mut_ptr(),
        &mut filled,
      );
    }
    for slice in &slices[..filled.min(count)] {
      if slice.ptr.is_null() || slice.length == 0 {
        continue;
      }
      let bytes = unsafe { std::slice::from_raw_parts(slice.ptr as *const u8, slice.length) };
      out.extend_from_slice(bytes);
    }
  }

  fn write_buffer_drain(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_drain(self.raw, length);
    }
  }

  fn write_buffer_length(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_write_buffer_length(self.raw) }
  }

  fn raise_event(&self, event: ConnectionEvent) {
    let abi_event: abi::envoy_dynamic_module_type_network_connection_event = event.into();
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_raise_event(self.raw, abi_event);
    }
  }

  fn should_drain_read_buffer(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(self.raw)
    }
  }

  fn set_is_readable(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_set_is_readable(self.raw);
    }
  }

  fn set_is_writable(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_set_is_writable(self.raw);
    }
  }

  fn flush_write_buffer(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_flush_write_buffer(self.raw);
    }
  }

  fn get_remote_address(&self) -> (String, u32) {
    self.get_address(abi::envoy_dynamic_module_callback_transport_socket_get_remote_address)
  }

  fn get_local_address(&self) -> (String, u32) {
    self.get_address(abi::envoy_dynamic_module_callback_transport_socket_get_local_address)
  }
}

/// In-module factory configuration for transport sockets created from this module.
///
/// This trait must be implemented by the module. Implementations must be `Sync` since they are
/// accessed from worker threads.
pub trait TransportSocketFactoryConfig<ETS: EnvoyTransportSocket + ?Sized>: Sync {
  /// Creates a new transport socket instance for a connection.
  fn new_transport_socket(&self, envoy: &mut ETS) -> Box<dyn TransportSocket<ETS>>;
}

/// In-module transport socket instance for a single connection.
///
/// All the event hooks are called on the same thread as the one that the [`TransportSocket`] is
/// created via the [`TransportSocketFactoryConfig::new_transport_socket`] method.
pub trait TransportSocket<ETS: EnvoyTransportSocket + ?Sized> {
  /// Called when Envoy installs callbacks on the transport socket.
  fn on_set_callbacks(&mut self, envoy: &mut ETS);
  /// Called when the transport reports that the connection is established.
  fn on_connected(&mut self, envoy: &mut ETS);
  /// Performs a read/decrypt step into the read buffer.
  fn on_do_read(&mut self, envoy: &mut ETS) -> IoResult;
  /// Performs an encrypt/write step from the write buffer.
  fn on_do_write(&mut self, envoy: &mut ETS, end_stream: bool) -> IoResult;
  /// Called when the transport socket is closed. When `abort_reset` is true the connection is being
  /// torn down with a TCP reset and any graceful shutdown should be skipped.
  fn on_close(&mut self, envoy: &mut ETS, event: ConnectionEvent, abort_reset: bool);
  /// Returns the negotiated application protocol, if any.
  fn get_protocol(&self, envoy: &mut ETS) -> String;
  /// Returns a human-readable failure reason, if any.
  fn get_failure_reason(&self, envoy: &mut ETS) -> String;
  /// Returns whether the socket may flush and close.
  fn can_flush_close(&self, envoy: &mut ETS) -> bool;
  /// Instructs the socket to begin using secure transport, as used by the STARTTLS pattern. Returns
  /// whether the socket switched to secure transport. Sockets that do not support this return
  /// false.
  fn start_secure_transport(&mut self, envoy: &mut ETS) -> bool;
}

// Internal Implementation Types

thread_local! {
  static GET_PROTOCOL_BUF: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
  static GET_FAILURE_REASON_BUF: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

/// Wraps an in-module transport socket instance for use in FFI callbacks.
struct TransportSocketWrapper {
  socket: Box<dyn TransportSocket<EnvoyTransportSocketImpl>>,
}

fn fill_string_buffer_out(
  tls: &'static std::thread::LocalKey<RefCell<Vec<u8>>>,
  dst: *mut abi::envoy_dynamic_module_type_module_buffer,
  value: &str,
) {
  if dst.is_null() {
    return;
  }
  tls.with(|cell| {
    let mut buf = cell.borrow_mut();
    buf.clear();
    buf.extend_from_slice(value.as_bytes());
    let filled = bytes_to_module_buffer(buf.as_slice());
    unsafe {
      *dst = filled;
    }
  });
}

// Transport Socket Event Hook Implementations

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr,
  socket_name: abi::envoy_dynamic_module_type_envoy_buffer,
  socket_config: abi::envoy_dynamic_module_type_envoy_buffer,
  is_upstream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let name_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(socket_name.ptr as *const u8, socket_name.length)
    };
    let name_str = match std::str::from_utf8(name_bytes) {
      Ok(s) => s,
      Err(_) => {
        crate::envoy_log_error!("transport socket factory config: socket_name is not valid UTF-8.");
        return std::ptr::null();
      },
    };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(
        socket_config.ptr as *const u8,
        socket_config.length,
      )
    };
    let Some(new_config_fn) = crate::NEW_TRANSPORT_SOCKET_FACTORY_CONFIG_FUNCTION.get() else {
      crate::envoy_log_error!(
        "transport socket factory config function not registered; ensure \
         declare_all_init_functions! includes transport_socket."
      );
      return std::ptr::null();
    };
    match new_config_fn(name_str, config_slice, is_upstream) {
      Some(config) => wrap_into_c_void_ptr!(config),
      None => std::ptr::null(),
    }
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_factory_config_new",
      panic,
    );
    std::ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_factory_config_destroy(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(
      factory_config_ptr,
      TransportSocketFactoryConfig<EnvoyTransportSocketImpl>
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_factory_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_new(
  factory_config_ptr: abi::envoy_dynamic_module_type_transport_socket_factory_config_module_ptr,
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let config = factory_config_ptr
      as *const *const dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>;
    let config = unsafe { &**config };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let socket = config.new_transport_socket(&mut envoy);
    let wrapper = Box::new(TransportSocketWrapper { socket });
    Box::into_raw(wrapper) as abi::envoy_dynamic_module_type_transport_socket_module_ptr
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_transport_socket_new", panic);
    std::ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_destroy(
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = transport_socket_module_ptr as *mut TransportSocketWrapper;
    let _ = unsafe { Box::from_raw(wrapper) };
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_transport_socket_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_set_callbacks(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper.socket.on_set_callbacks(&mut envoy);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_set_callbacks",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_on_connected(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper.socket.on_connected(&mut envoy);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_on_connected",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_do_read(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let io = wrapper.socket.on_do_read(&mut envoy);
    io.into()
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_transport_socket_do_read", panic);
    IoResult::close(0, false).into()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_do_write(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let io = wrapper.socket.on_do_write(&mut envoy, end_stream);
    io.into()
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_transport_socket_do_write", panic);
    IoResult::close(0, false).into()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_close(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  event: abi::envoy_dynamic_module_type_network_connection_event,
  abort_reset: bool,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper
      .socket
      .on_close(&mut envoy, event.into(), abort_reset);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_transport_socket_close", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_get_protocol(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(transport_socket_module_ptr as *const TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let s = wrapper.socket.get_protocol(&mut envoy);
    fill_string_buffer_out(&GET_PROTOCOL_BUF, result, &s);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_get_protocol",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_get_failure_reason(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  result: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(transport_socket_module_ptr as *const TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let s = wrapper.socket.get_failure_reason(&mut envoy);
    fill_string_buffer_out(&GET_FAILURE_REASON_BUF, result, &s);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_get_failure_reason",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_can_flush_close(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(transport_socket_module_ptr as *const TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper.socket.can_flush_close(&mut envoy)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_can_flush_close",
      panic,
    );
    false
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_start_secure_transport(
  transport_socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  transport_socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper.socket.start_secure_transport(&mut envoy)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_transport_socket_start_secure_transport",
      panic,
    );
    false
  })
}
