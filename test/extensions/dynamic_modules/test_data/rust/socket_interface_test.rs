//! Test module for dynamic module socket interfaces.
//!
//! The configuration selects the role.
//!
//! A port number selects the dialing role. The module runs an internal echo server on a loopback
//! listener, and when Envoy asks for a stream socket to the configured port, it starts a
//! non-blocking connect to the echo server instead and hands Envoy the socket as already connected,
//! with its I/O delegated to the module. Every other IPv4 stream socket is left to the kernel.
//!
//! The string "acceptor" selects the acceptor role. Listen sockets hand over their connections:
//! each time the test calls `socket_interface_test_queue_connection`, a thread creates a connection
//! and reports it on the listen sockets. Each handed-over connection serves "hello" from the
//! module's memory before reading its file descriptor, whose peer never writes, so its readiness
//! comes only from the module, starting before Envoy registers the socket.

use envoy_proxy_dynamic_modules_rust_sdk::socket_interface::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::VecDeque;
use std::io::{Read, Write};
use std::mem::MaybeUninit;
use std::net::{Shutdown, TcpListener, TcpStream};
use std::os::fd::{FromRawFd, IntoRawFd, RawFd};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

declare_all_init_functions!(init, socket_interface: new_socket_interface_config);

fn init() -> bool {
  true
}

fn new_socket_interface_config(
  _name: &str,
  config: &[u8],
) -> Option<Box<dyn SocketInterfaceConfig>> {
  let config_str = std::str::from_utf8(config).ok()?.trim();
  if config_str == "acceptor" {
    return Some(Box::new(AcceptorConfig {
      shared: ACCEPTOR_SHARED
        .get_or_init(|| Arc::new(AcceptorShared::default()))
        .clone(),
    }));
  }
  Some(Box::new(DialConfig {
    dial_port: config_str.parse().ok()?,
    echo_server: EchoServer::new()?,
  }))
}

// The libc functions this module needs, declared directly to keep it free of extra dependencies.
// The symbols resolve from the host process at load time.
#[cfg(not(test))]
extern "C" {
  fn socket(domain: i32, socket_type: i32, protocol: i32) -> i32;
  fn connect(fd: i32, addr: *const SockAddrIn, len: u32) -> i32;
  fn dup(fd: i32) -> i32;
}

// The unit test build links the module as an executable where the host symbols do not exist, so
// stub the calls out.
#[cfg(test)]
unsafe fn socket(_domain: i32, _socket_type: i32, _protocol: i32) -> i32 {
  -1
}
#[cfg(test)]
unsafe fn connect(_fd: i32, _addr: *const SockAddrIn, _len: u32) -> i32 {
  -1
}
#[cfg(test)]
unsafe fn dup(_fd: i32) -> i32 {
  -1
}

const AF_INET: i32 = 2;
const SOCK_STREAM: i32 = 1;
const SOCK_NONBLOCK: i32 = 0o4000;
const EINPROGRESS: i32 = 115;

#[repr(C)]
struct SockAddrIn {
  sin_family: u16,
  sin_port: u16,
  sin_addr: u32,
  sin_zero: [u8; 8],
}

// =================================================================================================
// Dialing role
// =================================================================================================

/// The internal echo server. Its thread stops when the configuration is dropped; the echo
/// connections exit on their own once their peers close them.
struct EchoServer {
  port: u16,
  stopped: Arc<AtomicBool>,
}

impl EchoServer {
  fn new() -> Option<EchoServer> {
    let listener = TcpListener::bind(("127.0.0.1", 0)).ok()?;
    listener.set_nonblocking(true).ok()?;
    let port = listener.local_addr().ok()?.port();
    let stopped = Arc::new(AtomicBool::new(false));
    let thread_stopped = stopped.clone();
    std::thread::spawn(move || {
      while !thread_stopped.load(Ordering::Relaxed) {
        match listener.accept() {
          Ok((stream, _)) => {
            std::thread::spawn(move || echo(stream));
          },
          Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
            std::thread::sleep(std::time::Duration::from_millis(10));
          },
          Err(_) => break,
        }
      }
    });
    Some(EchoServer { port, stopped })
  }
}

