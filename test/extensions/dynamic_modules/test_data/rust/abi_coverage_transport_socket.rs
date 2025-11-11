// A transport socket implementation specifically designed to maximize ABI coverage.
// This module exercises all ABI functions to ensure comprehensive test coverage.

use envoy_proxy_dynamic_modules_rust_sdk::transport_socket::*;
use envoy_proxy_dynamic_modules_rust_sdk::{abi, *};

// Declare init functions using the SDK macro.
declare_transport_socket_init_functions!(init, new_abi_coverage_config);

/// Program initialization.
fn init() -> bool {
  eprintln!("[ABI_COVERAGE] Module initialized");
  true
}

/// Create transport socket configuration.
fn new_abi_coverage_config<EC: EnvoyTransportSocketConfig, TS: EnvoyTransportSocket>(
  _envoy_socket_config: &mut EC,
  name: &str,
  _config: &[u8],
  is_upstream: bool,
) -> Option<Box<dyn TransportSocketConfig<TS>>> {
  eprintln!(
    "[ABI_COVERAGE CONFIG] Creating config: name={}, is_upstream={}",
    name, is_upstream
  );

  Some(Box::new(AbiCoverageConfig { is_upstream }))
}

struct AbiCoverageConfig {
  is_upstream: bool,
}

impl Drop for AbiCoverageConfig {
  fn drop(&mut self) {
    eprintln!("[ABI_COVERAGE CONFIG] Config destroyed");
  }
}

impl<TS: EnvoyTransportSocket> TransportSocketConfig<TS> for AbiCoverageConfig {
  fn new_transport_socket(&mut self, _envoy: &mut TS) -> Box<dyn TransportSocket<TS>> {
    eprintln!("[ABI_COVERAGE CONFIG] Creating new socket instance");
    Box::new(AbiCoverageSocket {
      is_upstream: self.is_upstream,
      connected: false,
      bytes_read: 0,
      bytes_written: 0,
    })
  }
}

struct AbiCoverageSocket {
  #[allow(dead_code)]
  is_upstream: bool,
  connected: bool,
  bytes_read: usize,
  bytes_written: usize,
}

impl Drop for AbiCoverageSocket {
  fn drop(&mut self) {
    eprintln!("[ABI_COVERAGE] Socket destroyed");
  }
}

impl<TS: EnvoyTransportSocket> TransportSocket<TS> for AbiCoverageSocket {
  fn on_connected(&mut self, envoy: &mut TS) {
    self.connected = true;
    eprintln!("[ABI_COVERAGE] onConnected called");

    // Exercise ALL log levels to cover all switch cases in abi_impl.cc.
    envoy.log(LogLevel::Trace, "[ABI_COVERAGE] Test trace log");
    envoy.log(LogLevel::Debug, "[ABI_COVERAGE] Test debug log");
    envoy.log(LogLevel::Info, "[ABI_COVERAGE] Test info log");
    envoy.log(LogLevel::Warn, "[ABI_COVERAGE] Test warn log");
    envoy.log(LogLevel::Error, "[ABI_COVERAGE] Test error log");
    envoy.log(LogLevel::Critical, "[ABI_COVERAGE] Test critical log");

    // Exercise get_fd to cover abi_impl.cc line 27.
    let fd = envoy.get_fd();
    eprintln!("[ABI_COVERAGE] File descriptor: {}", fd);

    // Exercise should_drain_read_buffer to cover abi_impl.cc lines 86-95.
    let should_drain = envoy.should_drain_read_buffer();
    eprintln!("[ABI_COVERAGE] Should drain: {}", should_drain);

    // Exercise set_readable to cover abi_impl.cc lines 97-106.
    envoy.set_readable();
    eprintln!("[ABI_COVERAGE] Set readable called");

    // Exercise flush_write_buffer to cover abi_impl.cc lines 138-147.
    envoy.flush_write_buffer();
    eprintln!("[ABI_COVERAGE] Flush write buffer called");

    // Exercise raise_event to cover all connection event types (abi_impl.cc lines 108-136).
    // This exercises all switch cases: RemoteClose, LocalClose, Connected, ConnectedZeroRtt.
    eprintln!("[ABI_COVERAGE] Testing raise_event with all event types");

    // Note: We can't actually test these because they would close the connection.
    // But we document them here for awareness.
    // envoy.raise_event(abi::envoy_dynamic_module_type_connection_event_RemoteClose);
    // envoy.raise_event(abi::envoy_dynamic_module_type_connection_event_LocalClose);
    // envoy.raise_event(abi::envoy_dynamic_module_type_connection_event_Connected);
    // envoy.raise_event(abi::envoy_dynamic_module_type_connection_event_ConnectedZeroRtt);
  }

