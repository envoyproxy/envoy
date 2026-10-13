//! Splice relay transport socket used by the dynamic module transport socket integration test.
//!
//! The "splice" transport socket goes on both the listener filter chain and the cluster of a TCP
//! proxy. The upstream socket registers itself under its connection ID, and the downstream socket
//! claims it through the upstream connection ID that the TCP proxy records. Each direction starts
//! on Envoy's buffered path and switches to splice(2) through a pipe once the peer reports that
//! exactly the bytes passed through Envoy have arrived in its write buffer. This keeps the bytes in
//! order and never splices into an upstream that writes bytes of its own, such as an HTTP tunnel.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

declare_transport_socket_init_functions!(init, new_transport_socket_factory_config);

// More than a pipe holds, so a splice is only limited by the pipe and the socket.
const MAX_SPLICE_BYTES: usize = 1 << 20;

type SocketPtr = abi::envoy_dynamic_module_type_transport_socket_envoy_ptr;

thread_local! {
  // Upstream relays of this worker that no downstream socket has claimed yet, keyed by the
  // connection ID of the upstream socket. Both sockets of a TCP proxy session run on one worker.
  static UNCLAIMED_UPSTREAMS: RefCell<HashMap<u64, Rc<RefCell<Relay>>>> =
    RefCell::new(HashMap::new());
}

fn init() -> bool {
  true
}

