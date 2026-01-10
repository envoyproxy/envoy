//! A simple passthrough transport socket implementation for testing.
//!
//! This transport socket simply passes through all read/write operations without any modification.

use envoy_proxy_dynamic_modules_rust_sdk::transport_socket::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_transport_socket_init_functions!(program_init, new_factory_config);

fn program_init() -> bool {
  true
}

fn new_factory_config<ETSFC: EnvoyTransportSocketFactoryConfig>(
  _envoy_factory_config: &mut ETSFC,
  _socket_name: &str,
  _socket_config: &[u8],
  _is_upstream: bool,
) -> Option<Box<dyn TransportSocketFactoryConfig<EnvoyTransportSocketImpl>>> {
  Some(Box::new(PassthroughFactoryConfig {}))
}

struct PassthroughFactoryConfig {}

impl TransportSocketFactoryConfig<EnvoyTransportSocketImpl> for PassthroughFactoryConfig {
  fn new_transport_socket(
    &self,
    _envoy_socket: &mut EnvoyTransportSocketImpl,
  ) -> Box<dyn TransportSocket<EnvoyTransportSocketImpl>> {
    Box::new(PassthroughTransportSocket {})
  }
}

struct PassthroughTransportSocket {}

impl TransportSocket<EnvoyTransportSocketImpl> for PassthroughTransportSocket {
  fn on_connected(&mut self, envoy_socket: &mut EnvoyTransportSocketImpl) {
    // Raise the connected event to notify the connection is established.
    envoy_socket.raise_event(ConnectionEvent::Connected);
  }

  fn do_read(&mut self, envoy_socket: &mut EnvoyTransportSocketImpl) -> IoResult {
    // Simple passthrough: read from raw socket and add to read buffer.
    let mut buffer = [0u8; 16384];
    let mut total_read = 0u64;
    let mut end_stream = false;

    loop {
      match envoy_socket.io_read(&mut buffer) {
        Ok(0) => {
          // End of stream.
          end_stream = true;
          break;
        },
        Ok(n) => {
          envoy_socket.read_buffer_add(&buffer[.. n]);
          total_read += n as u64;

          // Check if we should drain the read buffer.
          if envoy_socket.should_drain_read_buffer() {
            envoy_socket.set_is_readable();
            break;
          }
        },
        Err(_) => {
          // Would block or error.
          break;
        },
      }
    }

    IoResult::keep_open(total_read, end_stream)
  }

  fn do_write(
    &mut self,
    envoy_socket: &mut EnvoyTransportSocketImpl,
    _write_buffer_length: usize,
    _end_stream: bool,
  ) -> IoResult {
    // Simple passthrough: write slices to raw socket.
    let slices = envoy_socket.write_buffer_get_slices();
    let mut total_written = 0u64;

    for slice in slices {
      match envoy_socket.io_write(slice) {
        Ok(n) => {
          total_written += n as u64;
          if n < slice.len() {
            // Partial write, stop here.
            break;
          }
        },
        Err(_) => {
          // Would block or error.
          break;
        },
      }
    }

    if total_written > 0 {
      envoy_socket.write_buffer_drain(total_written as usize);
    }

    IoResult::keep_open(total_written, false)
  }
}