  fn do_read(&mut self, envoy: &mut TS, buffer: &mut [u8]) -> (u64, bool, bool) {
    // Exercise read_from_io_handle to cover abi_impl.cc lines 30-58.
    match envoy.read_from_io_handle(buffer) {
      Ok(bytes_read) => {
        self.bytes_read += bytes_read as usize;
        eprintln!(
          "[ABI_COVERAGE] Read {} bytes (total: {})",
          bytes_read, self.bytes_read
        );

        // Exercise set_readable after successful read for additional coverage.
        if bytes_read > 0 {
          envoy.set_readable();
        }

        (bytes_read, false, false)
      },
      Err(IoError::Again) => {
        // Cover the EAGAIN path in abi_impl.cc lines 44-46.
        eprintln!("[ABI_COVERAGE] Read would block (EAGAIN)");
        (0, false, false)
      },
      Err(IoError::Error(code)) => {
        // Cover the error path in abi_impl.cc line 47.
        eprintln!("[ABI_COVERAGE] Read error: {}", code);
        (0, true, false)
      },
    }
  }

  fn do_write(&mut self, envoy: &mut TS, data: &[u8], end_stream: bool) -> (u64, bool) {
    // Exercise write_to_io_handle to cover abi_impl.cc lines 60-84.
    match envoy.write_to_io_handle(data) {
      Ok(bytes_written) => {
        self.bytes_written += bytes_written as usize;
        eprintln!(
          "[ABI_COVERAGE] Wrote {} bytes (total: {})",
          bytes_written, self.bytes_written
        );

        // Exercise flush_write_buffer after successful write for additional coverage.
        if bytes_written > 0 {
          envoy.flush_write_buffer();
        }

        (bytes_written, end_stream)
      },
      Err(IoError::Again) => {
        // Cover the EAGAIN path in abi_impl.cc lines 76-78.
        eprintln!("[ABI_COVERAGE] Write would block (EAGAIN)");
        (0, end_stream)
      },
      Err(IoError::Error(code)) => {
        // Cover the error path in abi_impl.cc line 79.
        eprintln!("[ABI_COVERAGE] Write error: {}", code);
        (0, end_stream)
      },
    }
  }

  fn on_close(&mut self, envoy: &mut TS, _event: abi::envoy_dynamic_module_type_connection_event) {
    // Exercise different log levels in close handler.
    envoy.log(LogLevel::Info, "[ABI_COVERAGE] Connection closing");
    envoy.log(LogLevel::Debug, "[ABI_COVERAGE] Close event received");

    eprintln!("[ABI_COVERAGE] on_close called");
    self.connected = false;
  }

  // Implement protocol() to test protocol string caching in transport_socket.cc.
  fn protocol(&self) -> Option<&str> {
    Some("abi_coverage_v1")
  }

  // Implement failure_reason() to test failure reason caching in transport_socket.cc.
  fn failure_reason(&self) -> Option<&str> {
    if !self.connected {
      Some("Not connected")
    } else {
      None
    }
  }

  fn can_flush_close(&self) -> bool {
    // Test can_flush_close method coverage.
    true
  }

  // Implement get_ssl_info to test SSL info methods in transport_socket.cc.
  fn get_ssl_info(&self) -> Option<&SslConnectionInfo> {
    // We don't have real SSL info in this test socket, but we could return
    // a dummy value to exercise the code paths.
    None
  }

  fn configure_initial_congestion_window(
    &mut self,
    bandwidth_bits_per_sec: u64,
    rtt_microseconds: u64,
  ) {
    // Exercise this method to increase coverage.
    eprintln!(
      "[ABI_COVERAGE] configure_initial_congestion_window: bandwidth={}, rtt={}",
      bandwidth_bits_per_sec, rtt_microseconds
    );
  }

  fn start_secure_transport(&mut self, _envoy: &mut TS) -> bool {
    // Exercise this method to increase coverage.
    eprintln!("[ABI_COVERAGE] start_secure_transport called");
    true
  }
}
