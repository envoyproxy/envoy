//! Socket interface support for dynamic modules.
//!
//! A socket interface creates the sockets of Envoy's listeners and clusters. Selected as the
//! bootstrap's `default_socket_interface`, the module is asked for every socket except the admin
//! listener's, and for each one either leaves it to Envoy's standard socket interface or creates
//! it and hands Envoy the file descriptor. For a socket it creates, the module chooses a
//! [`SocketHandling`]: Envoy performs the I/O on the kernel file descriptor, delegates it to a
//! [`SocketIo`], or, for a listen socket, takes its connections from a [`SocketAcceptor`].
//!
//! Register a factory through the `socket_interface:` arm of
//! [`crate::declare_all_init_functions!`] and return a [`SocketInterfaceConfig`] from it. Resource
//! cleanup of a per-socket object belongs in its [`Drop`] implementation, which runs when Envoy
//! closes the socket. No method may block.

use crate::{abi, ffi_export, NewSocketInterfaceConfigFunction};
#[cfg(any(test, feature = "mock"))]
use mockall::*;
use smallvec::SmallVec;
use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::ops::BitOr;
use std::ptr;

/// Stream sockets pass at most this many slices per read or write, so the slice lists stay on the
/// stack.
const INLINE_SLICES: usize = 16;

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
  /// The IP address or, for AF_UNIX sockets, the path, which for an abstract socket is `@`
  /// followed by the name. Empty when Envoy creates the socket without a concrete address.
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
  /// The socket is readable, or a listen socket has a connection pending.
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

/// The outcome of the read and write methods of [`SocketIo`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketIoStatus {
  /// The operation completed with the given number of bytes transferred. A read of 0 bytes means
  /// the end of the stream, and Envoy treats a write of 0 bytes as [`SocketIoStatus::Again`].
  Bytes(usize),
  /// No data can be transferred right now. Envoy treats this as EAGAIN.
  Again,
  /// The socket hit an unrecoverable error. Envoy closes the connection.
  Error,
}

/// The outcome of [`SocketIo::io_shutdown`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketShutdownStatus {
  /// The module shut the socket down.
  Done,
  /// The shutdown failed.
  Error,
  /// The module does not shut the socket down itself, and Envoy calls `shutdown(2)` on the file
  /// descriptor, which sends the FIN of a half-close.
  NotImplemented,
}

/// A handle to report the readiness of a socket created by the module.
///
/// Envoy hands one handle per socket to [`SocketIo::set_ready_handle`] or
/// [`SocketAcceptor::set_ready_handle`]. Reporting readiness schedules the socket's hooks on its
/// worker, so a module that buffers data outside the kernel must call
/// [`EnvoySocketIoReadyHandle::ready`] when its buffered bytes become readable or writable, and a
/// [`SocketAcceptor`] when it has a connection pending. Envoy keeps a reported read pending until a
/// read or accept returns [`SocketIoStatus::Again`], including across the socket not yet being
/// registered with its worker or its reads being disabled.
///
/// The handle can be used from any thread, at any time: it holds no reference to the socket, so
/// calling `ready` or dropping it after the socket closed is harmless. Dropping it deletes the
/// Envoy-side handle.
#[cfg_attr(any(test, feature = "mock"), automock)]
pub trait EnvoySocketIoReadyHandle: Send + Sync {
  /// Reports that the socket is ready for the given events. This is a no-op once the socket is
  /// closed.
  fn ready(&self, events: SocketIoEvents);
}

/// This implements the [`EnvoySocketIoReadyHandle`] trait with the given raw pointer to the
/// Envoy readiness handle.
struct EnvoySocketIoReadyHandleImpl {
  raw_ptr: abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
}

// SAFETY: the handle wraps an opaque Envoy-side readiness handle whose `ready` and `delete` are
// safe to call from any thread by ABI contract.
unsafe impl Send for EnvoySocketIoReadyHandleImpl {}
unsafe impl Sync for EnvoySocketIoReadyHandleImpl {}

impl EnvoySocketIoReadyHandleImpl {
  /// Wraps the raw handle passed by Envoy.
  ///
  /// # Safety
  ///
  /// `raw_ptr` must be the handle Envoy passed to
  /// `envoy_dynamic_module_on_socket_interface_set_socket_handle`.
  unsafe fn new(
    raw_ptr: abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
  ) -> Self {
    Self { raw_ptr }
  }
}

