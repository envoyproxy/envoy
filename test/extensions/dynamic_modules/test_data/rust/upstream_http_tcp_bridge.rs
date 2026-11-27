use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_upstream_http_tcp_bridge_init_functions!(program_init, new_bridge_config);

fn program_init() -> bool {
  true
}

fn new_bridge_config(_name: &str, config: &[u8]) -> Option<Box<dyn UpstreamHttpTcpBridgeConfig>> {
  let config_str = std::str::from_utf8(config).unwrap_or("streaming");
  let mode = match config_str {
    "local_reply" => BridgeMode::LocalReply,
    "request_response" => BridgeMode::RequestResponse,
    "request_response_reuse" => BridgeMode::RequestResponseReuse,
    "fire_and_forget" => BridgeMode::FireAndForget,
    _ => BridgeMode::Streaming,
  };
  Some(Box::new(TestBridgeConfig { mode }))
}

// The size of the request the fire-and-forget mode sends, far more than a socket takes in one
// write on a new connection.
const FIRE_AND_FORGET_REQUEST_BYTES: usize = 8 * 1024 * 1024;

#[derive(Clone, Copy)]
enum BridgeMode {
  Streaming,
  LocalReply,
  // Like streaming, but ends the response on the first upstream reply while the upstream
  // connection stays open, as a request and response protocol does. Its protocol frames each
  // reply as one chunk, which the tests' fake upstream writes at once, so a module whose
  // replies can span reads must parse its own framing instead.
  RequestResponse,
  // Like request and response, and also lets the upstream connection serve the next request.
  RequestResponseReuse,
  // Sends a large request, ends its upstream data and replies at once without waiting for the
  // upstream to read it, as a fire-and-forget protocol does.
  FireAndForget,
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
      response_headers_sent: false,
    })
  }
}

struct TestBridge {
  mode: BridgeMode,
  response_headers_sent: bool,
}

impl UpstreamHttpTcpBridge for TestBridge {
  fn on_encode_headers(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    _end_of_stream: bool,
  ) {
    match self.mode {
      BridgeMode::Streaming | BridgeMode::RequestResponse | BridgeMode::RequestResponseReuse => {
        if let (Some(method), _) = envoy_bridge.get_request_header_value(":method", 0) {
          let method_str = std::str::from_utf8(&method).unwrap_or("?");
          let prefix = format!("METHOD={method_str} ");
          envoy_bridge.send_upstream_data(prefix.as_bytes(), false);
        }
      },
      BridgeMode::LocalReply => {
        envoy_bridge.send_response(403, &[], b"access denied");
      },
      BridgeMode::FireAndForget => {
        envoy_bridge.send_upstream_data(&vec![b'x'; FIRE_AND_FORGET_REQUEST_BYTES], true);
        envoy_bridge.send_response(200, &[], b"sent");
      },
    }
  }

  fn on_encode_data(&mut self, envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge, end_of_stream: bool) {
    let data = envoy_bridge.get_request_buffer();
    envoy_bridge.send_upstream_data(&data, end_of_stream);
  }

  fn on_encode_trailers(&mut self, envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge) {
    envoy_bridge.send_upstream_data(&[], true);
  }

  fn on_upstream_data(
    &mut self,
    envoy_bridge: &dyn EnvoyUpstreamHttpTcpBridge,
    end_of_stream: bool,
  ) {
    if !self.response_headers_sent {
      envoy_bridge.send_response_headers(200, &[("x-bridge-mode", b"dynamic_module")], false);
      self.response_headers_sent = true;
    }

    let tcp_data = envoy_bridge.get_response_buffer();
    let end_of_response = end_of_stream
      || matches!(
        self.mode,
        BridgeMode::RequestResponse | BridgeMode::RequestResponseReuse
      );
    if end_of_response && matches!(self.mode, BridgeMode::RequestResponseReuse) {
      envoy_bridge.enable_upstream_connection_reuse();
    }
    envoy_bridge.send_response_data(&tcp_data, end_of_response);
  }
}
