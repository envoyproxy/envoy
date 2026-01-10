//! A no-op transport socket implementation for testing.
//!
//! This transport socket does nothing, returning immediately without I/O.
//! Used for testing the transport socket infrastructure without real I/O.

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
  Some(Box::new(NoopFactoryConfig {}))
}

struct NoopFactoryConfig {}

impl TransportSocketFactoryConfig<EnvoyTransportSocketImpl> for NoopFactoryConfig {
  fn new_transport_socket(
    &self,
    _envoy_socket: &mut EnvoyTransportSocketImpl,
  ) -> Box<dyn TransportSocket<EnvoyTransportSocketImpl>> {
    Box::new(NoopTransportSocket {})
  }
}

struct NoopTransportSocket {}

impl TransportSocket<EnvoyTransportSocketImpl> for NoopTransportSocket {
  fn on_connected(&mut self, envoy_socket: &mut EnvoyTransportSocketImpl) {
    envoy_socket.raise_event(ConnectionEvent::Connected);
  }

  fn do_read(&mut self, _envoy_socket: &mut EnvoyTransportSocketImpl) -> IoResult {
    // Return immediately without doing any I/O.
    IoResult::keep_open(0, false)
  }

  fn do_write(
    &mut self,
    envoy_socket: &mut EnvoyTransportSocketImpl,
    write_buffer_length: usize,
    _end_stream: bool,
  ) -> IoResult {
    // Drain the write buffer without doing real I/O.
    if write_buffer_length > 0 {
      envoy_socket.write_buffer_drain(write_buffer_length);
    }
    IoResult::keep_open(write_buffer_length as u64, false)
  }

  fn on_close(&mut self, _envoy_socket: &mut EnvoyTransportSocketImpl, _event: ConnectionEvent) {
    // No-op close.
  }

  fn get_protocol(&self) -> &str {
    "noop-protocol"
  }

  fn get_failure_reason(&self) -> &str {
    ""
  }

  fn can_flush_close(&self) -> bool {
    true
  }
}
