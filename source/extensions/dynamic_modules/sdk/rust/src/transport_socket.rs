//! Transport socket support for dynamic modules.
//!
//! This module provides traits and types for implementing custom transport sockets as dynamic
//! modules. A transport socket performs I/O and participates in connection lifecycle for TCP
//! connections in Envoy.

use crate::{abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, wrap_into_c_void_ptr};
use std::cell::RefCell;
use std::ffi::c_void;
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
pub trait EnvoyTransportSocket {
  /// Returns the raw I/O handle for the connection, if one is exposed to the module.
  fn get_io_handle(&self) -> Option<*mut c_void>;
  /// Returns the native OS file descriptor for the I/O handle, or `None` if unavailable.
  fn io_handle_fd(&self, io_handle: *mut c_void) -> Option<i32>;
  /// Reads from the raw I/O handle into `buffer`. Returns `Ok(bytes_read)` on success.
  fn io_handle_read(&self, io_handle: *mut c_void, buffer: &mut [u8]) -> Result<usize, i64>;
  /// Writes to the raw I/O handle from `data`. Returns `Ok(bytes_written)` on success.
  fn io_handle_write(&self, io_handle: *mut c_void, data: &[u8]) -> Result<usize, i64>;
  /// Drains `length` bytes from the start of the read buffer.
  fn read_buffer_drain(&self, length: usize);
  /// Appends `data` to the read buffer.
  fn read_buffer_add(&self, data: &[u8]);
  /// Returns the current length of the read buffer.
  fn read_buffer_length(&self) -> usize;
  /// Drains `length` bytes from the start of the write buffer.
  fn write_buffer_drain(&self, length: usize);
  /// Fills `slices_out` with up to its length write-buffer slices. Returns how many slices were
  /// written.
  fn write_buffer_get_slices(
    &self,
    slices_out: &mut [abi::envoy_dynamic_module_type_envoy_buffer],
  ) -> usize;
  /// Returns the current length of the write buffer.
  fn write_buffer_length(&self) -> usize;
  /// Raises `event` on the underlying connection.
  fn raise_event(&self, event: ConnectionEvent);
  /// Returns whether Envoy expects the read buffer to be drained for flow-control reasons.
  fn should_drain_read_buffer(&self) -> bool;
  /// Requests that a future event-loop iteration schedules a read.
  fn set_is_readable(&self);
  /// Flushes pending write data toward the transport.
  fn flush_write_buffer(&self);
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
}

// SAFETY: The raw pointer is an opaque handle to an Envoy-side transport socket that is only used
// on the connection's thread as required by the ABI.
unsafe impl Send for EnvoyTransportSocketImpl {}
unsafe impl Sync for EnvoyTransportSocketImpl {}

#[allow(clippy::not_unsafe_ptr_arg_deref)]
impl EnvoyTransportSocket for EnvoyTransportSocketImpl {
  fn get_io_handle(&self) -> Option<*mut c_void> {
    let p = unsafe { abi::envoy_dynamic_module_callback_transport_socket_get_io_handle(self.raw) };
    if p.is_null() {
      None
    } else {
      Some(p)
    }
  }

  fn io_handle_fd(&self, io_handle: *mut c_void) -> Option<i32> {
    let fd = unsafe { abi::envoy_dynamic_module_callback_transport_socket_io_handle_fd(io_handle) };
    if fd < 0 {
      None
    } else {
      Some(fd)
    }
  }

  fn io_handle_read(&self, io_handle: *mut c_void, buffer: &mut [u8]) -> Result<usize, i64> {
    let mut bytes_read: usize = 0;
    let rc = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_read(
        io_handle,
        buffer.as_mut_ptr().cast(),
        buffer.len(),
        &mut bytes_read,
      )
    };
    if rc == 0 {
      Ok(bytes_read)
    } else {
      Err(rc)
    }
  }

  fn io_handle_write(&self, io_handle: *mut c_void, data: &[u8]) -> Result<usize, i64> {
    let mut bytes_written: usize = 0;
    let rc = unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_io_handle_write(
        io_handle,
        data.as_ptr().cast(),
        data.len(),
        &mut bytes_written,
      )
    };
    if rc == 0 {
      Ok(bytes_written)
    } else {
      Err(rc)
    }
  }

  fn read_buffer_drain(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_read_buffer_drain(self.raw, length);
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

  fn read_buffer_length(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_read_buffer_length(self.raw) }
  }

  fn write_buffer_drain(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_drain(self.raw, length);
    }
  }

  fn write_buffer_get_slices(
    &self,
    slices_out: &mut [abi::envoy_dynamic_module_type_envoy_buffer],
  ) -> usize {
    if slices_out.is_empty() {
      return 0;
    }
    let mut total: usize = 0;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        std::ptr::null_mut(),
        &mut total,
      );
    }
    let want = total.min(slices_out.len());
    let mut count = want;
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
        self.raw,
        slices_out.as_mut_ptr(),
        &mut count,
      );
    }
    count
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

  fn flush_write_buffer(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_flush_write_buffer(self.raw);
    }
  }
}

