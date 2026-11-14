//! Socket interface support for dynamic modules.
//!
//! A socket interface decides which socket Envoy creates for the addresses that select it. A
//! dynamic module creates the socket itself and hands the file descriptor to Envoy, and may also
//! implement the socket's data path: Envoy then reads and writes through the module, and the
//! module wakes the worker's event loop with a readiness handle.
//!
//! Register a factory through the `socket_interface:` arm of
//! [`crate::declare_all_init_functions!`] and return a [`SocketInterfaceConfig`] from it. The
//! [`SocketIo`] object returned from [`SocketInterfaceConfig::socket`] represents one created
//! socket, and its cleanup belongs in its [`Drop`] implementation.

use crate::{abi, ffi_export, NewSocketInterfaceConfigFunction};
use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::ops::BitOr;
use std::ptr;

/// The type of the socket, mirroring `Network::Socket::Type`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketType {
  Stream,
  Datagram,
}

/// The arguments of a socket creation request.
pub struct SocketCreation {
  /// The type of the socket.
  pub socket_type: SocketType,
  /// The IP address or, for AF_UNIX sockets, the path. Empty when Envoy creates the socket
  /// without a concrete address.
  pub address: String,
  /// The port of the address, or 0 for AF_UNIX sockets and empty addresses.
  pub port: u32,
  /// The address family, such as `libc::AF_INET`, `libc::AF_INET6` or `libc::AF_UNIX`.
  pub domain: i32,
  /// Whether the socket must be IPv6-only.
  pub socket_v6only: bool,
  /// Whether MPTCP is enabled on the socket. Only valid for stream sockets on Linux.
  pub mptcp_enabled: bool,
  /// The maximum size of the cache of addresses associated with packets received by this
  /// socket. 0 means no cache. Only valid for datagram sockets.
  pub max_addresses_cache_size: u64,
}

/// The readiness events reported with [`EnvoySocketIoReadyHandle::ready`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SocketIoEvents(u32);

impl SocketIoEvents {
  /// The socket is readable.
  pub const READ: Self = Self(0x1);
  /// The socket is writable.
  pub const WRITE: Self = Self(0x2);

  #[must_use]
  pub const fn union(self, other: Self) -> Self {
    Self(self.0 | other.0)
  }
}

impl BitOr for SocketIoEvents {
  type Output = Self;

  fn bitor(self, rhs: Self) -> Self {
    self.union(rhs)
  }
}

/// The outcome of the I/O methods of [`SocketIo`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketIoStatus {
  /// The operation completed with the given number of bytes transferred.
  Bytes(usize),
  /// No data was available right now. Envoy treats this as EAGAIN.
  Again,
  /// The socket hit an unrecoverable error. Envoy closes the connection.
  Error,
}

/// A handle to report the readiness of a socket created by the module.
///
/// Envoy hands one handle per socket to [`SocketIo::set_ready_handle`]. Reporting readiness
/// schedules the I/O hooks of the socket on its worker, so a module that buffers data outside
/// the kernel must call [`EnvoySocketIoReadyHandle::ready`] when its buffered bytes become
/// readable or writable.
///
/// The handle can be used from any thread while the socket is open. Dropping it deletes the
/// Envoy-side handle, so a `SocketIo` implementation must stop reporting readiness before it
/// drops the handle.
pub struct EnvoySocketIoReadyHandle {
  raw_ptr: abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
}

// SAFETY: the handle wraps an opaque Envoy-side readiness handle whose `ready` and `delete` are
// safe to call from any thread by ABI contract.
unsafe impl Send for EnvoySocketIoReadyHandle {}
unsafe impl Sync for EnvoySocketIoReadyHandle {}

impl EnvoySocketIoReadyHandle {
  /// Wraps the raw handle passed by Envoy. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `raw_ptr` must be the handle Envoy passed to
  /// `envoy_dynamic_module_on_socket_interface_set_socket_handle`.
  #[doc(hidden)]
  pub unsafe fn new(
    raw_ptr: abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
  ) -> Self {
    Self { raw_ptr }
  }