impl EnvoySocketIoReadyHandle for EnvoySocketIoReadyHandleImpl {
  fn ready(&self, events: SocketIoEvents) {
    unsafe { abi::envoy_dynamic_module_callback_socket_interface_io_ready(self.raw_ptr, events.0) };
  }
}

impl Drop for EnvoySocketIoReadyHandleImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_socket_interface_io_ready_delete(self.raw_ptr);
    }
  }
}

/// The per-socket object of a socket whose reads, writes and shutdown the module performs.
///
/// Envoy calls its methods on the worker thread of the socket, one call at a time. Only stream
/// sockets can delegate their I/O, and Envoy rejects a delegated socket while hot restart is
/// enabled.
pub trait SocketIo: Send {
  /// Called once per socket, right after Envoy wraps it. The implementation stores the handle to
  /// report readiness the kernel does not signal, or drops it if it does not need to.
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>);

  /// Called when Envoy reads from the socket. The implementation fills the slices in order and
  /// stops at the first slice it does not fill completely, since Envoy commits the byte count as
  /// one prefix across the slices. With `peek`, as listener filters use, it copies the bytes
  /// without consuming them. The slices are only valid during this call.
  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]], peek: bool) -> SocketIoStatus;

  /// Called when Envoy writes to the socket. The implementation takes a prefix of the bytes, in
  /// order across the buffers, and stops at the first buffer it does not take completely. After
  /// [`SocketIoStatus::Again`], it reports [`SocketIoEvents::WRITE`] once it can take more bytes,
  /// unless the kernel signals it. The buffers are only valid during this call.
  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus;

  /// Called when Envoy shuts down one side of the socket. `how` is the shutdown operation, such
  /// as `libc::SHUT_RD`, `libc::SHUT_WR` or `libc::SHUT_RDWR`. The default implementation returns
  /// [`SocketShutdownStatus::NotImplemented`], so Envoy shuts down the file descriptor.
  fn io_shutdown(&mut self, _how: i32) -> SocketShutdownStatus {
    SocketShutdownStatus::NotImplemented
  }

  /// Called when Envoy copies the socket. Returns the copy's file descriptor, for example from
  /// `dup(2)`, and its per-socket object. The default implementation returns `None`, which makes
  /// the copy a socket whose operations fail.
  fn duplicate(&mut self) -> Option<(i64, Box<dyn SocketIo>)> {
    None
  }
}

/// An address the module reports for a connection it hands over.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SocketAddress {
  /// The IP address or, for AF_UNIX, the path.
  pub address: String,
  /// The port, or 0 for AF_UNIX.
  pub port: u32,
}

/// The outcome of [`SocketAcceptor::accept`].
pub enum SocketAcceptResult {
  /// A connection was handed over. Envoy owns the file descriptor from then on and runs it through
  /// the listener's filter chains like any accepted connection.
  Accepted {
    /// The file descriptor of the connection. It must be a non-blocking, connected stream socket
    /// of the listen socket's address family.
    fd: i64,
    /// How Envoy performs the connection's I/O, which is [`SocketHandling::Kernel`] or
    /// [`SocketHandling::DelegatedIo`].
    handling: SocketHandling,
    /// The local address of the connection, for example the destination of a connection the
    /// module received over its own transport, which Envoy uses for filter chain matching. Envoy
    /// reads it from the file descriptor when this is `None`.
    local_address: Option<SocketAddress>,
    /// The peer address of the connection. Envoy reads it from the file descriptor when this is
    /// `None`.
    peer_address: Option<SocketAddress>,
  },
  /// No connection is pending.
  Again,
  /// The accept failed.
  Error,
}

/// The per-socket object of a listen socket whose connections the module hands over.
///
/// Envoy never binds the socket or listens on it, and calls [`SocketAcceptor::accept`] on the
/// socket's worker after the module reports [`SocketIoEvents::READ`] on its readiness handle, until
/// it returns [`SocketAcceptResult::Again`]. Envoy rejects such a socket while hot restart is
/// enabled.
pub trait SocketAcceptor: Send {
  /// Called once per socket, right after Envoy wraps it. The implementation stores the handle and
  /// reports [`SocketIoEvents::READ`] on it whenever a connection is pending.
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>);

  /// Called in place of `accept(2)` to hand Envoy the next pending connection.
  fn accept(&mut self) -> SocketAcceptResult;

  /// Called when Envoy copies the listen socket, for example for each worker when the listener
  /// does not use `reuse_port`. Returns the copy's file descriptor and per-socket object, each of
  /// which hands over its own share of the connections. The default implementation returns
  /// `None`, which makes the copy a socket whose operations fail.
  fn duplicate(&mut self) -> Option<(i64, Box<dyn SocketAcceptor>)> {
    None
  }
}