/// In-module factory configuration for transport sockets created from this module.
///
/// This trait must be implemented by the module. Implementations must be `Send + Sync` because
/// they are shared across threads during configuration loading.
pub trait TransportSocketFactoryConfig<ETS: EnvoyTransportSocket + ?Sized>: Send + Sync {
  /// Creates a new transport socket instance for a connection.
  fn new_transport_socket(&self, envoy: &mut ETS) -> Box<dyn TransportSocket<ETS>>;
}

/// In-module transport socket instance for a single connection.
///
/// Implementations must be `Send` because connection ownership may move across worker contexts
/// according to Envoy threading rules for transport sockets.
pub trait TransportSocket<ETS: EnvoyTransportSocket + ?Sized>: Send {
  /// Called when Envoy installs callbacks on the transport socket.
  fn on_set_callbacks(&mut self, envoy: &mut ETS);
  /// Called when the transport reports that the connection is established.
  fn on_connected(&mut self, envoy: &mut ETS);
  /// Performs a read/decrypt step into the read buffer.
  fn on_do_read(&mut self, envoy: &mut ETS) -> IoResult;
  /// Performs an encrypt/write step from the write buffer.
  fn on_do_write(&mut self, envoy: &mut ETS, end_stream: bool) -> IoResult;
  /// Called when the transport socket is closed.
  fn on_close(&mut self, envoy: &mut ETS, event: ConnectionEvent);
  /// Returns the negotiated application protocol, if any.
  fn get_protocol(&self, envoy: &mut ETS) -> String;
  /// Returns a human-readable failure reason, if any.
  fn get_failure_reason(&self, envoy: &mut ETS) -> String;
  /// Returns whether the socket may flush and close.
  fn can_flush_close(&self, envoy: &mut ETS) -> bool;
}

// -- Internal Implementation Types --

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

// -- FFI Event Hook Implementations --

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
    let name_bytes =
      unsafe { std::slice::from_raw_parts(socket_name.ptr as *const u8, socket_name.length) };
    let name_str = match std::str::from_utf8(name_bytes) {
      Ok(s) => s,
      Err(_) => {
        crate::envoy_log_error!("transport socket factory config: socket_name is not valid UTF-8.");
        return std::ptr::null();
      },
    };
    let config_slice =
      unsafe { std::slice::from_raw_parts(socket_config.ptr as *const u8, socket_config.length) };
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
    log_panic(
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
    log_panic(
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
    log_panic("envoy_dynamic_module_on_transport_socket_new", panic);
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
    log_panic("envoy_dynamic_module_on_transport_socket_destroy", panic);
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
    log_panic(
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
    log_panic(
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
    log_panic("envoy_dynamic_module_on_transport_socket_do_read", panic);
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
  _write_buffer_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_io_result {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    let io = wrapper.socket.on_do_write(&mut envoy, end_stream);
    io.into()
  }))
  .unwrap_or_else(|panic| {
    log_panic("envoy_dynamic_module_on_transport_socket_do_write", panic);
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
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &mut *(transport_socket_module_ptr as *mut TransportSocketWrapper) };
    let mut envoy = EnvoyTransportSocketImpl::new(transport_socket_envoy_ptr);
    wrapper.socket.on_close(&mut envoy, event.into());
  }))
  .map_err(|panic| {
    log_panic("envoy_dynamic_module_on_transport_socket_close", panic);
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
    log_panic(
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
    log_panic(
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
    log_panic(
      "envoy_dynamic_module_on_transport_socket_can_flush_close",
      panic,
    );
    false
  })
}

/// Log a panic caught at an FFI boundary.
fn log_panic(function_name: &str, panic: Box<dyn std::any::Any + Send>) {
  crate::envoy_log_error!(
    "{}: caught panic: {}",
    function_name,
    crate::panic_payload_to_string(panic)
  );
}
