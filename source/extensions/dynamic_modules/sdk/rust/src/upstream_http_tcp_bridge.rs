use crate::buffer::EnvoyBuffer;
use crate::{
  abi,
  bytes_to_module_buffer,
  drop_wrapped_c_void_ptr,
  str_to_module_buffer,
  wrap_into_c_void_ptr,
  NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION,
};
use mockall::*;

/// The module-side bridge configuration.
///
/// This trait must be implemented by the module to handle bridge configuration.
/// The object is created when the corresponding Envoy upstream config is loaded, and
/// it is dropped when the corresponding Envoy config is destroyed.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait UpstreamHttpTcpBridgeConfig: Send + Sync {
  /// Create a new per-request bridge instance.
  ///
  /// This is called for each HTTP request routed to a cluster using this bridge.
  fn new_bridge(
    &self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
  ) -> Box<dyn UpstreamHttpTcpBridge>;
}

/// The module-side per-request bridge instance.
///
/// This trait must be implemented by the module to handle HTTP-to-TCP protocol bridging.
/// One instance is created per HTTP request.
pub trait UpstreamHttpTcpBridge: Send {
  /// Called when HTTP request headers are being encoded for the upstream.
  ///
  /// The module can read request headers via `envoy_bridge` and optionally write data
  /// to the request buffer to be sent to the TCP upstream.
  fn on_encode_headers(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status;

  /// Called when HTTP request body data is being encoded for the upstream.
  ///
  /// In streaming mode, this is called per chunk. In buffered mode, this is called once
  /// with the full accumulated body when end_of_stream is reached.
  fn on_encode_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status;

  /// Called when HTTP request trailers are being encoded for the upstream.
  fn on_encode_trailers(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status;

  /// Called when raw TCP data is received from the upstream connection.
  ///
  /// The module should read the TCP data via `envoy_bridge.get_response_buffer()`,
  /// process it, and build the HTTP response using the response header/body/trailer
  /// callbacks on `envoy_bridge`.
  fn on_upstream_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status;
}

/// Envoy-side bridge operations available to the module.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyUpstreamHttpTcpBridge: Send {
  /// Get a request header value by key at the given index.
  ///
  /// Returns the header value and the total count of values for the key.
  fn get_request_header_value<'a>(
    &'a self,
    key: &str,
    index: usize,
  ) -> (Option<EnvoyBuffer<'a>>, usize);

  /// Get the number of request headers.
  fn get_request_headers_size(&self) -> usize;

  /// Get all request headers as key-value pairs.
  fn get_request_headers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Get the current request buffer contents.
  fn get_request_buffer<'a>(&'a self) -> EnvoyBuffer<'a>;

  /// Set or replace the request buffer data to be sent to the TCP upstream.
  fn set_request_buffer(&self, data: &[u8]);

  /// Drain bytes from the beginning of the request buffer.
  fn drain_request_buffer(&self, length: usize);

  /// Append data to the request buffer.
  fn append_request_buffer(&self, data: &[u8]);

  /// Get the raw TCP response data from the upstream.
  fn get_response_buffer<'a>(&'a self) -> EnvoyBuffer<'a>;

  /// Set a response header (replaces existing value).
  fn set_response_header(&self, key: &str, value: &str);

  /// Add a response header (appends to existing values).
  fn add_response_header(&self, key: &str, value: &str);

  /// Set the HTTP response body data (replaces any previous body).
  fn set_response_body(&self, data: &[u8]);

  /// Append data to the HTTP response body.
  fn append_response_body(&self, data: &[u8]);

  /// Set a response trailer (replaces existing value).
  fn set_response_trailer(&self, key: &str, value: &str);

  /// Add a response trailer (appends to existing values).
  fn add_response_trailer(&self, key: &str, value: &str);
}

// Envoy-side implementation

struct EnvoyUpstreamHttpTcpBridgeImpl {
  raw: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
}

unsafe impl Send for EnvoyUpstreamHttpTcpBridgeImpl {}

