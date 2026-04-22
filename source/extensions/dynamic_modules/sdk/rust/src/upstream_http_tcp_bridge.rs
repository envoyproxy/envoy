use crate::{
  abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, str_to_module_buffer, wrap_into_c_void_ptr,
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
/// One instance is created per HTTP request. The module controls the data flow by calling
/// explicit callbacks on `envoy_bridge` (send_upstream_data, send_response, etc.) rather
/// than returning status codes.
pub trait UpstreamHttpTcpBridge: Send {
  /// Called when HTTP request headers are being encoded for the upstream.
  ///
  /// The module can read request headers via `envoy_bridge` and use `send_upstream_data`
  /// or `send_response` to act on the request.
  fn on_encode_headers(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  );

  /// Called when HTTP request body data is being encoded for the upstream.
  ///
  /// The module can read the current request body data via `envoy_bridge.get_request_buffer()`
  /// and use `send_upstream_data` to forward data to the TCP upstream.
  fn on_encode_data(&mut self, envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge, end_of_stream: bool);

  /// Called when HTTP request trailers are being encoded for the upstream.
  fn on_encode_trailers(&mut self, envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge);

  /// Called when raw TCP data is received from the upstream connection.
  ///
  /// The module should read the TCP data via `envoy_bridge.get_response_buffer()`,
  /// process it, and send the HTTP response using `send_response_headers`,
  /// `send_response_data`, and `send_response_trailers` on `envoy_bridge`.
  fn on_upstream_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  );
}

/// Envoy-side bridge operations available to the module.
#[automock]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyUpstreamHttpTcpBridge: Send {
  /// Get a request header value by key at the given index.
  ///
  /// Returns the header value and the total count of values for the key.
  fn get_request_header_value(&self, key: &str, index: usize) -> (Option<Vec<u8>>, usize);

  /// Get the number of request headers.
  fn get_request_headers_size(&self) -> usize;

  /// Get all request headers as key-value pairs.
  fn get_request_headers(&self) -> Vec<(Vec<u8>, Vec<u8>)>;

  /// Get the current request buffer contents as a contiguous byte vector.
  fn get_request_buffer(&self) -> Vec<u8>;

  /// Get the raw TCP response data from the upstream as a contiguous byte vector.
  fn get_response_buffer(&self) -> Vec<u8>;

  /// Send transformed data to the TCP upstream connection.
  fn send_upstream_data(&self, data: &[u8], end_stream: bool);

  /// Send a complete local response to the downstream client, ending the stream.
  fn send_response<'a>(&self, status_code: u32, headers: &'a [(&'a str, &'a [u8])], body: &[u8]);

  /// Send response headers to the downstream client, optionally ending the stream.
  fn send_response_headers<'a>(
    &self,
    status_code: u32,
    headers: &'a [(&'a str, &'a [u8])],
    end_stream: bool,
  );

  /// Send response body data to the downstream client, optionally ending the stream.
  fn send_response_data(&self, data: &[u8], end_stream: bool);

  /// Send response trailers to the downstream client, ending the stream.
  fn send_response_trailers<'a>(&self, trailers: &'a [(&'a str, &'a [u8])]);
}

const MAX_BUFFER_SLICES: usize = 64;

struct EnvoyUpstreamHttpTcpBridgeImpl {
  raw: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
}

unsafe impl Send for EnvoyUpstreamHttpTcpBridgeImpl {}

impl EnvoyUpstreamHttpTcpBridgeImpl {
  fn new(raw: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr) -> Self {
    Self { raw }
  }

  fn read_buffer_slices(
    &self,
    getter: unsafe extern "C" fn(
      abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
      *mut abi::envoy_dynamic_module_type_envoy_buffer,
      *mut usize,
    ),
  ) -> Vec<u8> {
    let mut buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null_mut(),
        length: 0,
      };
      MAX_BUFFER_SLICES
    ];
    let mut num_slices: usize = 0;
    unsafe {
      getter(self.raw, buffers.as_mut_ptr(), &mut num_slices);
    }
    if num_slices == 0 {
      return Vec::new();
    }
    let mut result = Vec::new();
    for buf in buffers.iter().take(num_slices) {
      if !buf.ptr.is_null() && buf.length > 0 {
        let slice = unsafe { std::slice::from_raw_parts(buf.ptr as *const u8, buf.length) };
        result.extend_from_slice(slice);
      }
    }
    result
  }

  fn build_module_headers(
    headers: &[(&str, &[u8])],
  ) -> Vec<abi::envoy_dynamic_module_type_module_http_header> {
    headers
      .iter()
      .map(|(k, v)| abi::envoy_dynamic_module_type_module_http_header {
        key_ptr: k.as_ptr() as *const _,
        key_length: k.len(),
        value_ptr: v.as_ptr() as *const _,
        value_length: v.len(),
      })
      .collect()
  }
}