  /// Reports that the socket is ready for the given events. This is a no-op once the socket is
  /// closed.
  pub fn ready(&self, events: SocketIoEvents) {
    unsafe { abi::envoy_dynamic_module_callback_socket_interface_io_ready(self.raw_ptr, events.0) };
  }
}

impl Drop for EnvoySocketIoReadyHandle {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_socket_interface_io_ready_delete(self.raw_ptr);
    }
  }
}

/// The per-socket object of a socket created by a dynamic module.
///
/// Envoy calls the I/O methods on the worker thread of the socket, one call at a time. Resource
/// cleanup belongs in a [`Drop`] implementation, which runs when Envoy closes the socket and must
/// stop the threads the socket started, since the module may be unloaded once it returns.
pub trait SocketIo: Send {
  /// Called once per socket, right after Envoy wraps it in its I/O handle. The implementation
  /// must store the handle to report readiness with [`EnvoySocketIoReadyHandle::ready`], or drop
  /// it if it does not need readiness reporting; dropping it deletes the Envoy-side handle.
  fn set_ready_handle(&mut self, handle: EnvoySocketIoReadyHandle);

  /// Called when Envoy reads from the socket. The implementation reads from the socket however
  /// it chooses and writes the bytes into the slices, which are only valid during this call.
  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]]) -> SocketIoStatus;

  /// Called when Envoy writes to the socket. The buffers are only valid during this call.
  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus;

  /// Called when Envoy shuts down one side of the socket. `how` is the shutdown operation, such
  /// as `libc::SHUT_RD`, `libc::SHUT_WR` or `libc::SHUT_RDWR`. The default implementation does
  /// nothing.
  fn io_shutdown(&mut self, _how: i32) -> SocketIoStatus {
    SocketIoStatus::Bytes(0)
  }
}

/// A socket interface implemented by a dynamic module.
///
/// A single instance is created per socket interface configuration.
/// [`SocketInterfaceConfig::socket`] runs concurrently on several threads, so it must be
/// thread-safe and must not block. Resource cleanup belongs in a [`Drop`] implementation, which
/// must stop the threads the interface started, since the module may be unloaded once it returns.
pub trait SocketInterfaceConfig: Send + Sync {
  /// Returns whether the interface supports the address family, such as `libc::AF_INET`,
  /// `libc::AF_INET6` or `libc::AF_UNIX`.
  fn ip_family_supported(&self, domain: i32) -> bool;

  /// Creates a socket for the request. The implementation creates the socket itself, sets it to
  /// non-blocking mode, and returns its file descriptor with a new [`SocketIo`] object. From the
  /// point this returns, Envoy owns the file descriptor and closes it; the implementation must
  /// not close it.
  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult;

  /// Called by the main thread when the server has finished initializing. The default
  /// implementation does nothing.
  fn on_server_initialized(&self) {}

  /// Called by each worker thread when it is ready to accept connections. The default
  /// implementation does nothing.
  fn on_worker_thread_initialized(&self) {}
}

/// The outcome of [`SocketInterfaceConfig::socket`].
pub enum SocketCreationResult {
  /// The socket was created. The file descriptor is handed to Envoy, which owns it from then on.
  Created {
    /// The file descriptor of the created socket. It must be non-blocking.
    fd: i64,
    /// The per-socket object of the created socket.
    io: Box<dyn SocketIo>,
  },
  /// The socket was created with its connection already established, so Envoy must not connect
  /// it again. Only valid for sockets handled by the I/O hooks.
  AlreadyConnected {
    /// The file descriptor of the connected socket. It must be non-blocking.
    fd: i64,
    /// The per-socket object of the created socket.
    io: Box<dyn SocketIo>,
  },
  /// The interface does not support the address family. Envoy creates the socket through its
  /// standard socket interface instead.
  UnsupportedFamily,
  /// The socket could not be created.
  Error,
}

impl std::fmt::Debug for SocketCreationResult {
  fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
    match self {
      // The per-socket objects are opaque, so only their presence is reported.
      SocketCreationResult::Created { fd, .. } => {
        f.debug_struct("Created").field("fd", fd).finish()
      },
      SocketCreationResult::AlreadyConnected { fd, .. } => {
        f.debug_struct("AlreadyConnected").field("fd", fd).finish()
      },
      SocketCreationResult::UnsupportedFamily => write!(f, "UnsupportedFamily"),
      SocketCreationResult::Error => write!(f, "Error"),
    }
  }
}