fn new_transport_socket_factory_config(
  name: &str,
  _config: &[u8],
  is_upstream: bool,
) -> Option<Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>> {
  if name != "splice" {
    envoy_log_error!("unknown transport socket name {}", name);
    return None;
  }
  let role = if is_upstream {
    Role::Upstream
  } else {
    Role::Downstream
  };
  Some(Box::new(SpliceFactoryConfig { role }))
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Role {
  Downstream,
  Upstream,
}

impl Role {
  fn peer(self) -> Role {
    match self {
      Role::Downstream => Role::Upstream,
      Role::Upstream => Role::Downstream,
    }
  }
}

struct SpliceFactoryConfig {
  role: Role,
}

impl TransportSocketFactoryConfig<EnvoyTransportSocketImpl> for SpliceFactoryConfig {
  fn new_transport_socket(
    &self,
    _envoy: &mut EnvoyTransportSocketImpl,
  ) -> Box<dyn TransportSocket<EnvoyTransportSocketImpl>> {
    Box::new(SpliceTransportSocket::new(self.role))
  }
}

enum Mode {
  // The reader passes its bytes through Envoy's buffers.
  Buffered,
  // The reader splices its bytes into the pipe and the writer splices them out.
  Spliced(sys::Pipe),
  // The direction stays on the buffered path for good.
  Disabled,
}

// One direction of a relay. The reader moves bytes from its socket into the pipe and the writer
// moves them from the pipe into its socket.
struct Direction {
  mode: Mode,
  // Bytes in the pipe.
  pipe_bytes: usize,
  // Bytes that have arrived in the writer's write buffer, published by the writer on each write and
  // cleared once the reader passes more bytes through Envoy.
  bytes_arrived_at_writer: Option<u64>,
  // The reader saw the end of its stream. It reports it to Envoy once the pipe is empty.
  reader_eof: bool,
  // The reader could not tell a full pipe from an empty socket, so the writer wakes it once it
  // has drained some of the pipe.
  reader_waiting: bool,
}

impl Direction {
  fn new() -> Self {
    Direction {
      mode: Mode::Buffered,
      pipe_bytes: 0,
      bytes_arrived_at_writer: None,
      reader_eof: false,
      reader_waiting: false,
    }
  }
}

// The state shared by the downstream and the upstream socket of a TCP proxy session.
struct Relay {
  // Envoy pointers of the two sockets. A socket clears its own when it closes, before Envoy
  // destroys it, so a pointer here is always safe to wake.
  downstream: Option<SocketPtr>,
  upstream: Option<SocketPtr>,
  downstream_to_upstream: Direction,
  upstream_to_downstream: Direction,
}

impl Relay {
  fn socket(&mut self, role: Role) -> &mut Option<SocketPtr> {
    match role {
      Role::Downstream => &mut self.downstream,
      Role::Upstream => &mut self.upstream,
    }
  }

  fn peer(&mut self, role: Role) -> Option<SocketPtr> {
    *self.socket(role.peer())
  }

  // The direction whose bytes the socket of `role` reads.
  fn outbound(&mut self, role: Role) -> &mut Direction {
    match role {
      Role::Downstream => &mut self.downstream_to_upstream,
      Role::Upstream => &mut self.upstream_to_downstream,
    }
  }

  // The direction whose bytes the socket of `role` writes.
  fn inbound(&mut self, role: Role) -> &mut Direction {
    self.outbound(role.peer())
  }
}

// Splicing a blocking socket would block the worker thread. Envoy creates its sockets non-blocking
// except for upstream sockets when io_uring is enabled.
fn spliceable_fd(envoy: &EnvoyTransportSocketImpl) -> Option<i32> {
  envoy.get_fd().filter(|fd| sys::is_nonblocking(*fd))
}

// A close that strands bytes in the pipe this socket fills is a reset, so that the peer sees the
// truncation.
fn close_result(peer_reset: bool, pipe_has_bytes: bool, bytes_processed: usize) -> IoResult {
  if peer_reset || pipe_has_bytes {
    IoResult::reset(bytes_processed)
  } else {
    IoResult::close(bytes_processed, false)
  }
}

fn wake_reader(socket: Option<SocketPtr>) {
  if let Some(socket) = socket {
    EnvoyTransportSocketImpl::new(socket).set_is_readable();
  }
}

fn wake_writer(socket: Option<SocketPtr>) {
  if let Some(socket) = socket {
    EnvoyTransportSocketImpl::new(socket).set_is_writable();
  }
}

struct SpliceTransportSocket {
  role: Role,
  // The connection ID an upstream socket is registered under until it closes.
  registered_id: Option<u64>,
  // The socket descriptor, set once the socket joins a relay.
  fd: i32,
  relay: Option<Rc<RefCell<Relay>>>,
  // A downstream socket looks for its upstream once the TCP proxy records the upstream connection.
  claim_attempted: bool,
  // Bytes this socket passed into Envoy's read buffer.
  bytes_read_through_envoy: u64,
  // Bytes this socket drained from Envoy's write buffer.
  bytes_drained_from_envoy: u64,
  write_shutdown: bool,
  failure_reason: String,
}

impl SpliceTransportSocket {
  fn new(role: Role) -> Self {
    SpliceTransportSocket {
      role,
      registered_id: None,
      fd: -1,
      relay: None,
      claim_attempted: false,
      bytes_read_through_envoy: 0,
      bytes_drained_from_envoy: 0,
      write_shutdown: false,
      failure_reason: String::new(),
    }
  }

  fn register_upstream(&mut self, envoy: &EnvoyTransportSocketImpl) {
    if self.role != Role::Upstream {
      return;
    }
    let (Some(id), Some(fd)) = (envoy.get_connection_id(), spliceable_fd(envoy)) else {
      return;
    };
    let relay = Rc::new(RefCell::new(Relay {
      downstream: None,
      upstream: Some(envoy.envoy_ptr()),
      downstream_to_upstream: Direction::new(),
      upstream_to_downstream: Direction::new(),
    }));
    UNCLAIMED_UPSTREAMS.with(|upstreams| upstreams.borrow_mut().insert(id, relay.clone()));
    self.registered_id = Some(id);
    self.fd = fd;
    self.relay = Some(relay);
  }

  // A claim removes the upstream from the registry, so an upstream joins at most one relay. Only a
  // pair where no bytes have passed from the downstream to the upstream yet can tell that the
  // upstream carries exactly the downstream bytes, which rules out HTTP tunnels and early data.
  fn claim_upstream(&mut self, envoy: &EnvoyTransportSocketImpl) {
    if self.role != Role::Downstream || self.claim_attempted {
      return;
    }
    let Some(upstream_id) = envoy.get_upstream_connection_id() else {
      return;
    };
    self.claim_attempted = true;
    let Some(fd) = spliceable_fd(envoy) else {
      return;
    };
    let relay = UNCLAIMED_UPSTREAMS
      .with(|upstreams| upstreams.borrow_mut().remove(&upstream_id))
      .filter(|relay| {
        self.bytes_read_through_envoy == 0
          && relay
            .borrow()
            .downstream_to_upstream
            .bytes_arrived_at_writer
            .unwrap_or(0)
            == 0
      });
    let Some(relay) = relay else {
      envoy_log_debug!("upstream connection {} cannot be spliced", upstream_id);
      return;
    };
    relay.borrow_mut().downstream = Some(envoy.envoy_ptr());
    self.fd = fd;
    self.relay = Some(relay);
    envoy_log_debug!(
      "connection {} paired with upstream connection {}",
      envoy.get_connection_id().unwrap_or_default(),
      upstream_id
    );
  }

  // The peer stops splicing into the pipe this socket drained and is woken to read through Envoy
  // instead.
  fn detach(&mut self) {
    if let Some(id) = self.registered_id.take() {
      let _ = UNCLAIMED_UPSTREAMS.try_with(|upstreams| upstreams.borrow_mut().remove(&id));
    }
    let Some(relay) = self.relay.take() else {
      return;
    };
    let mut relay = relay.borrow_mut();
    *relay.socket(self.role) = None;
    relay.inbound(self.role).mode = Mode::Disabled;
    wake_reader(relay.peer(self.role));
  }

  fn maybe_start_splicing(&self, envoy: &EnvoyTransportSocketImpl, relay: &mut Relay) {
    if relay.peer(self.role).is_none() {
      return;
    }
    let direction = relay.outbound(self.role);
    if !matches!(direction.mode, Mode::Buffered)
      || self.bytes_read_through_envoy == 0
      || direction.bytes_arrived_at_writer != Some(self.bytes_read_through_envoy)
    {
      return;
    }
    let connection_id = envoy.get_connection_id().unwrap_or_default();
    match sys::Pipe::new() {
      Some(pipe) => {
        envoy_log_debug!(
          "connection {} splicing {}",
          connection_id,
          self.outbound_name()
        );
        direction.mode = Mode::Spliced(pipe);
      },
      None => {
        envoy_log_debug!(
          "connection {} cannot create a pipe to splice {}",
          connection_id,
          self.outbound_name()
        );
        direction.mode = Mode::Disabled;
      },
    }
  }

  fn outbound_name(&self) -> &'static str {
    match self.role {
      Role::Downstream => "downstream to upstream",
      Role::Upstream => "upstream to downstream",
    }
  }

  fn read_buffered(&mut self, envoy: &EnvoyTransportSocketImpl) -> IoResult {
    let mut total = 0;
    let result = loop {
      let (status, n) = envoy.io_read_to_read_buffer(usize::MAX);
      match status {
        IoStatus::Success if n == 0 => break IoResult::keep_open(total, true),
        IoStatus::Success => {
          total += n;
          if envoy.should_drain_read_buffer() {
            envoy.set_is_readable();
            break IoResult::keep_open(total, false);
          }
        },
        IoStatus::Again => break IoResult::keep_open(total, false),
        IoStatus::Error => {
          self.failure_reason = "read error".to_string();
          break IoResult::close(total, false);
        },
      }
    };
    self.bytes_read_through_envoy += total as u64;
    result
  }

  fn read_spliced(
    &mut self,
    envoy: &EnvoyTransportSocketImpl,
    direction: &mut Direction,
    writer: Option<SocketPtr>,
  ) -> IoResult {
    let Mode::Spliced(pipe) = &direction.mode else {
      return IoResult::keep_open(0, false);
    };
    let pipe_fd = pipe.write_fd;
    let mut spliced = 0;
    let mut error = None;
    while !direction.reader_eof {
      match sys::splice(self.fd, pipe_fd, MAX_SPLICE_BYTES) {
        Ok(0) => direction.reader_eof = true,
        Ok(n) => {
          direction.pipe_bytes += n;
          spliced += n;
        },
        Err(errno) if errno == sys::EAGAIN => {
          // A full pipe raises no socket event, so the writer wakes the reader instead.
          direction.reader_waiting = direction.pipe_bytes > 0;
          break;
        },
        Err(errno) => {
          error = Some(errno);
          break;
        },
      }
    }
    if spliced > 0 {
      envoy.add_wire_bytes(spliced as u64, 0);
      wake_writer(writer);
    }
    if let Some(errno) = error {
      self.failure_reason = format!("splice read error {errno}");
      return close_result(errno == sys::ECONNRESET, direction.pipe_bytes > 0, spliced);
    }
    // Envoy closes the connection once both directions ended and its buffers are empty, so the end
    // of the stream waits for the writer to drain the pipe.
    let end_stream = direction.reader_eof && direction.pipe_bytes == 0;
    if end_stream {
      direction.mode = Mode::Disabled;
    }
    IoResult::keep_open(spliced, end_stream)
  }

  // Returns the bytes spliced and the errno of a failed splice.
  fn write_spliced(
    &self,
    envoy: &EnvoyTransportSocketImpl,
    direction: &mut Direction,
    reader: Option<SocketPtr>,
  ) -> (usize, Option<i32>) {
    let Mode::Spliced(pipe) = &direction.mode else {
      return (0, None);
    };
    let pipe_fd = pipe.read_fd;
    let mut spliced = 0;
    let mut error = None;
    while direction.pipe_bytes > 0 {
      match sys::splice(pipe_fd, self.fd, direction.pipe_bytes) {
        Ok(0) => break,
        Ok(n) => {
          direction.pipe_bytes -= n.min(direction.pipe_bytes);
          spliced += n;
        },
        Err(errno) if errno == sys::EAGAIN => break,
        Err(errno) => {
          error = Some(errno);
          break;
        },
      }
    }
    if spliced > 0 {
      envoy.add_wire_bytes(0, spliced as u64);
      if direction.reader_waiting || (direction.reader_eof && direction.pipe_bytes == 0) {
        direction.reader_waiting = false;
        wake_reader(reader);
      }
    }
    (spliced, error)
  }
}

