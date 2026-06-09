//! Reference transport socket module used by the dynamic module transport socket integration test.
//!
//! It applies a symmetric XOR transform to the raw connection bytes. The same transform runs on
//! both reads and writes, so a peer that echoes the transformed bytes observes the original
//! plaintext. A zero key behaves as a passthrough.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_transport_socket_init_functions!(init, new_transport_socket_factory_config_fn);

// Matches Envoy's default per-read size so the read loop drains the socket in one pass.
const READ_CHUNK_SIZE: usize = 16384;
// Used when the "xor" socket is configured without an explicit key.
const DEFAULT_XOR_KEY: u8 = 0x5a;

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the
/// [`envoy_proxy_dynamic_modules_rust_sdk::NewTransportSocketFactoryConfigFunction`] signature.
fn new_transport_socket_factory_config_fn<ETS: EnvoyTransportSocket>(
  name: &str,
  config: &[u8],
  _is_upstream: bool,
) -> Option<Box<dyn TransportSocketFactoryConfig<ETS>>> {
  match name {
    "passthrough" => Some(Box::new(XorTransportSocketConfig { key: 0 })),
    "xor" => {
      let key = config.first().copied().unwrap_or(DEFAULT_XOR_KEY);
      Some(Box::new(XorTransportSocketConfig { key }))
    },
    _ => None,
  }
}

struct XorTransportSocketConfig {
  key: u8,
}

impl<ETS: EnvoyTransportSocket> TransportSocketFactoryConfig<ETS> for XorTransportSocketConfig {
  fn new_transport_socket(&self, _envoy: &mut ETS) -> Box<dyn TransportSocket<ETS>> {
    Box::new(XorTransportSocket {
      key: self.key,
      write_shutdown: false,
      write_scratch: Vec::new(),
      failure_reason: String::new(),
    })
  }
}

struct XorTransportSocket {
  key: u8,
  write_shutdown: bool,
  write_scratch: Vec<u8>,
  // Last I/O failure, surfaced through get_failure_reason like a real secure transport.
  failure_reason: String,
}

fn apply_xor(bytes: &mut [u8], key: u8) {
  if key == 0 {
    return;
  }
  for b in bytes {
    *b ^= key;
  }
}

impl<ETS: EnvoyTransportSocket> TransportSocket<ETS> for XorTransportSocket {
  fn on_set_callbacks(&mut self, _envoy: &mut ETS) {}

  fn on_connected(&mut self, envoy: &mut ETS) {
    // A real secure transport finishes its handshake here. Mirror that by inspecting the
    // connection endpoints and flushing any buffered output before signaling readiness.
    let (remote, _) = envoy.get_remote_address();
    let (local, _) = envoy.get_local_address();
    if !remote.is_empty() && !local.is_empty() {
      envoy.flush_write_buffer();
    }
    envoy.raise_event(ConnectionEvent::Connected);
  }

  fn on_do_read(&mut self, envoy: &mut ETS) -> IoResult {
    let mut total = 0;
    let mut buffer = [0u8; READ_CHUNK_SIZE];
    loop {
      let (status, n) = envoy.io_read(&mut buffer);
      match status {
        IoStatus::Success => {
          if n == 0 {
            return IoResult::keep_open(total, true);
          }
          apply_xor(&mut buffer[..n], self.key);
          envoy.read_buffer_add(&buffer[..n]);
          total += n;
          if envoy.should_drain_read_buffer() {
            envoy.set_is_readable();
            return IoResult::keep_open(total, false);
          }
        },
        IoStatus::Again => return IoResult::keep_open(total, false),
        IoStatus::Error => {
          self.failure_reason = "read error".to_string();
          return IoResult::close(total, false);
        },
      }
    }
  }

  fn on_do_write(&mut self, envoy: &mut ETS, end_stream: bool) -> IoResult {
    // Snapshot and transform the whole write buffer once. The transform is position-independent so
    // partial writes are handled by draining only the bytes the socket accepted.
    self.write_scratch.clear();
    envoy.copy_write_buffer(&mut self.write_scratch);
    apply_xor(&mut self.write_scratch, self.key);

    let mut written = 0;
    let action = loop {
      if written == self.write_scratch.len() {
        if end_stream && !self.write_shutdown {
          envoy.io_shutdown_write();
          self.write_shutdown = true;
        }
        break PostIoAction::KeepOpen;
      }
      let (status, n) = envoy.io_write(&self.write_scratch[written..]);
      match status {
        IoStatus::Success => {
          if n == 0 {
            break PostIoAction::KeepOpen;
          }
          written += n;
        },
        IoStatus::Again => break PostIoAction::KeepOpen,
        IoStatus::Error => {
          self.failure_reason = "write error".to_string();
          break PostIoAction::Close;
        },
      }
    };
    envoy.write_buffer_drain(written);
    match action {
      PostIoAction::KeepOpen => IoResult::keep_open(written, false),
      PostIoAction::Close => IoResult::close(written, false),
    }
  }

  fn on_close(&mut self, _envoy: &mut ETS, _event: ConnectionEvent, _abort_reset: bool) {}

  fn get_protocol(&self, _envoy: &mut ETS) -> String {
    // A configured (non-zero key) socket reports a negotiated protocol name, mirroring how a secure
    // transport surfaces its ALPN result. The passthrough socket negotiates nothing.
    if self.key != 0 {
      "xor".to_string()
    } else {
      String::new()
    }
  }

  fn get_failure_reason(&self, _envoy: &mut ETS) -> String {
    self.failure_reason.clone()
  }

  fn can_flush_close(&self, _envoy: &mut ETS) -> bool {
    true
  }

  fn start_secure_transport(&mut self, _envoy: &mut ETS) -> bool {
    // The XOR transform stands in for the secure layer, so a configured (non-zero) key reports
    // that secure transport is active. The passthrough socket reports false.
    self.key != 0
  }
}