impl Drop for EchoServer {
  fn drop(&mut self) {
    self.stopped.store(true, Ordering::Relaxed);
  }
}

fn echo(mut stream: TcpStream) {
  // The accepted stream inherits blocking mode, which is what this thread wants.
  let _ = stream.set_nonblocking(false);
  let mut buf = [0u8; 4096];
  loop {
    match stream.read(&mut buf) {
      Ok(0) | Err(_) => break,
      Ok(n) => {
        if stream.write_all(&buf[.. n]).is_err() {
          break;
        }
      },
    }
  }
}

struct DialConfig {
  dial_port: u32,
  echo_server: EchoServer,
}

impl DialConfig {
  // Starts a non-blocking connect to the echo server, which Envoy completes once the socket is
  // writable, so the worker never blocks here.
  fn dial(&self) -> Option<RawFd> {
    let fd = unsafe { socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0) };
    if fd < 0 {
      return None;
    }
    let addr = SockAddrIn {
      sin_family: AF_INET as u16,
      sin_port: self.echo_server.port.to_be(),
      sin_addr: u32::from_be_bytes([127, 0, 0, 1]).to_be(),
      sin_zero: [0; 8],
    };
    let rc = unsafe { connect(fd, &addr, std::mem::size_of::<SockAddrIn>() as u32) };
    if rc != 0 && std::io::Error::last_os_error().raw_os_error() != Some(EINPROGRESS) {
      drop(unsafe { TcpStream::from_raw_fd(fd) });
      return None;
    }
    Some(fd)
  }
}

impl SocketInterfaceConfig for DialConfig {
  fn ip_family_supported(&self, domain: i32) -> bool {
    domain == AF_INET
  }

  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult {
    if creation.socket_type != SocketType::Stream
      || creation.domain != AF_INET
      || creation.port != self.dial_port
    {
      return SocketCreationResult::UseDefault;
    }
    match self.dial() {
      Some(fd) => SocketCreationResult::AlreadyConnected {
        fd: fd as i64,
        handling: SocketHandling::DelegatedIo(Box::new(PassthroughSocket::new(fd))),
      },
      None => SocketCreationResult::Error,
    }
  }
}

// =================================================================================================
// Acceptor role
// =================================================================================================

/// State shared by every listen socket of the acceptor role and the thread that creates
/// connections.
#[derive(Default)]
struct AcceptorShared {
  // Connections waiting to be handed over, as the file descriptors Envoy receives.
  pending: Mutex<VecDeque<RawFd>>,
  // The other ends of the handed-over connections, kept open so they never close on Envoy.
  peers: Mutex<Vec<TcpStream>>,
  // The readiness handles of the listen sockets and their copies.
  ready_handles: Mutex<Vec<Box<dyn EnvoySocketIoReadyHandle>>>,
  duplicates: AtomicU32,
}

static ACCEPTOR_SHARED: OnceLock<Arc<AcceptorShared>> = OnceLock::new();

/// Called by the integration test: creates a connection on another thread, queues it and reports
/// it on every listen socket, so whichever worker accepts first hands it over.
#[no_mangle]
pub extern "C" fn socket_interface_test_queue_connection() {
  let Some(shared) = ACCEPTOR_SHARED.get().cloned() else {
    return;
  };
  let _ = std::thread::spawn(move || {
    let Ok(listener) = TcpListener::bind(("127.0.0.1", 0)) else {
      return;
    };
    let Ok(addr) = listener.local_addr() else {
      return;
    };
    let Ok(client) = TcpStream::connect(addr) else {
      return;
    };
    let Ok((peer, _)) = listener.accept() else {
      return;
    };
    if client.set_nonblocking(true).is_err() {
      return;
    }
    shared.peers.lock().unwrap().push(peer);
    shared
      .pending
      .lock()
      .unwrap()
      .push_back(client.into_raw_fd());
    for handle in shared.ready_handles.lock().unwrap().iter() {
      handle.ready(SocketIoEvents::READ);
    }
  })
  .join();
}

