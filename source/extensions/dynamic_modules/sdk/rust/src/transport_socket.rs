// High-level Rust SDK for Transport Socket Dynamic Modules.

use crate::abi;
use std::sync::OnceLock;

/// The function signature for creating new transport socket configurations.
pub type NewTransportSocketConfigFunction<EC, TS> =
  fn(
    envoy_socket_config: &mut EC,
    name: &str,
    config: &[u8],
    is_upstream: bool,
  ) -> Option<Box<dyn TransportSocketConfig<TS>>>;

/// The global init function for transport socket configurations.
pub static NEW_TRANSPORT_SOCKET_CONFIG_FUNCTION: OnceLock<
  NewTransportSocketConfigFunction<EnvoyTransportSocketConfigImpl, EnvoyTransportSocketImpl>,
> = OnceLock::new();

/// Macro to declare init functions for transport socket modules.
#[macro_export]
macro_rules! declare_transport_socket_init_functions {
  ($f:ident, $new_transport_socket_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      envoy_proxy_dynamic_modules_rust_sdk::transport_socket::NEW_TRANSPORT_SOCKET_CONFIG_FUNCTION
        .get_or_init(|| $new_transport_socket_config_fn);
      if ($f()) {
        envoy_proxy_dynamic_modules_rust_sdk::abi::kAbiVersion.as_ptr()
          as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
}

/// The trait that represents the configuration for a transport socket.
pub trait TransportSocketConfig<TS: EnvoyTransportSocket> {
  fn new_transport_socket(&mut self, _envoy: &mut TS) -> Box<dyn TransportSocket<TS>> {
    panic!("not implemented");
  }
}

/// The trait that corresponds to a transport socket for each connection.
pub trait TransportSocket<TS: EnvoyTransportSocket> {
  fn on_connected(&mut self, _envoy: &mut TS) {}
  fn do_read(&mut self, _envoy: &mut TS, _buffer: &mut [u8]) -> (u64, bool, bool) {
    (0, false, false)
  }
  fn do_write(&mut self, _envoy: &mut TS, _data: &[u8], _end_stream: bool) -> (u64, bool) {
    (0, false)
  }
  fn on_close(&mut self, _envoy: &mut TS, _event: abi::envoy_dynamic_module_type_connection_event) {
  }
  fn protocol(&self) -> Option<&str> {
    None
  }
  fn failure_reason(&self) -> Option<&str> {
    None
  }
  fn can_flush_close(&self) -> bool {
    true
  }
  fn get_ssl_info(&self) -> Option<&SslConnectionInfo> {
    None
  }
  fn configure_initial_congestion_window(
    &mut self,
    _bandwidth_bits_per_sec: u64,
    _rtt_microseconds: u64,
  ) {
  }
  fn start_secure_transport(&mut self, _envoy: &mut TS) -> bool {
    true
  }
}

/// SSL/TLS connection information.
#[derive(Debug, Clone)]
pub struct SslConnectionInfo {
  pub tls_version: String,
  pub cipher_suite: String,
  pub alpn_protocol: String,
  pub sni: String,
  pub peer_certificates: Vec<Vec<u8>>,
  pub peer_certificate_validated: bool,
}

/// Trait for accessing Envoy transport socket configuration context.
pub trait EnvoyTransportSocketConfig {
  // Configuration-level operations.
}

/// Trait for accessing Envoy transport socket runtime context.
pub trait EnvoyTransportSocket {
  fn read_from_io_handle(&mut self, buffer: &mut [u8]) -> Result<u64, IoError>;
  fn write_to_io_handle(&self, data: &[u8]) -> Result<u64, IoError>;
  fn raise_event(&mut self, event: abi::envoy_dynamic_module_type_connection_event);
  fn set_readable(&mut self);
  fn should_drain_read_buffer(&self) -> bool;
  fn flush_write_buffer(&mut self);
  fn get_fd(&self) -> i32;
  fn log(&self, level: LogLevel, message: &str);
}

/// IO error types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IoError {
  Again,
  Error(i32),
}

/// Log levels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
  Trace    = 0,
  Debug    = 1,
  Info     = 2,
  Warn     = 3,
  Error    = 4,
  Critical = 5,
}