/// How Envoy handles a socket the module creates or a connection it hands over.
pub enum SocketHandling {
  /// Envoy performs the socket's I/O on the kernel file descriptor. The module keeps no
  /// per-socket object.
  Kernel,
  /// Envoy delegates the socket's reads, writes and shutdown to the [`SocketIo`].
  DelegatedIo(Box<dyn SocketIo>),
  /// The socket is a listen socket whose connections the [`SocketAcceptor`] hands over. Only valid
  /// for [`SocketCreationResult::Created`].
  HandsOverConnections(Box<dyn SocketAcceptor>),
}

/// The outcome of [`SocketInterfaceConfig::socket`].
pub enum SocketCreationResult {
  /// The module does not create this socket, and Envoy creates it through its standard socket
  /// interface.
  UseDefault,
  /// The socket was created. The file descriptor is handed to Envoy, which owns it from then on.
  Created {
    /// The file descriptor of the created socket. It must be non-blocking and have the requested
    /// address family and socket type.
    fd: i64,
    /// How Envoy handles the socket.
    handling: SocketHandling,
  },
  /// The socket was created with its connection established, or with a non-blocking connect still
  /// in flight, so Envoy must not connect it. Envoy treats the connection as established once the
  /// socket becomes writable. A connection configured with a source address cannot use it.
  AlreadyConnected {
    /// The file descriptor of the socket. It must be non-blocking and have the requested address
    /// family and socket type.
    fd: i64,
    /// How Envoy performs the socket's I/O, which is [`SocketHandling::Kernel`] or
    /// [`SocketHandling::DelegatedIo`].
    handling: SocketHandling,
  },
  /// The socket could not be created. Envoy uses a socket whose operations fail, so the operation
  /// that requested the socket fails.
  Error,
}

/// A socket interface implemented by a dynamic module.
///
/// A single instance is created per socket interface configuration. Its methods run concurrently
/// on several threads, including the main thread for listen sockets, health checks and xDS, so
/// they must be thread-safe and must not block. Resource cleanup belongs in a [`Drop`]
/// implementation, which runs once every socket the module created is gone and must stop the
/// threads the interface started, since the module may be unloaded once it returns.
pub trait SocketInterfaceConfig: Send + Sync {
  /// Returns whether the interface supports the address family, such as `libc::AF_INET`,
  /// `libc::AF_INET6` or `libc::AF_UNIX`. Envoy treats a family the host supports as supported
  /// either way, and leaves the sockets the module declines to its standard socket interface.
  fn ip_family_supported(&self, domain: i32) -> bool;

  /// Creates a socket for the request, or leaves it to Envoy with
  /// [`SocketCreationResult::UseDefault`].
  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult;
}

/// The socket interface that a module configuration holds.
struct ModuleSocketInterface {
  config: Box<dyn SocketInterfaceConfig>,
}

/// The per-socket state of a socket the module created or handed over.
enum ModuleSocket {
  Io(Box<dyn SocketIo>),
  Acceptor {
    acceptor: Box<dyn SocketAcceptor>,
    // The addresses of the last connection handed over, which Envoy reads after `accept`
    // returns.
    local_address: String,
    peer_address: String,
  },
}

impl ModuleSocket {
  fn acceptor(acceptor: Box<dyn SocketAcceptor>) -> Self {
    ModuleSocket::Acceptor {
      acceptor,
      local_address: String::new(),
      peer_address: String::new(),
    }
  }
}

/// Converts a handling into the flags and per-socket state Envoy receives.
fn into_flags_and_state(
  handling: SocketHandling,
) -> (
  u32,
  abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
) {
  match handling {
    SocketHandling::Kernel => (0, ptr::null_mut()),
    SocketHandling::DelegatedIo(io) => (
      abi::envoy_dynamic_module_type_socket_interface_socket_flag::DelegateIo as u32,
      crate::wrap_into_c_void_ptr!(ModuleSocket::Io(io)) as *mut c_void,
    ),
    SocketHandling::HandsOverConnections(acceptor) => (
      abi::envoy_dynamic_module_type_socket_interface_socket_flag::HandsOverConnections as u32,
      crate::wrap_into_c_void_ptr!(ModuleSocket::acceptor(acceptor)) as *mut c_void,
    ),
  }
}