/// The socket interface that a module configuration holds.
struct ModuleSocketInterface {
  config: Box<dyn SocketInterfaceConfig>,
}

/// The per-socket state of one socket created by the module.
struct ModuleSocketIo {
  io: Box<dyn SocketIo>,
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_config_new(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_socket_interface_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> *const c_void {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes. The
    // helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed UTF-8.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    envoy_dynamic_module_on_socket_interface_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      crate::NEW_SOCKET_INTERFACE_CONFIG_FUNCTION.get(),
    )
  }
  on_panic = ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_socket_interface_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_socket_interface_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: Option<&NewSocketInterfaceConfigFunction>,
) -> *const c_void {
  match new_fn.and_then(|factory| factory(name, config)) {
    Some(config) => crate::wrap_into_c_void_ptr!(ModuleSocketInterface { config }),
    None => ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr: *const c_void) {
    drop(Box::from_raw(config_ptr as *mut ModuleSocketInterface));
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_ip_family_supported(
    config_ptr: *const c_void,
    domain: i32,
  ) -> bool {
    let module = &*(config_ptr as *const ModuleSocketInterface);
    module.config.ip_family_supported(domain)
  }
  on_panic = false
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_socket(
    config_ptr: *const c_void,
    socket_type: abi::envoy_dynamic_module_type_socket_interface_socket_type,
    address: abi::envoy_dynamic_module_type_envoy_buffer,
    port: u32,
    domain: i32,
    socket_v6only: bool,
    options: *const abi::envoy_dynamic_module_type_socket_interface_creation_options,
    fd_out: *mut i64,
    socket_module_ptr_out: *mut abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  ) -> abi::envoy_dynamic_module_type_socket_interface_creation_result {
    let module = &*(config_ptr as *const ModuleSocketInterface);
    // SAFETY: `address` is a protobuf string (UTF-8 by contract). The helper tolerates
    // `(nullptr, 0)` empty inputs.
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    let creation = SocketCreation {
      socket_type: match socket_type {
        abi::envoy_dynamic_module_type_socket_interface_socket_type::Stream => SocketType::Stream,
        abi::envoy_dynamic_module_type_socket_interface_socket_type::Datagram => {
          SocketType::Datagram
        },
      },
      address: address_str.as_ref().to_string(),
      port,
      domain,
      socket_v6only,
      // SAFETY: Envoy passes a valid pointer to the creation options.
      mptcp_enabled: unsafe { (*options).mptcp_enabled },
      // SAFETY: Envoy passes a valid pointer to the creation options.
      max_addresses_cache_size: unsafe { (*options).max_addresses_cache_size },
    };
    match module.config.socket(&creation) {
      SocketCreationResult::Created { fd, io } => {
        *fd_out = fd;
        *socket_module_ptr_out = crate::wrap_into_c_void_ptr!(ModuleSocketIo { io });
        abi::envoy_dynamic_module_type_socket_interface_creation_result::OK
      },
      SocketCreationResult::AlreadyConnected { fd, io } => {
        *fd_out = fd;
        *socket_module_ptr_out = crate::wrap_into_c_void_ptr!(ModuleSocketIo { io });
        abi::envoy_dynamic_module_type_socket_interface_creation_result::ALREADY_CONNECTED
      },
      SocketCreationResult::UnsupportedFamily => {
        abi::envoy_dynamic_module_type_socket_interface_creation_result::UNSUPPORTED_FAMILY
      },
      SocketCreationResult::Error => {
        abi::envoy_dynamic_module_type_socket_interface_creation_result::ERROR
      },
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_creation_result::ERROR
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_socket_close(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  ) {
    drop(Box::from_raw(
      socket_module_ptr as *mut ModuleSocketIo,
    ));
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_set_socket_handle(
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    io_ready_handle_module_ptr:
      abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
  ) {
    let module = &mut *(socket_module_ptr as *mut ModuleSocketIo);
    // SAFETY: Envoy passes the readiness handle it created for this socket, and the handle's
    // lifetime is module-managed from this point on.
    let handle = unsafe { EnvoySocketIoReadyHandle::new(io_ready_handle_module_ptr) };
    module.io.set_ready_handle(handle);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_read(
    config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    slices: *mut abi::envoy_dynamic_module_type_socket_interface_io_slice,
    num_slices: usize,
    bytes_transferred: *mut u64,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let _ = config_ptr;
    let module = &mut *(socket_module_ptr as *mut ModuleSocketIo);
    // SAFETY: Envoy passes a valid array of `num_slices` slices owned by its read buffer.
    let abi_slices: &[abi::envoy_dynamic_module_type_socket_interface_io_slice] =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(slices as *const _, num_slices) };
    let mut rust_slices: Vec<&mut [MaybeUninit<u8>]> = Vec::with_capacity(abi_slices.len());
    for slice in abi_slices {
      // An empty slice may carry a null pointer, which `from_raw_parts_mut` rejects even for a
      // length of zero, so empty slices are passed through without materializing a pointer.
      rust_slices.push(if slice.length == 0 {
        &mut []
      } else {
        unsafe { std::slice::from_raw_parts_mut(slice.ptr as *mut MaybeUninit<u8>, slice.length) }
      });
    }
    match module.io.io_read(&mut rust_slices) {
      SocketIoStatus::Bytes(bytes) => {
        *bytes_transferred = bytes as u64;
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      SocketIoStatus::Again => abi::envoy_dynamic_module_type_socket_interface_io_status::Again,
      SocketIoStatus::Error => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_write(
    config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    buffers: *const abi::envoy_dynamic_module_type_envoy_buffer,
    num_buffers: usize,
    bytes_transferred: *mut u64,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let _ = config_ptr;
    let module = &mut *(socket_module_ptr as *mut ModuleSocketIo);
    // SAFETY: Envoy passes a valid array of `num_buffers` buffers owned by its write buffer.
    let abi_buffers =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(buffers, num_buffers) };
    let mut rust_buffers: Vec<&[u8]> = Vec::with_capacity(abi_buffers.len());
    for buffer in abi_buffers {
      // An empty buffer may carry a null pointer, which `from_raw_parts` rejects even for a length
      // of zero, so empty buffers are passed through without materializing a pointer.
      rust_buffers.push(if buffer.length == 0 {
        &[]
      } else {
        unsafe { std::slice::from_raw_parts(buffer.ptr as *const u8, buffer.length) }
      });
    }
    match module.io.io_write(&rust_buffers) {
      SocketIoStatus::Bytes(bytes) => {
        *bytes_transferred = bytes as u64;
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      SocketIoStatus::Again => abi::envoy_dynamic_module_type_socket_interface_io_status::Again,
      SocketIoStatus::Error => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_shutdown(
    config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    how: i32,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let _ = config_ptr;
    let module = &mut *(socket_module_ptr as *mut ModuleSocketIo);
    match module.io.io_shutdown(how) {
      SocketIoStatus::Bytes(_) => {
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      SocketIoStatus::Again => abi::envoy_dynamic_module_type_socket_interface_io_status::Again,
      SocketIoStatus::Error => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_on_server_initialized(
    config_ptr: *const c_void,
  ) {
    let module = &*(config_ptr as *const ModuleSocketInterface);
    module.config.on_server_initialized();
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_on_worker_thread_initialized(
    config_ptr: *const c_void,
  ) {
    let module = &*(config_ptr as *const ModuleSocketInterface);
    module.config.on_worker_thread_initialized();
  }
}

#[cfg(test)]
mod tests {
  use super::*;
  use crate::abi::{
    envoy_dynamic_module_type_socket_interface_creation_options,
    envoy_dynamic_module_type_socket_interface_creation_result,
    envoy_dynamic_module_type_socket_interface_io_slice,
    envoy_dynamic_module_type_socket_interface_io_status,
    envoy_dynamic_module_type_socket_interface_socket_type,
  };
  use crate::mod_test::{MOCK_SOCKET_IO_READY_CALLS, MOCK_SOCKET_IO_READY_DELETES};
  use std::sync::{Arc, Mutex};

  type Calls = Arc<Mutex<Vec<&'static str>>>;

  // A per-socket object that records the calls Envoy makes and panics on demand.
  struct TestIo {
    calls: Calls,
    panic_on: &'static str,
  }

  impl Drop for TestIo {
    fn drop(&mut self) {
      self.calls.lock().unwrap().push("drop");
    }
  }

  impl SocketIo for TestIo {
    fn set_ready_handle(&mut self, handle: EnvoySocketIoReadyHandle) {
      self.calls.lock().unwrap().push("set_ready_handle");
      handle.ready(SocketIoEvents::READ);
      // Dropping the handle deletes the Envoy-side handle.
    }

    fn io_read(&mut self, _slices: &mut [&mut [MaybeUninit<u8>]]) -> SocketIoStatus {
      self.calls.lock().unwrap().push("io_read");
      if self.panic_on == "io_read" {
        panic!("io_read panic");
      }
      SocketIoStatus::Bytes(3)
    }

    fn io_write(&mut self, _buffers: &[&[u8]]) -> SocketIoStatus {
      self.calls.lock().unwrap().push("io_write");
      if self.panic_on == "io_write" {
        panic!("io_write panic");
      }
      SocketIoStatus::Again
    }

    fn io_shutdown(&mut self, _how: i32) -> SocketIoStatus {
      self.calls.lock().unwrap().push("io_shutdown");
      if self.panic_on == "io_shutdown" {
        panic!("io_shutdown panic");
      }
      SocketIoStatus::Error
    }
  }

  // A socket interface config that creates sockets with a caller-supplied result.
  struct TestConfig {
    calls: Calls,
    panic_on_socket: bool,
    make_result: Box<dyn Fn(&Calls) -> SocketCreationResult + Send + Sync>,
  }

  impl SocketInterfaceConfig for TestConfig {
    fn ip_family_supported(&self, domain: i32) -> bool {
      domain == 2 // AF_INET
    }

    fn socket(&self, _creation: &SocketCreation) -> SocketCreationResult {
      self.calls.lock().unwrap().push("socket");
      if self.panic_on_socket {
        panic!("socket panic");
      }
      (self.make_result)(&self.calls)
    }
  }

  fn new_io(calls: &Calls, panic_on: &'static str) -> Box<dyn SocketIo> {
    Box::new(TestIo {
      calls: calls.clone(),
      panic_on,
    })
  }

  fn new_config_ptr(
    calls: Calls,
    panic_on_socket: bool,
    make_result: Box<dyn Fn(&Calls) -> SocketCreationResult + Send + Sync>,
  ) -> *const c_void {
    Box::into_raw(Box::new(ModuleSocketInterface {
      config: Box::new(TestConfig {
        calls,
        panic_on_socket,
        make_result,
      }),
    })) as *const c_void
  }

  fn envoy_buffer(bytes: &[u8]) -> abi::envoy_dynamic_module_type_envoy_buffer {
    abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: bytes.as_ptr() as *const _,
      length: bytes.len(),
    }
  }

  fn call_socket(
    config_ptr: *const c_void,
    fd_out: &mut i64,
    socket_ptr_out: &mut abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  ) -> envoy_dynamic_module_type_socket_interface_creation_result {
    let options = envoy_dynamic_module_type_socket_interface_creation_options {
      mptcp_enabled: false,
      max_addresses_cache_size: 0,
    };
    unsafe {
      envoy_dynamic_module_on_socket_interface_socket(
        config_ptr,
        envoy_dynamic_module_type_socket_interface_socket_type::Stream,
        envoy_buffer(b"127.0.0.1"),
        8080,
        2, // AF_INET
        false,
        &options,
        fd_out,
        socket_ptr_out,
      )
    }
  }

  #[test]
  fn test_config_new_impl_round_trip() {
    fn factory(_name: &str, _config: &[u8]) -> Option<Box<dyn SocketInterfaceConfig>> {
      Some(Box::new(TestConfig {
        calls: Arc::new(Mutex::new(Vec::new())),
        panic_on_socket: false,
        make_result: Box::new(|_calls| SocketCreationResult::Error),
      }))
    }
    let config_ptr = envoy_dynamic_module_on_socket_interface_config_new_impl(
      "name",
      b"config",
      Some(&(factory as NewSocketInterfaceConfigFunction)),
    );
    assert!(!config_ptr.is_null());
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

    fn rejecting(_name: &str, _config: &[u8]) -> Option<Box<dyn SocketInterfaceConfig>> {
      None
    }
    assert!(envoy_dynamic_module_on_socket_interface_config_new_impl(
      "name",
      b"config",
      Some(&(rejecting as NewSocketInterfaceConfigFunction)),
    )
    .is_null());
    assert!(
      envoy_dynamic_module_on_socket_interface_config_new_impl("name", b"config", None).is_null()
    );
  }

  #[test]
  fn test_socket_lifecycle_with_created_socket() {
    let calls: Calls = Arc::new(Mutex::new(Vec::new()));
    let config_ptr = new_config_ptr(
      calls.clone(),
      false,
      Box::new(|calls| SocketCreationResult::Created {
        fd: 7,
        io: new_io(calls, ""),
      }),
    );

    assert!(unsafe { envoy_dynamic_module_on_socket_interface_ip_family_supported(config_ptr, 2) });
    assert!(!unsafe {
      envoy_dynamic_module_on_socket_interface_ip_family_supported(config_ptr, 10)
    });

    let mut fd: i64 = 0;
    let mut socket_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr =
      ptr::null_mut();
    let result = call_socket(config_ptr, &mut fd, &mut socket_ptr);
    assert_eq!(
      result,
      envoy_dynamic_module_type_socket_interface_creation_result::OK
    );
    assert_eq!(fd, 7);
    assert!(!socket_ptr.is_null());
    assert!(calls.lock().unwrap().contains(&"socket"));

    // Envoy hands the readiness handle to the io object, which reports readiness through the
    // mocked callback; dropping the handle deletes the Envoy-side handle.
    let handle =
      0x1000usize as abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr;
    unsafe { envoy_dynamic_module_on_socket_interface_set_socket_handle(socket_ptr, handle) };
    {
      let recorded = calls.lock().unwrap();
      assert!(recorded.contains(&"set_ready_handle"));
    }
    assert_eq!(
      *MOCK_SOCKET_IO_READY_CALLS.lock().unwrap(),
      vec![(0x1000, SocketIoEvents::READ.0)]
    );
    assert_eq!(*MOCK_SOCKET_IO_READY_DELETES.lock().unwrap(), vec![0x1000]);

    // io_read writes the bytes into the Envoy slices.
    let mut buf = [0u8; 4];
    let mut slice = envoy_dynamic_module_type_socket_interface_io_slice {
      ptr: buf.as_mut_ptr() as *mut _,
      length: buf.len(),
    };
    let mut transferred: u64 = 0;
    let status = unsafe {
      envoy_dynamic_module_on_socket_interface_io_read(
        ptr::null(),
        socket_ptr,
        &mut slice,
        1,
        &mut transferred,
      )
    };
    assert_eq!(
      status,
      envoy_dynamic_module_type_socket_interface_io_status::Success
    );
    assert_eq!(transferred, 3);

    // io_write reports Again.
    let buffers = [envoy_buffer(b"abc")];
    let status = unsafe {
      envoy_dynamic_module_on_socket_interface_io_write(
        ptr::null(),
        socket_ptr,
        buffers.as_ptr(),
        1,
        &mut transferred,
      )
    };
    assert_eq!(
      status,
      envoy_dynamic_module_type_socket_interface_io_status::Again
    );

    // io_shutdown reports Error.
    let status =
      unsafe { envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), socket_ptr, 1) };
    assert_eq!(
      status,
      envoy_dynamic_module_type_socket_interface_io_status::Error
    );

    // Closing releases the per-socket object.
    unsafe { envoy_dynamic_module_on_socket_interface_socket_close(ptr::null(), socket_ptr) };
    assert!(calls.lock().unwrap().contains(&"drop"));

    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
  }

  #[test]
  fn test_socket_already_connected_result() {
    let calls: Calls = Arc::new(Mutex::new(Vec::new()));
    let config_ptr = new_config_ptr(
      calls.clone(),
      false,
      Box::new(|calls| SocketCreationResult::AlreadyConnected {
        fd: 9,
        io: new_io(calls, ""),
      }),
    );

    let mut fd: i64 = 0;
    let mut socket_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr =
      ptr::null_mut();
    let result = call_socket(config_ptr, &mut fd, &mut socket_ptr);
    assert_eq!(
      result,
      envoy_dynamic_module_type_socket_interface_creation_result::ALREADY_CONNECTED
    );
    assert_eq!(fd, 9);

    unsafe { envoy_dynamic_module_on_socket_interface_socket_close(ptr::null(), socket_ptr) };
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
  }

  #[test]
  fn test_socket_unsupported_family_and_error_results() {
    let calls: Calls = Arc::new(Mutex::new(Vec::new()));
    let unsupported_ptr = new_config_ptr(
      calls.clone(),
      false,
      Box::new(|_calls| SocketCreationResult::UnsupportedFamily),
    );
    let mut fd: i64 = 0;
    let mut socket_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr =
      ptr::null_mut();
    let result = call_socket(unsupported_ptr, &mut fd, &mut socket_ptr);
    assert_eq!(
      result,
      envoy_dynamic_module_type_socket_interface_creation_result::UNSUPPORTED_FAMILY
    );
    // The out-params are untouched when no socket is handed over.
    assert_eq!(fd, 0);
    assert!(socket_ptr.is_null());
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(unsupported_ptr) };

    let error_ptr = new_config_ptr(calls, false, Box::new(|_calls| SocketCreationResult::Error));
    let result = call_socket(error_ptr, &mut fd, &mut socket_ptr);
    assert_eq!(
      result,
      envoy_dynamic_module_type_socket_interface_creation_result::ERROR
    );
    assert_eq!(fd, 0);
    assert!(socket_ptr.is_null());
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(error_ptr) };
  }

  #[test]
  fn test_socket_hooks_recover_from_panics() {
    let calls: Calls = Arc::new(Mutex::new(Vec::new()));

    let mut fd: i64 = 0;
    let mut socket_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr =
      ptr::null_mut();
    let panicking_config_ptr = new_config_ptr(
      calls,
      true,
      Box::new(|_calls| SocketCreationResult::Created {
        fd: 7,
        io: new_io(&Arc::new(Mutex::new(Vec::new())), ""),
      }),
    );
    let result = call_socket(panicking_config_ptr, &mut fd, &mut socket_ptr);
    assert_eq!(
      result,
      envoy_dynamic_module_type_socket_interface_creation_result::ERROR
    );
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(panicking_config_ptr) };

    // The I/O hooks return Error when the io object panics, and the per-socket state stays
    // releasable afterwards.
    let mut slice = envoy_dynamic_module_type_socket_interface_io_slice {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let mut transferred: u64 = 0;
    for panics_on in ["io_read", "io_write", "io_shutdown"] {
      let io_ptr = Box::into_raw(Box::new(ModuleSocketIo {
        io: new_io(&Arc::new(Mutex::new(Vec::new())), panics_on),
      })) as abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr;
      let status = match panics_on {
        "io_read" => unsafe {
          envoy_dynamic_module_on_socket_interface_io_read(
            ptr::null(),
            io_ptr,
            &mut slice,
            1,
            &mut transferred,
          )
        },
        "io_write" => unsafe {
          envoy_dynamic_module_on_socket_interface_io_write(
            ptr::null(),
            io_ptr,
            [].as_ptr(),
            0,
            &mut transferred,
          )
        },
        _ => unsafe {
          envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), io_ptr, 1)
        },
      };
      assert_eq!(
        status,
        envoy_dynamic_module_type_socket_interface_io_status::Error
      );
      unsafe { envoy_dynamic_module_on_socket_interface_socket_close(ptr::null(), io_ptr) };
    }
  }
}