/// Implementation of EnvoyTransportSocketConfig.
pub struct EnvoyTransportSocketConfigImpl {
  pub raw_ptr: abi::envoy_dynamic_module_type_transport_socket_config_envoy_ptr,
}

impl EnvoyTransportSocketConfig for EnvoyTransportSocketConfigImpl {}

/// Implementation of EnvoyTransportSocket.
pub struct EnvoyTransportSocketImpl {
  pub raw_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
}

impl EnvoyTransportSocketImpl {
  pub fn new(raw_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr) -> Self {
    Self { raw_ptr }
  }
}

impl EnvoyTransportSocket for EnvoyTransportSocketImpl {
  fn read_from_io_handle(&mut self, buffer: &mut [u8]) -> Result<u64, IoError> {
    unsafe {
      let mut bytes_read: u64 = 0;
      let result = abi::envoy_dynamic_module_callback_transport_socket_io_handle_read(
        self.raw_ptr,
        buffer.as_mut_ptr() as *mut std::ffi::c_void,
        buffer.len(),
        &mut bytes_read,
      );

      match result {
        0 => Ok(bytes_read),
        -2 => Err(IoError::Again),
        code => Err(IoError::Error(code)),
      }
    }
  }

  fn write_to_io_handle(&self, data: &[u8]) -> Result<u64, IoError> {
    unsafe {
      let mut bytes_written: u64 = 0;
      let result = abi::envoy_dynamic_module_callback_transport_socket_io_handle_write(
        self.raw_ptr,
        data.as_ptr() as *const std::ffi::c_void,
        data.len(),
        &mut bytes_written,
      );

      match result {
        0 => Ok(bytes_written),
        -2 => Err(IoError::Again),
        code => Err(IoError::Error(code)),
      }
    }
  }

  fn raise_event(&mut self, event: abi::envoy_dynamic_module_type_connection_event) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_raise_event(self.raw_ptr, event);
    }
  }

  fn set_readable(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_set_readable(self.raw_ptr);
    }
  }

  fn should_drain_read_buffer(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(self.raw_ptr)
    }
  }

  fn flush_write_buffer(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_flush_write_buffer(self.raw_ptr);
    }
  }

  fn get_fd(&self) -> i32 {
    unsafe { abi::envoy_dynamic_module_callback_transport_socket_get_io_handle_fd(self.raw_ptr) }
  }

  fn log(&self, level: LogLevel, message: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_transport_socket_log(
        level as i32,
        message.as_ptr() as *const std::os::raw::c_char,
        message.len(),
      );
    }
  }
}

// Macro helpers.
macro_rules! wrap_into_c_void_ptr {
  ($t:expr) => {{
    let boxed = Box::new($t);
    Box::into_raw(boxed) as *const ::std::os::raw::c_void
  }};
}

/// This macro is used to drop the Box<dyn T> and the underlying object when the C code calls the
/// destroy function. This is a counterpart to [`wrap_into_c_void_ptr!`].
macro_rules! drop_wrapped_c_void_ptr {
  ($ptr:expr, $trait_:ident $(< $($args:ident),* >)?) => {{
    if !$ptr.is_null() {
      let config = $ptr as *mut *mut dyn $trait_$(< $($args),* >)?;

      // Drop the Box<*mut $t>, and then the Box<$t>, which also
      // drops the underlying object.
      unsafe {
        let outer = Box::from_raw(config);
        let _inner = Box::from_raw(*outer);
      }
    }
  }};
}