impl Drop for SpliceTransportSocket {
  fn drop(&mut self) {
    self.detach();
  }
}

impl TransportSocket<EnvoyTransportSocketImpl> for SpliceTransportSocket {
  fn on_set_callbacks(&mut self, envoy: &mut EnvoyTransportSocketImpl) {
    self.register_upstream(envoy);
  }

  fn on_connected(&mut self, envoy: &mut EnvoyTransportSocketImpl) {
    envoy.raise_event(ConnectionEvent::Connected);
  }

  fn on_do_read(&mut self, envoy: &mut EnvoyTransportSocketImpl) -> IoResult {
    self.claim_upstream(envoy);
    let Some(relay) = self.relay.clone() else {
      return self.read_buffered(envoy);
    };
    let mut relay = relay.borrow_mut();
    self.maybe_start_splicing(envoy, &mut relay);
    let writer = relay.peer(self.role);
    let direction = relay.outbound(self.role);
    if matches!(direction.mode, Mode::Spliced(_)) {
      return self.read_spliced(envoy, direction, writer);
    }
    let result = self.read_buffered(envoy);
    if result.bytes_processed > 0 {
      direction.bytes_arrived_at_writer = None;
    }
    result
  }

  fn on_do_write(&mut self, envoy: &mut EnvoyTransportSocketImpl, end_stream: bool) -> IoResult {
    self.claim_upstream(envoy);
    let relay = self.relay.clone();
    let mut relay = relay.as_ref().map(|relay| relay.borrow_mut());
    let pending = envoy.write_buffer_length();
    let mut outbound_pipe_has_bytes = false;
    if let Some(relay) = relay.as_deref_mut() {
      relay.inbound(self.role).bytes_arrived_at_writer =
        Some(self.bytes_drained_from_envoy + pending as u64);
      outbound_pipe_has_bytes = relay.outbound(self.role).pipe_bytes > 0;
    }

    // The write buffer holds the bytes that preceded any spliced ones, so it goes first.
    let mut written = 0;
    if pending > 0 {
      loop {
        let (status, n) = envoy.io_write_from_write_buffer();
        match status {
          IoStatus::Success if n > 0 => {
            written += n;
            self.bytes_drained_from_envoy += n as u64;
          },
          IoStatus::Success => break,
          IoStatus::Again => return IoResult::keep_open(written, false),
          IoStatus::Error => {
            self.failure_reason = "write error".to_string();
            return close_result(false, outbound_pipe_has_bytes, written);
          },
        }
      }
    }

    let mut spliced = 0;
    let mut pipe_empty = true;
    if let Some(relay) = relay.as_deref_mut() {
      let reader = relay.peer(self.role);
      let direction = relay.inbound(self.role);
      let (n, error) = self.write_spliced(envoy, direction, reader);
      spliced = n;
      if let Some(errno) = error {
        self.failure_reason = format!("splice write error {errno}");
        return close_result(
          errno == sys::ECONNRESET,
          outbound_pipe_has_bytes,
          written + spliced,
        );
      }
      pipe_empty = direction.pipe_bytes == 0;
    }

    if end_stream && pipe_empty && !self.write_shutdown {
      envoy.io_shutdown_write();
      self.write_shutdown = true;
    }
    IoResult::keep_open(written + spliced, false)
  }