impl EnvoyUpstreamHttpTcpBridgeImpl {
  fn new(raw: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyUpstreamHttpTcpBridge for EnvoyUpstreamHttpTcpBridgeImpl {
  fn get_request_header_value<'a>(
    &'a self,
    key: &str,
    index: usize,
  ) -> (Option<EnvoyBuffer<'a>>, usize) {
    let key_buf = str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut total_count: usize = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
        self.raw,
        key_buf,
        &mut result,
        index,
        &mut total_count,
      )
    };
    if found && !result.ptr.is_null() {
      (
        Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) }),
        total_count,
      )
    } else {
      (None, total_count)
    }
  }

  fn get_request_headers_size(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(self.raw)
    }
  }

  fn get_request_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let size = self.get_request_headers_size();
    if size == 0 {
      return Vec::new();
    }
    let mut headers: Vec<abi::envoy_dynamic_module_type_envoy_http_header> = vec![
      abi::envoy_dynamic_module_type_envoy_http_header {
        key_ptr: std::ptr::null_mut(),
        key_length: 0,
        value_ptr: std::ptr::null_mut(),
        value_length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(
        self.raw,
        headers.as_mut_ptr(),
      )
    };
    if !ok {
      return Vec::new();
    }
    headers
      .iter()
      .map(|h| unsafe {
        (
          EnvoyBuffer::new_from_raw(h.key_ptr as *const u8, h.key_length),
          EnvoyBuffer::new_from_raw(h.value_ptr as *const u8, h.value_length),
        )
      })
      .collect()
  }

  fn get_request_buffer(&self) -> EnvoyBuffer<'_> {
    let mut ptr: usize = 0;
    let mut length: usize = 0;
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(
        self.raw,
        &mut ptr,
        &mut length,
      );
    }
    if ptr == 0 || length == 0 {
      return EnvoyBuffer::default();
    }
    unsafe { EnvoyBuffer::new_from_raw(ptr as *const u8, length) }
  }

  fn set_request_buffer(&self, data: &[u8]) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_request_buffer(self.raw, buf);
    }
  }

  fn drain_request_buffer(&self, length: usize) {
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_request_buffer(
        self.raw, length,
      );
    }
  }

  fn append_request_buffer(&self, data: &[u8]) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_request_buffer(
        self.raw, buf,
      );
    }
  }

  fn get_response_buffer(&self) -> EnvoyBuffer<'_> {
    let mut ptr: usize = 0;
    let mut length: usize = 0;
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
        self.raw,
        &mut ptr,
        &mut length,
      );
    }
    if ptr == 0 || length == 0 {
      return EnvoyBuffer::default();
    }
    unsafe { EnvoyBuffer::new_from_raw(ptr as *const u8, length) }
  }

  fn set_response_header(&self, key: &str, value: &str) {
    let key_buf = str_to_module_buffer(key);
    let value_buf = str_to_module_buffer(value);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
        self.raw, key_buf, value_buf,
      );
    }
  }

  fn add_response_header(&self, key: &str, value: &str) {
    let key_buf = str_to_module_buffer(key);
    let value_buf = str_to_module_buffer(value);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
        self.raw, key_buf, value_buf,
      );
    }
  }

  fn set_response_body(&self, data: &[u8]) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_body(self.raw, buf);
    }
  }

  fn append_response_body(&self, data: &[u8]) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_response_body(
        self.raw, buf,
      );
    }
  }

  fn set_response_trailer(&self, key: &str, value: &str) {
    let key_buf = str_to_module_buffer(key);
    let value_buf = str_to_module_buffer(value);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_trailer(
        self.raw, key_buf, value_buf,
      );
    }
  }

  fn add_response_trailer(&self, key: &str, value: &str) {
    let key_buf = str_to_module_buffer(key);
    let value_buf = str_to_module_buffer(value);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_trailer(
        self.raw, key_buf, value_buf,
      );
    }
  }
}

// Upstream HTTP TCP Bridge Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr {
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const _,
      name.length,
    ))
  };
  let config_slice = unsafe { std::slice::from_raw_parts(config.ptr as *const _, config.length) };
  let new_config_fn = NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION
    .get()
    .expect("NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION must be set");
  match new_config_fn(name_str, config_slice) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_module_ptr, UpstreamHttpTcpBridgeConfig);
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_new(
  config_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr,
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
) -> abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr {
  let config = config_module_ptr as *const *const dyn UpstreamHttpTcpBridgeConfig;
  let config = &**config;
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  let bridge = config.new_bridge(&envoy_bridge);
  wrap_into_c_void_ptr!(bridge)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_headers(&envoy_bridge, end_of_stream)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_data(&envoy_bridge, end_of_stream)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_trailers(&envoy_bridge)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_upstream_data(&envoy_bridge, end_of_stream)
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
) {
  drop_wrapped_c_void_ptr!(bridge_module_ptr, UpstreamHttpTcpBridge);
}

/// Declare the init functions for the upstream HTTP TCP bridge dynamic module.
///
/// The first argument is the program init function with [`ProgramInitFunction`] type.
/// The second argument is the factory function with [`NewUpstreamHttpTcpBridgeConfigFunction`]
/// type.
#[macro_export]
macro_rules! declare_upstream_http_tcp_bridge_init_functions {
  ($f:ident, $new_bridge_config_fn:expr) => {
    #[no_mangle]
    pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
      envoy_proxy_dynamic_modules_rust_sdk::NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION
        .get_or_init(|| $new_bridge_config_fn);
      if ($f()) {
        envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
          as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
}