/// Called by the integration test: the number of listen socket copies Envoy made.
#[no_mangle]
pub extern "C" fn socket_interface_test_duplicate_count() -> u32 {
  ACCEPTOR_SHARED
    .get()
    .map_or(0, |shared| shared.duplicates.load(Ordering::Relaxed))
}

struct AcceptorConfig {
  shared: Arc<AcceptorShared>,
}

impl SocketInterfaceConfig for AcceptorConfig {
  fn ip_family_supported(&self, domain: i32) -> bool {
    domain == AF_INET
  }

  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult {
    if creation.socket_type != SocketType::Stream || creation.domain != AF_INET {
      return SocketCreationResult::UseDefault;
    }
    // Upstream sockets are left to the kernel, and the listener's socket hands over connections.
    // The listener asks for its bind address with port 0, and an upstream socket for the fake
    // upstream's port.
    if creation.port != 0 {
      return SocketCreationResult::UseDefault;
    }
    let fd = unsafe { socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0) };
    if fd < 0 {
      return SocketCreationResult::Error;
    }
    SocketCreationResult::Created {
      fd: fd as i64,
      handling: SocketHandling::HandsOverConnections(Box::new(ListenSocket {
        fd,
        shared: self.shared.clone(),
      })),
    }
  }
}

/// A listen socket that hands over the queued connections.
struct ListenSocket {
  fd: RawFd,
  shared: Arc<AcceptorShared>,
}

impl SocketAcceptor for ListenSocket {
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>) {
    if !self.shared.pending.lock().unwrap().is_empty() {
      handle.ready(SocketIoEvents::READ);
    }
    self.shared.ready_handles.lock().unwrap().push(handle);
  }

  fn accept(&mut self) -> SocketAcceptResult {
    match self.shared.pending.lock().unwrap().pop_front() {
      Some(fd) => SocketAcceptResult::Accepted {
        fd: fd as i64,
        handling: SocketHandling::DelegatedIo(Box::new(GreetingSocket {
          greeting: b"hello".to_vec(),
          inner: PassthroughSocket::new(fd),
        })),
        local_address: None,
        peer_address: None,
      },
      None => SocketAcceptResult::Again,
    }
  }

  fn duplicate(&mut self) -> Option<(i64, Box<dyn SocketAcceptor>)> {
    let fd = unsafe { dup(self.fd) };
    if fd < 0 {
      return None;
    }
    self.shared.duplicates.fetch_add(1, Ordering::Relaxed);
    Some((
      fd as i64,
      Box::new(ListenSocket {
        fd,
        shared: self.shared.clone(),
      }),
    ))
  }
}

/// A connection that serves a greeting from the module's memory before reading its socket. The
/// kernel never signals the greeting, so the socket reports it as soon as Envoy hands over its
/// readiness handle, before Envoy registers the socket with its worker.
struct GreetingSocket {
  greeting: Vec<u8>,
  inner: PassthroughSocket,
}

impl SocketIo for GreetingSocket {
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>) {
    handle.ready(SocketIoEvents::READ);
    self.inner.set_ready_handle(handle);
  }

  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]], peek: bool) -> SocketIoStatus {
    if self.greeting.is_empty() {
      return self.inner.io_read(slices, peek);
    }
    let mut copied = 0;
    for slice in slices.iter_mut() {
      let chunk = slice.len().min(self.greeting.len() - copied);
      for (dst, src) in slice
        .iter_mut()
        .zip(&self.greeting[copied .. copied + chunk])
      {
        *dst = MaybeUninit::new(*src);
      }
      copied += chunk;
      // Envoy commits the count as one prefix, so stop at the first slice not filled.
      if chunk < slice.len() {
        break;
      }
    }
    if !peek {
      self.greeting.drain(.. copied);
    }
    SocketIoStatus::Bytes(copied)
  }

  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus {
    self.inner.io_write(buffers)
  }

  fn io_shutdown(&mut self, how: i32) -> SocketShutdownStatus {
    self.inner.io_shutdown(how)
  }
}