  fn on_close(
    &mut self,
    _envoy: &mut EnvoyTransportSocketImpl,
    _event: ConnectionEvent,
    _abort_reset: bool,
  ) {
    self.detach();
  }

  fn get_protocol(&self, _envoy: &mut EnvoyTransportSocketImpl) -> String {
    String::new()
  }

  fn get_failure_reason(&self, _envoy: &mut EnvoyTransportSocketImpl) -> String {
    self.failure_reason.clone()
  }

  fn can_flush_close(&self, _envoy: &mut EnvoyTransportSocketImpl) -> bool {
    true
  }

  fn start_secure_transport(&mut self, _envoy: &mut EnvoyTransportSocketImpl) -> bool {
    false
  }
}

#[cfg(target_os = "linux")]
mod sys {
  use std::os::raw::{c_int, c_uint};

  pub const EAGAIN: i32 = 11;
  pub const ECONNRESET: i32 = 104;
  const EINTR: i32 = 4;
  const O_NONBLOCK: c_int = 0o4000;
  const O_CLOEXEC: c_int = 0o2000000;
  const F_GETFL: c_int = 3;
  const SPLICE_F_MOVE: c_uint = 1;
  const SPLICE_F_NONBLOCK: c_uint = 2;

  extern "C" {
    fn pipe2(fds: *mut c_int, flags: c_int) -> c_int;
    #[link_name = "splice"]
    fn libc_splice(
      fd_in: c_int,
      off_in: *mut i64,
      fd_out: c_int,
      off_out: *mut i64,
      len: usize,
      flags: c_uint,
    ) -> isize;
    fn fcntl(fd: c_int, cmd: c_int, ...) -> c_int;
    fn close(fd: c_int) -> c_int;
  }

