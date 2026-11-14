//! Test module for dynamic module socket interfaces.
//!
//! The module runs an internal echo server on a loopback listener that it starts when the
//! configuration is created. The configuration is the port that triggers the dialing role: when
//! Envoy asks for a stream socket to that port, the module dials a connection to the internal
//! echo server instead and hands the connected socket to Envoy, which never connects it. Every
//! other IPv4 stream socket is created unbound and left for Envoy to connect or listen on, and
//! the I/O of every socket flows through the module's read and write hooks.

use envoy_proxy_dynamic_modules_rust_sdk::socket_interface::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::io::{Read, Write};
use std::mem::MaybeUninit;
use std::net::{Shutdown, TcpListener, TcpStream};
use std::os::fd::{FromRawFd, IntoRawFd, RawFd};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

declare_all_init_functions!(init, socket_interface: new_socket_interface_config);

fn init() -> bool {
  true
}

fn new_socket_interface_config(
  _name: &str,
  config: &[u8],
) -> Option<Box<dyn SocketInterfaceConfig>> {
  Some(Box::new(TestSocketInterfaceConfig {
    dial_port: std::str::from_utf8(config).ok()?.trim().parse().ok()?,
    acceptor: Acceptor::new()?,
  }))
}

/// The internal echo server. Its thread stops when the configuration is dropped; the echo
/// connections exit on their own once their peers close them.
struct Acceptor {
  port: u16,
  stopped: Arc<AtomicBool>,
}

impl Acceptor {
  fn new() -> Option<Arc<Acceptor>> {
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
    Some(Arc::new(Acceptor { port, stopped }))
  }
}

impl Drop for Acceptor {
  fn drop(&mut self) {
    self.stopped.store(true, Ordering::Relaxed);
  }
}

fn echo(mut stream: TcpStream) {
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

// Creates an unbound socket for the plain role. Declared directly instead of through the libc
// crate to keep the test module free of extra dependencies; the symbol resolves from the host
// process at load time.
#[cfg(not(test))]
extern "C" {
  fn socket(domain: i32, socket_type: i32, protocol: i32) -> i32;
}

// The unit test build links the module as an executable where the host symbol does not exist, so
// stub the call out.
#[cfg(test)]
unsafe fn socket(_domain: i32, _socket_type: i32, _protocol: i32) -> i32 {
  -1
}

const AF_INET: i32 = 2;
const SOCK_STREAM: i32 = 1;
const SOCK_NONBLOCK: i32 = 0o4000;

struct TestSocketInterfaceConfig {
  dial_port: u32,
  acceptor: Arc<Acceptor>,
}

impl SocketInterfaceConfig for TestSocketInterfaceConfig {
  fn ip_family_supported(&self, domain: i32) -> bool {
    domain == AF_INET
  }

  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult {
    if creation.socket_type != SocketType::Stream || creation.domain != AF_INET {
      return SocketCreationResult::UnsupportedFamily;
    }
    if creation.port == self.dial_port {
      // The dialing role: connect to the internal echo server and hand the connected socket to
      // Envoy without letting it connect again.
      let stream = match TcpStream::connect(("127.0.0.1", self.acceptor.port)) {
        Ok(stream) => stream,
        Err(_) => return SocketCreationResult::Error,
      };
      if stream.set_nonblocking(true).is_err() {
        return SocketCreationResult::Error;
      }
      let fd = stream.into_raw_fd();
      return SocketCreationResult::AlreadyConnected {
        fd: fd as i64,
        io: Box::new(PassthroughSocket { fd }),
      };
    }
    // The plain role: an unbound socket Envoy connects or listens on.
    let fd = unsafe { socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0) };
    if fd < 0 {
      return SocketCreationResult::Error;
    }
    SocketCreationResult::Created {
      fd: fd as i64,
      io: Box::new(PassthroughSocket { fd }),
    }
  }
}

/// A per-socket object that performs the kernel read and write syscalls on the socket the module
/// created.
struct PassthroughSocket {
  fd: RawFd,
}

impl SocketIo for PassthroughSocket {
  fn set_ready_handle(&mut self, handle: EnvoySocketIoReadyHandle) {
    // The kernel reports readiness for these sockets, so the handle is not needed; dropping it
    // deletes the Envoy-side handle.
    drop(handle);
  }

  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]]) -> SocketIoStatus {
    let mut total = 0usize;
    let mut eof = false;
    for slice in slices.iter_mut() {
      // The kernel write of a read syscall is the only use of the bytes, so treating the
      // uninitialized slice as initialized is safe here.
      let buf =
        unsafe { std::slice::from_raw_parts_mut(slice.as_mut_ptr().cast::<u8>(), slice.len()) };
      let mut stream = unsafe { ManuallyDropStream::new(self.fd) };
      match stream.read(buf) {
        Ok(0) => {
          eof = true;
          break;
        },
        Ok(n) => total += n,
        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
        Err(_) => return SocketIoStatus::Error,
      }
    }
    if total > 0 {
      SocketIoStatus::Bytes(total)
    } else if eof {
      SocketIoStatus::Bytes(0)
    } else {
      SocketIoStatus::Again
    }
  }

  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus {
    let mut total = 0usize;
    for buffer in buffers {
      let mut stream = unsafe { ManuallyDropStream::new(self.fd) };
      match stream.write(buffer) {
        Ok(n) => total += n,
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

  fn io_shutdown(&mut self, how: i32) -> SocketIoStatus {
    let shutdown_kind = match how {
      0 => Shutdown::Read,
      1 => Shutdown::Write,
      _ => Shutdown::Both,
    };
    let stream = unsafe { ManuallyDropStream::new(self.fd) };
    match stream.shutdown(shutdown_kind) {
      Ok(()) => SocketIoStatus::Bytes(0),
      Err(_) => SocketIoStatus::Error,
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