// =================================================================================================
// Shared per-socket I/O
// =================================================================================================

/// A per-socket object that performs the kernel read and write syscalls on the socket the module
/// created.
struct PassthroughSocket {
  fd: RawFd,
  // The kernel reports this socket's readiness, so the handle is kept only to keep it alive.
  _ready_handle: Option<Box<dyn EnvoySocketIoReadyHandle>>,
}

impl PassthroughSocket {
  fn new(fd: RawFd) -> Self {
    Self {
      fd,
      _ready_handle: None,
    }
  }

  fn stream(&self) -> ManuallyDropStream {
    unsafe { ManuallyDropStream::new(self.fd) }
  }
}

impl SocketIo for PassthroughSocket {
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>) {
    self._ready_handle = Some(handle);
  }

  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]], peek: bool) -> SocketIoStatus {
    let mut total = 0usize;
    for slice in slices.iter_mut() {
      // The read syscall only writes into the slice, so handing it over uninitialized is safe.
      let buf =
        unsafe { std::slice::from_raw_parts_mut(slice.as_mut_ptr().cast::<u8>(), slice.len()) };
      let result = if peek {
        self.stream().peek(buf)
      } else {
        self.stream().read(buf)
      };
      match result {
        Ok(n) => {
          total += n;
          // A short read ends the prefix, and a peek only looks at the first slice.
          if n < buf.len() || peek {
            break;
          }
        },
        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
          if total == 0 {
            return SocketIoStatus::Again;
          }
          break;
        },
        Err(_) => return SocketIoStatus::Error,
      }
    }
    SocketIoStatus::Bytes(total)
  }

  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus {
    let mut total = 0usize;
    for buffer in buffers {
      match self.stream().write(buffer) {
        Ok(n) => {
          total += n;
          // A short write ends the prefix; writing on would reorder the stream.
          if n < buffer.len() {
            break;
          }
        },
        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
        Err(_) => return SocketIoStatus::Error,
      }
    }
    if total > 0 || buffers.is_empty() {
      SocketIoStatus::Bytes(total)
    } else {
      SocketIoStatus::Again
    }
  }

  fn io_shutdown(&mut self, how: i32) -> SocketShutdownStatus {
    let shutdown_kind = match how {
      0 => Shutdown::Read,
      1 => Shutdown::Write,
      _ => Shutdown::Both,
    };
    match self.stream().shutdown(shutdown_kind) {
      Ok(()) => SocketShutdownStatus::Done,
      Err(_) => SocketShutdownStatus::Error,
    }
  }
}

/// A `TcpStream` view of a file descriptor Envoy owns; `ManuallyDrop` keeps the wrapper from
/// closing the descriptor.
struct ManuallyDropStream {
  stream: std::mem::ManuallyDrop<TcpStream>,
}

impl ManuallyDropStream {
  /// # Safety
  ///
  /// `fd` must be a TCP socket file descriptor.
  unsafe fn new(fd: RawFd) -> Self {
    Self {
      stream: std::mem::ManuallyDrop::new(unsafe { TcpStream::from_raw_fd(fd) }),
    }
  }
}

impl std::ops::Deref for ManuallyDropStream {
  type Target = TcpStream;

  fn deref(&self) -> &TcpStream {
    &self.stream
  }
}

impl std::ops::DerefMut for ManuallyDropStream {
  fn deref_mut(&mut self) -> &mut TcpStream {
    &mut self.stream
  }
}
