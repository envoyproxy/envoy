// Passthrough transport socket implementation using high-level SDK abstractions.
// This demonstrates the recommended way to write transport socket modules.

use envoy_proxy_dynamic_modules_rust_sdk::transport_socket::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

// Declare init functions using the SDK macro.
declare_transport_socket_init_functions!(init, new_transport_socket_config);

/// Program initialization.
///
/// This is called once when the dynamic module is loaded.
/// Returning false will cause the module to be unloaded.
fn init() -> bool {
  eprintln!("[PASSTHROUGH] Module initialized");
  true
}

/// Create transport socket configuration.
///
/// This is called when a new transport socket configuration is created in Envoy.
fn new_transport_socket_config<EC: EnvoyTransportSocketConfig, TS: EnvoyTransportSocket>(
  _envoy_socket_config: &mut EC,
  name: &str,
  _config: &[u8],
  is_upstream: bool,
) -> Option<Box<dyn TransportSocketConfig<TS>>> {
  eprintln!(
    "[PASSTHROUGH CONFIG] Creating socket: name={}, upstream={}",
    name, is_upstream
  );
  Some(Box::new(PassthroughConfig {}))
}

/// Passthrough socket configuration.
///
/// This represents the configuration for passthrough sockets.
struct PassthroughConfig {}

impl<TS: EnvoyTransportSocket> TransportSocketConfig<TS> for PassthroughConfig {
  /// Create a new transport socket instance for each connection.
  fn new_transport_socket(&mut self, _envoy: &mut TS) -> Box<dyn TransportSocket<TS>> {
    eprintln!("[PASSTHROUGH CONFIG] Creating new socket instance");
    Box::new(PassthroughSocket {})
  }
}

/// Passthrough socket implementation.
///
/// This socket passes data through unchanged.
/// It demonstrates the basic transport socket lifecycle.
struct PassthroughSocket {}

impl<TS: EnvoyTransportSocket> TransportSocket<TS> for PassthroughSocket {
  /// Called when the connection is established.
  fn on_connected(&mut self, envoy: &mut TS) {
    envoy.log(LogLevel::Debug, "Passthrough socket connected");
    // The C++ code will raise the Connected event after calling this function.
  }

  /// Read data from the connection.
  ///
  /// For passthrough, we simply read from the underlying IO handle and return it unchanged.
  /// Returns (bytes_read, should_close, end_stream_read).
  fn do_read(&mut self, envoy: &mut TS, buffer: &mut [u8]) -> (u64, bool, bool) {
    match envoy.read_from_io_handle(buffer) {
      Ok(bytes_read) => {
        if bytes_read > 0 {
          envoy.log(LogLevel::Trace, &format!("Read {} bytes", bytes_read));
        }
        (bytes_read, false, false)
      },
      Err(IoError::Again) => (0, false, false),
      Err(IoError::Error(code)) => {
        envoy.log(LogLevel::Error, &format!("Read error: code={}", code));
        (0, true, false)
      },
    }
  }

  /// Write data to the connection.
  ///
  /// For passthrough, we simply write to the underlying IO handle unchanged.
  /// Returns (bytes_written, should_close).
  fn do_write(&mut self, envoy: &mut TS, data: &[u8], end_stream: bool) -> (u64, bool) {
    match envoy.write_to_io_handle(data) {
      Ok(bytes_written) => {
        if bytes_written > 0 {
          envoy.log(LogLevel::Trace, &format!("Wrote {} bytes", bytes_written));
        }
        (bytes_written, end_stream)
      },
      Err(IoError::Again) => (0, false),
      Err(IoError::Error(code)) => {
        envoy.log(LogLevel::Error, &format!("Write error: code={}", code));
        (0, true)
      },
    }
  }

  /// Called when the socket is closed.
  fn on_close(&mut self, envoy: &mut TS, event: abi::envoy_dynamic_module_type_connection_event) {
    envoy.log(
      LogLevel::Debug,
      &format!("Passthrough socket closing: event={:?}", event),
    );
  }

  /// Returns the protocol string for this socket.
  fn protocol(&self) -> Option<&str> {
    Some("passthrough")
  }

  /// Returns whether the socket can be flushed and closed.
  fn can_flush_close(&self) -> bool {
    true
  }
}