fn to_io_status(
  status: SocketIoStatus,
  bytes_transferred: *mut u64,
) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
  match status {
    SocketIoStatus::Bytes(bytes) => {
      // SAFETY: Envoy passes a valid out-param.
      unsafe { *bytes_transferred = bytes as u64 };
      abi::envoy_dynamic_module_type_socket_interface_io_status::Success
    },
    SocketIoStatus::Again => abi::envoy_dynamic_module_type_socket_interface_io_status::Again,
    SocketIoStatus::Error => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
  }
}

fn to_abi_address(
  address: &str,
  port: u32,
) -> abi::envoy_dynamic_module_type_socket_interface_address {
  abi::envoy_dynamic_module_type_socket_interface_address {
    address: abi::envoy_dynamic_module_type_module_buffer {
      ptr: address.as_ptr() as *const _,
      length: address.len(),
    },
    port,
  }
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
    flags_out: *mut u32,
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
    let (result, fd, handling) = match module.config.socket(&creation) {
      SocketCreationResult::UseDefault => {
        return abi::envoy_dynamic_module_type_socket_interface_creation_result::UseDefault
      },
      SocketCreationResult::Error => {
        return abi::envoy_dynamic_module_type_socket_interface_creation_result::Error
      },
      SocketCreationResult::Created { fd, handling } => (
        abi::envoy_dynamic_module_type_socket_interface_creation_result::Created,
        fd,
        handling,
      ),
      SocketCreationResult::AlreadyConnected { fd, handling } => (
        abi::envoy_dynamic_module_type_socket_interface_creation_result::AlreadyConnected,
        fd,
        handling,
      ),
    };
    let (flags, state) = into_flags_and_state(handling);
    *fd_out = fd;
    *flags_out = flags;
    *socket_module_ptr_out = state;
    result
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_creation_result::Error
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
    drop(Box::from_raw(socket_module_ptr as *mut ModuleSocket));
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_socket_duplicate(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    fd_out: *mut i64,
    duplicate_socket_module_ptr_out:
      *mut abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let module = &mut *(socket_module_ptr as *mut ModuleSocket);
    let copy = match module {
      ModuleSocket::Io(io) => io.duplicate().map(|(fd, io)| (fd, ModuleSocket::Io(io))),
      ModuleSocket::Acceptor { acceptor, .. } => acceptor
        .duplicate()
        .map(|(fd, acceptor)| (fd, ModuleSocket::acceptor(acceptor))),
    };
    match copy {
      Some((fd, state)) => {
        *fd_out = fd;
        *duplicate_socket_module_ptr_out = crate::wrap_into_c_void_ptr!(state) as *mut c_void;
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      None => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_set_socket_handle(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    io_ready_handle_module_ptr:
      abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr,
  ) {
    let module = &mut *(socket_module_ptr as *mut ModuleSocket);
    // SAFETY: Envoy passes the readiness handle it created for this socket, and the handle's
    // lifetime is module-managed from this point on.
    let handle = Box::new(unsafe { EnvoySocketIoReadyHandleImpl::new(io_ready_handle_module_ptr) });
    match module {
      ModuleSocket::Io(io) => io.set_ready_handle(handle),
      ModuleSocket::Acceptor { acceptor, .. } => acceptor.set_ready_handle(handle),
    }
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_read(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    slices: *mut abi::envoy_dynamic_module_type_socket_interface_io_slice,
    num_slices: usize,
    peek: bool,
    bytes_transferred: *mut u64,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let ModuleSocket::Io(io) = &mut *(socket_module_ptr as *mut ModuleSocket) else {
      return abi::envoy_dynamic_module_type_socket_interface_io_status::Error;
    };
    // SAFETY: Envoy passes a valid array of `num_slices` slices owned by its read buffer.
    let abi_slices: &[abi::envoy_dynamic_module_type_socket_interface_io_slice] =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(slices as *const _, num_slices) };
    let mut rust_slices: SmallVec<[&mut [MaybeUninit<u8>]; INLINE_SLICES]> = abi_slices
      .iter()
      .map(|slice| -> &mut [MaybeUninit<u8>] {
        // An empty slice may carry a null pointer, which `from_raw_parts_mut` rejects even for a
        // length of zero, so empty slices are passed through without materializing a pointer.
        if slice.length == 0 {
          &mut []
        } else {
          unsafe { std::slice::from_raw_parts_mut(slice.ptr as *mut MaybeUninit<u8>, slice.length) }
        }
      })
      .collect();
    to_io_status(io.io_read(&mut rust_slices, peek), bytes_transferred)
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_write(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    buffers: *const abi::envoy_dynamic_module_type_envoy_buffer,
    num_buffers: usize,
    bytes_transferred: *mut u64,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let ModuleSocket::Io(io) = &mut *(socket_module_ptr as *mut ModuleSocket) else {
      return abi::envoy_dynamic_module_type_socket_interface_io_status::Error;
    };
    // SAFETY: Envoy passes a valid array of `num_buffers` buffers owned by its write buffer.
    let abi_buffers = unsafe { crate::ffi_helpers::slice_from_raw_or_empty(buffers, num_buffers) };
    let rust_buffers: SmallVec<[&[u8]; INLINE_SLICES]> = abi_buffers
      .iter()
      .map(|buffer| -> &[u8] {
        // An empty buffer may carry a null pointer, which `from_raw_parts` rejects even for a
        // length of zero, so empty buffers are passed through without materializing a pointer.
        if buffer.length == 0 {
          &[]
        } else {
          unsafe { std::slice::from_raw_parts(buffer.ptr as *const u8, buffer.length) }
        }
      })
      .collect();
    to_io_status(io.io_write(&rust_buffers), bytes_transferred)
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_io_shutdown(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    how: i32,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let ModuleSocket::Io(io) = &mut *(socket_module_ptr as *mut ModuleSocket) else {
      return abi::envoy_dynamic_module_type_socket_interface_io_status::NotImplemented;
    };
    match io.io_shutdown(how) {
      SocketShutdownStatus::Done => {
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      SocketShutdownStatus::Error => {
        abi::envoy_dynamic_module_type_socket_interface_io_status::Error
      },
      SocketShutdownStatus::NotImplemented => {
        abi::envoy_dynamic_module_type_socket_interface_io_status::NotImplemented
      },
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_socket_interface_accept(
    _config_ptr: *const c_void,
    socket_module_ptr: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    fd_out: *mut i64,
    flags_out: *mut u32,
    accepted_socket_module_ptr_out:
      *mut abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
    local_address_out: *mut abi::envoy_dynamic_module_type_socket_interface_address,
    peer_address_out: *mut abi::envoy_dynamic_module_type_socket_interface_address,
  ) -> abi::envoy_dynamic_module_type_socket_interface_io_status {
    let ModuleSocket::Acceptor {
      acceptor,
      local_address,
      peer_address,
    } = &mut *(socket_module_ptr as *mut ModuleSocket)
    else {
      return abi::envoy_dynamic_module_type_socket_interface_io_status::Error;
    };
    match acceptor.accept() {
      SocketAcceptResult::Accepted {
        fd,
        handling,
        local_address: local,
        peer_address: peer,
      } => {
        let (flags, state) = into_flags_and_state(handling);
        *fd_out = fd;
        *flags_out = flags;
        *accepted_socket_module_ptr_out = state;
        // The addresses live in the listen socket's state until its next hook call, which is
        // after Envoy copied them.
        if let Some(local) = local {
          *local_address = local.address;
          *local_address_out = to_abi_address(local_address, local.port);
        }
        if let Some(peer) = peer {
          *peer_address = peer.address;
          *peer_address_out = to_abi_address(peer_address, peer.port);
        }
        abi::envoy_dynamic_module_type_socket_interface_io_status::Success
      },
      SocketAcceptResult::Again => abi::envoy_dynamic_module_type_socket_interface_io_status::Again,
      SocketAcceptResult::Error => abi::envoy_dynamic_module_type_socket_interface_io_status::Error,
    }
  }
  on_panic = abi::envoy_dynamic_module_type_socket_interface_io_status::Error
}

#[cfg(test)]
#[path = "socket_interface_test.rs"]
mod tests;