impl EnvoyUpstreamHttpTcpBridge for EnvoyUpstreamHttpTcpBridgeImpl {
  fn get_request_header_value(&self, key: &str, index: usize) -> (Option<Vec<u8>>, usize) {
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
      let slice = unsafe { std::slice::from_raw_parts(result.ptr as *const u8, result.length) };
      (Some(slice.to_vec()), total_count)
    } else {
      (None, total_count)
    }
  }

  fn get_request_headers_size(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(self.raw)
    }
  }

  fn get_request_headers(&self) -> Vec<(Vec<u8>, Vec<u8>)> {
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
          std::slice::from_raw_parts(h.key_ptr as *const u8, h.key_length).to_vec(),
          std::slice::from_raw_parts(h.value_ptr as *const u8, h.value_length).to_vec(),
        )
      })
      .collect()
  }

  fn get_request_buffer(&self) -> Vec<u8> {
    self.read_buffer_slices(
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer,
    )
  }

  fn get_response_buffer(&self) -> Vec<u8> {
    self.read_buffer_slices(
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer,
    )
  }

  fn send_upstream_data(&self, data: &[u8], end_stream: bool) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(
        self.raw, buf, end_stream,
      );
    }
  }

  fn send_response(&self, status_code: u32, headers: &[(&str, &[u8])], body: &[u8]) {
    let mut header_vec = Self::build_module_headers(headers);
    let body_buf = bytes_to_module_buffer(body);
    let headers_ptr = if header_vec.is_empty() {
      std::ptr::null_mut()
    } else {
      header_vec.as_mut_ptr()
    };
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
        self.raw,
        status_code,
        headers_ptr,
        header_vec.len(),
        body_buf,
      );
    }
  }

  fn send_response_headers(&self, status_code: u32, headers: &[(&str, &[u8])], end_stream: bool) {
    let mut header_vec = Self::build_module_headers(headers);
    let headers_ptr = if header_vec.is_empty() {
      std::ptr::null_mut()
    } else {
      header_vec.as_mut_ptr()
    };
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(
        self.raw,
        status_code,
        headers_ptr,
        header_vec.len(),
        end_stream,
      );
    }
  }

  fn send_response_data(&self, data: &[u8], end_stream: bool) {
    let buf = bytes_to_module_buffer(data);
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(
        self.raw, buf, end_stream,
      );
    }
  }

  fn send_response_trailers(&self, trailers: &[(&str, &[u8])]) {
    let mut trailer_vec = Self::build_module_headers(trailers);
    let trailers_ptr = if trailer_vec.is_empty() {
      std::ptr::null_mut()
    } else {
      trailer_vec.as_mut_ptr()
    };
    unsafe {
      abi::envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(
        self.raw,
        trailers_ptr,
        trailer_vec.len(),
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
) {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_headers(&envoy_bridge, end_of_stream);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
  end_of_stream: bool,
) {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_data(&envoy_bridge, end_of_stream);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
) {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_encode_trailers(&envoy_bridge);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
  bridge_envoy_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
  bridge_module_ptr: abi::envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr,
  end_of_stream: bool,
) {
  let bridge = bridge_module_ptr as *mut Box<dyn UpstreamHttpTcpBridge>;
  let bridge = unsafe { &mut *bridge };
  let envoy_bridge = EnvoyUpstreamHttpTcpBridgeImpl::new(bridge_envoy_ptr);
  bridge.on_upstream_data(&envoy_bridge, end_of_stream);
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
      envoy_proxy_dynamic_modules_rust_sdk::set_factory_once!(
        envoy_proxy_dynamic_modules_rust_sdk::NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION,
        $new_bridge_config_fn,
        "NEW_UPSTREAM_HTTP_TCP_BRIDGE_CONFIG_FUNCTION"
      );
      if ($f()) {
        envoy_proxy_dynamic_modules_rust_sdk::abi::envoy_dynamic_modules_abi_version.as_ptr()
          as *const ::std::os::raw::c_char
      } else {
        ::std::ptr::null()
      }
    }
  };
}