// C ABI function implementations.

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_config_new(
  envoy_config_ptr: abi::envoy_dynamic_module_type_transport_socket_config_envoy_ptr,
  name_ptr: *const std::os::raw::c_char,
  name_size: usize,
  config_ptr: *const std::os::raw::c_char,
  config_size: usize,
  is_upstream: bool,
) -> abi::envoy_dynamic_module_type_transport_socket_config_module_ptr {
  let name = if !name_ptr.is_null() {
    std::str::from_utf8(std::slice::from_raw_parts(name_ptr.cast::<u8>(), name_size))
      .unwrap_or_default()
  } else {
    ""
  };

  let config = if !config_ptr.is_null() {
    std::slice::from_raw_parts(config_ptr.cast::<u8>(), config_size)
  } else {
    &[]
  };

  let mut envoy_socket_config = EnvoyTransportSocketConfigImpl {
    raw_ptr: envoy_config_ptr,
  };

  envoy_dynamic_module_on_transport_socket_config_new_impl(
    &mut envoy_socket_config,
    name,
    config,
    is_upstream,
    *NEW_TRANSPORT_SOCKET_CONFIG_FUNCTION
      .get()
      .expect("NEW_TRANSPORT_SOCKET_CONFIG_FUNCTION must be set"),
  )
}

