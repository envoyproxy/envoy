use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_upstream_http_tcp_bridge_init_functions!(program_init, new_bridge_config);

fn program_init() -> bool {
  true
}

fn new_bridge_config(_name: &str, config: &[u8]) -> Option<Box<dyn UpstreamHttpTcpBridgeConfig>> {
  let config_str = std::str::from_utf8(config).unwrap_or("streaming");
  let mode = match config_str {
    "buffered" => BridgeMode::Buffered,
    "local_reply" => BridgeMode::LocalReply,
    _ => BridgeMode::Streaming,
  };
  Some(Box::new(TestBridgeConfig { mode }))
}

#[derive(Clone, Copy)]
enum BridgeMode {
  Streaming,
  Buffered,
  LocalReply,
}

struct TestBridgeConfig {
  mode: BridgeMode,
}

impl UpstreamHttpTcpBridgeConfig for TestBridgeConfig {
  fn new_bridge(
    &self,
    _envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
  ) -> Box<dyn UpstreamHttpTcpBridge> {
    Box::new(TestBridge {
      mode: self.mode,
      response_headers_set: false,
    })
  }
}

struct TestBridge {
  mode: BridgeMode,
  response_headers_set: bool,
}

impl UpstreamHttpTcpBridge for TestBridge {
  fn on_encode_headers(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status {
    match self.mode {
      BridgeMode::Streaming => {
        if let (Some(method), _) = envoy_bridge.get_request_header_value(":method", 0) {
          let method_str = std::str::from_utf8(method.as_slice()).unwrap_or("?");
          let prefix = format!("METHOD={} ", method_str);
          envoy_bridge.append_request_buffer(prefix.as_bytes());
        }
        abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status::Continue
      },
      BridgeMode::Buffered => {
        use abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status as S;
        S::StopAndBuffer
      },
      BridgeMode::LocalReply => {
        envoy_bridge.set_response_header(":status", "403");
        envoy_bridge.set_response_body(b"access denied");
        abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status::EndStream
      },
    }
  }

  fn on_encode_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status {
    match self.mode {
      BridgeMode::Streaming => {
        abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status::Continue
      },
      BridgeMode::Buffered => {
        let data = envoy_bridge.get_request_buffer();
        let header = format!("LEN={} ", data.as_slice().len());
        let mut new_data = header.into_bytes();
        new_data.extend_from_slice(data.as_slice());
        envoy_bridge.set_request_buffer(&new_data);
        abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status::Continue
      },
      BridgeMode::LocalReply => {
        abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status::EndStream
      },
    }
  }

  fn on_encode_trailers(
    &mut self,
    _envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status {
    abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status::Continue
  }

  fn on_upstream_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status {
    if !self.response_headers_set {
      envoy_bridge.set_response_header(":status", "200");
      envoy_bridge.add_response_header("x-bridge-mode", "dynamic_module");
      self.response_headers_set = true;
    }

    let tcp_data = envoy_bridge.get_response_buffer();
    envoy_bridge.append_response_body(tcp_data.as_slice());

    if end_of_stream {
      abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status::EndStream
    } else {
      abi::envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status::Continue
    }
  }
}