  pub fn is_nonblocking(fd: c_int) -> bool {
    let flags = unsafe { fcntl(fd, F_GETFL) };
    flags >= 0 && flags & O_NONBLOCK != 0
  }

  // Moves up to `len` bytes between a socket and a pipe without blocking. Returns the number of
  // bytes moved, zero at the end of the stream, or the errno.
  pub fn splice(fd_in: c_int, fd_out: c_int, len: usize) -> Result<usize, i32> {
    loop {
      let n = unsafe {
        libc_splice(
          fd_in,
          std::ptr::null_mut(),
          fd_out,
          std::ptr::null_mut(),
          len,
          SPLICE_F_MOVE | SPLICE_F_NONBLOCK,
        )
      };
      if n >= 0 {
        return Ok(n as usize);
      }
      let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
      if errno != EINTR {
        return Err(errno);
      }
    }
  }

  pub struct Pipe {
    pub read_fd: c_int,
    pub write_fd: c_int,
  }

  impl Pipe {
    pub fn new() -> Option<Pipe> {
      let mut fds = [0 as c_int; 2];
      if unsafe { pipe2(fds.as_mut_ptr(), O_NONBLOCK | O_CLOEXEC) } != 0 {
        return None;
      }
      Some(Pipe {
        read_fd: fds[0],
        write_fd: fds[1],
      })
    }
  }

  impl Drop for Pipe {
    fn drop(&mut self) {
      unsafe {
        close(self.read_fd);
        close(self.write_fd);
      }
    }
  }
}

// splice(2) is Linux only, so elsewhere every direction stays buffered.
#[cfg(not(target_os = "linux"))]
mod sys {
  pub const EAGAIN: i32 = 11;
  pub const ECONNRESET: i32 = 104;

  pub fn is_nonblocking(_fd: i32) -> bool {
    false
  }

  pub fn splice(_fd_in: i32, _fd_out: i32, _len: usize) -> Result<usize, i32> {
    Err(EAGAIN)
  }

  pub struct Pipe {
    pub read_fd: i32,
    pub write_fd: i32,
  }

  impl Pipe {
    pub fn new() -> Option<Pipe> {
      None
    }
  }
}