fn envoy_dynamic_module_on_transport_socket_config_new_impl(
  envoy_socket_config: &mut EnvoyTransportSocketConfigImpl,
  name: &str,
  config: &[u8],
  is_upstream: bool,
  new_config_fn: NewTransportSocketConfigFunction<
    EnvoyTransportSocketConfigImpl,
    EnvoyTransportSocketImpl,
  >,
) -> abi::envoy_dynamic_module_type_transport_socket_config_module_ptr {
  if let Some(socket_config) = new_config_fn(envoy_socket_config, name, config, is_upstream) {
    wrap_into_c_void_ptr!(socket_config)
  } else {
    std::ptr::null()
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_transport_socket_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_ptr, TransportSocketConfig<EnvoyTransportSocketImpl>);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_new(
  config_ptr: abi::envoy_dynamic_module_type_transport_socket_config_module_ptr,
  socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
) -> abi::envoy_dynamic_module_type_transport_socket_module_ptr {
  let mut envoy_socket = EnvoyTransportSocketImpl {
    raw_ptr: socket_envoy_ptr,
  };

  let socket_config = {
    let raw = config_ptr as *mut *mut dyn TransportSocketConfig<EnvoyTransportSocketImpl>;
    &mut **raw
  };

  envoy_dynamic_module_on_transport_socket_new_impl(&mut envoy_socket, socket_config)
}

fn envoy_dynamic_module_on_transport_socket_new_impl(
  envoy_socket: &mut EnvoyTransportSocketImpl,
  socket_config: &mut dyn TransportSocketConfig<EnvoyTransportSocketImpl>,
) -> abi::envoy_dynamic_module_type_transport_socket_module_ptr {
  let socket = socket_config.new_transport_socket(envoy_socket);
  wrap_into_c_void_ptr!(socket)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_destroy(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  drop_wrapped_c_void_ptr!(socket_ptr, TransportSocket<EnvoyTransportSocketImpl>);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_set_callbacks(
  _socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  _socket_module_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  // Callbacks are set.
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_on_connected(
  socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) {
  let mut envoy = EnvoyTransportSocketImpl::new(socket_envoy_ptr);
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };
  socket.on_connected(&mut envoy);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_do_read(
  socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  buffer: *mut std::ffi::c_void,
  buffer_capacity: usize,
) -> abi::envoy_dynamic_module_type_io_result {
  let mut envoy = EnvoyTransportSocketImpl::new(socket_envoy_ptr);
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };

  let buffer_slice = std::slice::from_raw_parts_mut(buffer as *mut u8, buffer_capacity);
  let (bytes_processed, should_close, end_stream_read) = socket.do_read(&mut envoy, buffer_slice);

  abi::envoy_dynamic_module_type_io_result {
    action: if should_close {
      abi::envoy_dynamic_module_type_post_io_action::Close
    } else {
      abi::envoy_dynamic_module_type_post_io_action::KeepOpen
    },
    bytes_processed,
    end_stream_read,
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_do_write(
  socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  buffer: *const std::ffi::c_void,
  buffer_length: usize,
  end_stream: bool,
) -> abi::envoy_dynamic_module_type_io_result {
  let mut envoy = EnvoyTransportSocketImpl::new(socket_envoy_ptr);
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };

  let data = std::slice::from_raw_parts(buffer as *const u8, buffer_length);
  let (bytes_processed, should_close) = socket.do_write(&mut envoy, data, end_stream);

  abi::envoy_dynamic_module_type_io_result {
    action: if should_close {
      abi::envoy_dynamic_module_type_post_io_action::Close
    } else {
      abi::envoy_dynamic_module_type_post_io_action::KeepOpen
    },
    bytes_processed,
    end_stream_read: false,
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_close(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  event: abi::envoy_dynamic_module_type_connection_event,
) {
  // Note: The close function doesn't get socket_envoy_ptr, only the socket_module_ptr.
  // We create a dummy envoy context for the trait call.
  if socket_ptr.is_null() {
    return;
  }

  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };

  // Create dummy envoy context (we don't have socket_envoy_ptr at close time).
  let mut envoy = EnvoyTransportSocketImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  socket.on_close(&mut envoy, event);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_protocol(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  protocol_ptr: *mut *const std::os::raw::c_char,
  protocol_length: *mut usize,
) {
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &**raw
  };

  if let Some(proto) = socket.protocol() {
    *protocol_ptr = proto.as_ptr() as *const std::os::raw::c_char;
    *protocol_length = proto.len();
  } else {
    *protocol_ptr = std::ptr::null();
    *protocol_length = 0;
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_failure_reason(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  reason_ptr: *mut *const std::os::raw::c_char,
  reason_length: *mut usize,
) {
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &**raw
  };

  if let Some(reason) = socket.failure_reason() {
    *reason_ptr = reason.as_ptr() as *const std::os::raw::c_char;
    *reason_length = reason.len();
  } else {
    *reason_ptr = std::ptr::null();
    *reason_length = 0;
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_can_flush_close(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &**raw
  };
  socket.can_flush_close()
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_get_ssl_info(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  info: *mut abi::envoy_dynamic_module_type_ssl_connection_info,
) -> bool {
  if info.is_null() {
    return false;
  }

  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &**raw
  };

  if let Some(ssl_info) = socket.get_ssl_info() {
    let mut info_struct = *info;

    // Note: The SSL info is owned by the socket and persists for the socket's lifetime.
    // This is safe because the socket outlives this function call.
    info_struct.tls_version_ptr = ssl_info.tls_version.as_ptr() as *const std::os::raw::c_char;
    info_struct.tls_version_length = ssl_info.tls_version.len();
    info_struct.cipher_suite_ptr = ssl_info.cipher_suite.as_ptr() as *const std::os::raw::c_char;
    info_struct.cipher_suite_length = ssl_info.cipher_suite.len();
    info_struct.protocol_ptr = ssl_info.alpn_protocol.as_ptr() as *const std::os::raw::c_char;
    info_struct.protocol_length = ssl_info.alpn_protocol.len();
    info_struct.server_name_ptr = ssl_info.sni.as_ptr() as *const std::os::raw::c_char;
    info_struct.server_name_length = ssl_info.sni.len();
    info_struct.peer_certificate_validated = ssl_info.peer_certificate_validated;

    if !ssl_info.peer_certificates.is_empty() {
      info_struct.peer_certificate_ptr =
        ssl_info.peer_certificates[0].as_ptr() as *const std::os::raw::c_char;
      info_struct.peer_certificate_length = ssl_info.peer_certificates[0].len();
    } else {
      info_struct.peer_certificate_ptr = std::ptr::null();
      info_struct.peer_certificate_length = 0;
    }

    *info = info_struct;
    true
  } else {
    false
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_start_secure_transport(
  socket_envoy_ptr: abi::envoy_dynamic_module_type_transport_socket_envoy_ptr,
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
) -> bool {
  let mut envoy = EnvoyTransportSocketImpl::new(socket_envoy_ptr);
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };
  socket.start_secure_transport(&mut envoy)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window(
  socket_ptr: abi::envoy_dynamic_module_type_transport_socket_module_ptr,
  bandwidth_bits_per_sec: u64,
  rtt_microseconds: u64,
) {
  let socket = {
    let raw = socket_ptr as *mut *mut dyn TransportSocket<EnvoyTransportSocketImpl>;
    &mut **raw
  };
  socket.configure_initial_congestion_window(bandwidth_bits_per_sec, rtt_microseconds);
}
