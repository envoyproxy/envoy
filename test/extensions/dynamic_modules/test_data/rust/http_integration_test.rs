use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_init_functions!(init, new_http_filter_config_fn);

fn init() -> bool {
  true
}

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EC, EHF>>> {
  match name {
    "passthrough" => Some(Box::new(PassthroughHttpFilterConfig {})),
    "header_callbacks" => Some(Box::new(HeadersHttpFilterConfig {
      headers_to_add: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "body_callbacks" => Some(Box::new(BodyCallbacksFilterConfig {
      immediate_end_of_stream: config == b"immediate_end_of_stream",
    })),
    "send_response" => Some(Box::new(SendResponseHttpFilterConfig {
      on_request_headers: config == b"on_request_headers",
    })),
    _ => panic!("Unknown filter name: {}", name),
  }
}

struct PassthroughHttpFilterConfig {}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for PassthroughHttpFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(PassthroughHttpFilter {})
  }
}

struct PassthroughHttpFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PassthroughHttpFilter {}

struct HeadersHttpFilterConfig {
  headers_to_add: String,
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for HeadersHttpFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    let headers_to_add: Vec<(String, String)> = self
      .headers_to_add
      .split(',')
      .map(|header| {
        let parts: Vec<&str> = header.split(':').collect();
        assert_eq!(parts.len(), 2);
        (parts[0].to_string(), parts[1].to_string())
      })
      .collect();
    Box::new(HeadersHttpFilter {
      headers_to_add,
      request_headers_called: false,
      request_trailers_called: false,
      response_headers_called: false,
      response_trailers_called: false,
    })
  }
}

struct HeadersHttpFilter {
  headers_to_add: Vec<(String, String)>,
  request_headers_called: bool,
  request_trailers_called: bool,
  response_headers_called: bool,
  response_trailers_called: bool,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for HeadersHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    self.request_headers_called = true;
    let path_header = envoy_filter
      .get_request_header_value(":path")
      .expect(":path header");
    assert_eq!(path_header.as_slice(), b"/test/long/url");
    let method_header = envoy_filter
      .get_request_header_value(":method")
      .expect(":method header");
    assert_eq!(method_header.as_slice(), b"POST");

    let foo_header = envoy_filter
      .get_request_header_value("foo")
      .expect("foo header");
    assert_eq!(foo_header.as_slice(), b"bar");
    for (name, value) in &self.headers_to_add {
      envoy_filter.set_request_header(name, value.as_bytes());
    }
    envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    self.request_trailers_called = true;
    let foo_trailer = envoy_filter
      .get_request_trailer_value("foo")
      .expect("foo trailer");
    assert_eq!(foo_trailer.as_slice(), b"bar");
    for (name, value) in &self.headers_to_add {
      envoy_filter.set_request_trailer(name, value.as_bytes());
    }
    envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_response_headers_status {
    self.response_headers_called = true;
    let foo_header = envoy_filter
      .get_response_header_value("foo")
      .expect("foo header");
    assert_eq!(foo_header.as_slice(), b"bar");
    for (name, value) in &self.headers_to_add {
      envoy_filter.set_response_header(name, value.as_bytes());
    }
    envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    self.response_trailers_called = true;
    let foo_trailer = envoy_filter
      .get_response_trailer_value("foo")
      .expect("foo trailer");
    assert_eq!(foo_trailer.as_slice(), b"bar");
    for (name, value) in &self.headers_to_add {
      envoy_filter.set_response_trailer(name, value.as_bytes());
    }
    envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}

impl Drop for HeadersHttpFilter {
  fn drop(&mut self) {
    assert!(self.request_headers_called);
    assert!(self.request_trailers_called);
    assert!(self.response_headers_called);
    assert!(self.response_trailers_called);
  }
}

struct BodyCallbacksFilterConfig {
  immediate_end_of_stream: bool,
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for BodyCallbacksFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(BodyCallbacksFilter {
      immediate_end_of_stream: self.immediate_end_of_stream,
      seen_request_body: false,
      seen_response_body: false,
    })
  }
}

struct BodyCallbacksFilter {
  /// This is true when we should not see end_of_stream=false, configured by the filter config.
  immediate_end_of_stream: bool,
  seen_request_body: bool,
  seen_response_body: bool,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for BodyCallbacksFilter {
  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_body_status {
    if !end_of_stream {
      assert!(!self.immediate_end_of_stream);
      // Buffer the request body until the end of stream.
      return envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationAndBuffer;
    }
    self.seen_request_body = true;

    let request_body = envoy_filter
      .get_request_body()
      .expect("request body not available");
    let mut body = String::new();
    for chunk in request_body {
      body.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
    }
    assert_eq!(body, "request_body");

    // Drain the request body.
    envoy_filter.drain_request_body(body.len());
    // Append the new request body.
    envoy_filter.append_request_body(b"new_request_body");
    // Plus we need to set the content length.
    envoy_filter.set_request_header("content-length", b"16");

    envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_response_body_status {
    if !end_of_stream {
      // Buffer the response body until the end of stream.
      return envoy_dynamic_module_type_on_http_filter_response_body_status::StopIterationAndBuffer;
    }
    self.seen_response_body = true;

    let response_body = envoy_filter
      .get_response_body()
      .expect("response body not available");
    let mut body = String::new();
    for chunk in response_body {
      body.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
    }
    assert_eq!(body, "response_body");

    // Drain the response body.
    envoy_filter.drain_response_body(body.len());
    // Append the new response body.
    envoy_filter.append_response_body(b"new_response_body");
    // Plus we need to set the content length.
    envoy_filter.set_response_header("content-length", b"17");

    envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }
}

impl Drop for BodyCallbacksFilter {
  fn drop(&mut self) {
    assert!(self.seen_request_body);
    assert!(self.seen_response_body);
  }
}

struct SendResponseHttpFilterConfig {
  on_request_headers: bool,
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for SendResponseHttpFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(SendResponseHttpFilter {
      on_request_headers: self.on_request_headers,
    })
  }
}

struct SendResponseHttpFilter {
  on_request_headers: bool,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for SendResponseHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    if self.on_request_headers {
      envoy_filter.send_response(
        200,
        vec![("some_header", b"some_value")],
        Some(b"local_response_body_from_on_request_headers"),
      );
      envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
    } else {
      envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
    }
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_body_status {
    envoy_filter.send_response(
      200,
      vec![("some_header", b"some_value")],
      Some(b"local_response_body_from_on_request_body"),
    );
    envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationAndBuffer
  }
}
