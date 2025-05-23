use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::any::Any;

declare_init_functions!(
  init,
  new_http_filter_config_fn,
  new_http_filter_per_route_config_fn
);

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
    "per_route_config" => Some(Box::new(PerRouteFilterConfig {
      value: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "body_callbacks" => Some(Box::new(BodyCallbacksFilterConfig {
      immediate_end_of_stream: config == b"immediate_end_of_stream",
    })),
    "http_callouts" => Some(Box::new(HttpCalloutsFilterConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "send_response" => Some(Box::new(SendResponseHttpFilterConfig::new(config))),
    _ => panic!("Unknown filter name: {}", name),
  }
}

fn new_http_filter_per_route_config_fn(name: &str, config: &[u8]) -> Option<Box<dyn Any>> {
  match name {
    "per_route_config" => Some(Box::new(PerRoutePerRouteFilterConfig {
      value: String::from_utf8(config.to_owned()).unwrap(),
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

struct PerRouteFilterConfig {
  value: String,
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for PerRouteFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(PerRouteFilter {
      value: self.value.clone(),
      per_route_config: None,
    })
  }
}
struct PerRouteFilter {
  value: String,
  per_route_config: Option<std::sync::Arc<dyn Any>>,
}

struct PerRoutePerRouteFilterConfig {
  value: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PerRouteFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter.set_request_header("x-config", self.value.as_bytes());
    self.per_route_config = envoy_filter.get_most_specific_route_config();
    if let Some(ref per_route_config) = self.per_route_config {
      let per_route_config = per_route_config
        .downcast_ref::<PerRoutePerRouteFilterConfig>()
        .expect("wrong type for per route config");
      envoy_filter.set_request_header("x-per-route-config", per_route_config.value.as_bytes());
    }

    envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    if let Some(ref per_route_config) = self.per_route_config {
      let per_route_config = per_route_config
        .downcast_ref::<PerRoutePerRouteFilterConfig>()
        .expect("wrong type for per route config");
      envoy_filter.set_response_header(
        "x-per-route-config-response",
        per_route_config.value.as_bytes(),
      );
    }

    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
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
  f: SendResponseHttpFilter,
}

impl SendResponseHttpFilterConfig {
  fn new(config: &[u8]) -> Self {
    let f = match config {
      b"on_request_headers" => SendResponseHttpFilter::OnRequestHeader,
      b"on_request_body" => SendResponseHttpFilter::OnRequestBody,
      b"on_response_headers" => SendResponseHttpFilter::OnResponseHeader,
      _ => panic!("Unknown filter name: {:?}", config),
    };
    Self { f }
  }
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for SendResponseHttpFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(self.f.clone())
  }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum SendResponseHttpFilter {
  OnRequestHeader,
  OnRequestBody,
  OnResponseHeader,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for SendResponseHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    if self == &SendResponseHttpFilter::OnRequestHeader {
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
    if self == &SendResponseHttpFilter::OnRequestBody {
      envoy_filter.send_response(
        200,
        vec![("some_header", b"some_value")],
        Some(b"local_response_body_from_on_request_body"),
      );
      envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationAndBuffer
    } else {
      envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
    }
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    if self == &SendResponseHttpFilter::OnResponseHeader {
      envoy_filter.send_response(
        500,
        vec![("some_header", b"some_value")],
        Some(b"local_response_body_from_on_response_headers"),
      );
      return envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration;
    }
    envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }
}

struct HttpCalloutsFilterConfig {
  cluster_name: String,
}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for HttpCalloutsFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(HttpCalloutsFilter {
      cluster_name: self.cluster_name.clone(),
    })
  }
}

struct HttpCalloutsFilter {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for HttpCalloutsFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    let result = envoy_filter.send_http_callout(
      1234,
      &self.cluster_name,
      vec![
        (":path", b"/"),
        (":method", b"GET"),
        ("host", b"example.com"),
      ],
      Some(b"http_callout_body"),
      1000,
    );
    if result != envoy_dynamic_module_type_http_callout_init_result::Success {
      envoy_filter.send_response(500, vec![("foo", b"bar")], None);
    } else {
      // Try sending the same callout id, which should fail.
      assert_eq!(
        envoy_filter.send_http_callout(
          1234,
          &self.cluster_name,
          vec![
            (":path", b"/"),
            (":method", b"GET"),
            ("host", b"example.com"),
          ],
          None,
          1000,
        ),
        abi::envoy_dynamic_module_type_http_callout_init_result::DuplicateCalloutId
      );
    }
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_callout_done(
    &mut self,
    envoy_filter: &mut EHF,
    callout_id: u32,
    result: abi::envoy_dynamic_module_type_http_callout_result,
    response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    response_body: Option<&[EnvoyBuffer]>,
  ) {
    if self.cluster_name == "resetting_cluster" {
      assert_eq!(result, envoy_dynamic_module_type_http_callout_result::Reset);
      return;
    }
    assert_eq!(
      result,
      envoy_dynamic_module_type_http_callout_result::Success
    );
    assert_eq!(callout_id, 1234);
    assert!(response_headers.is_some());
    assert!(response_body.is_some());
    let response_headers = response_headers.unwrap();
    let response_body = response_body.unwrap();
    let mut found_header = false;
    for (name, value) in response_headers {
      if name.as_slice() == b"some_header" {
        assert_eq!(value.as_slice(), b"some_value");
        found_header = true;
        break;
      }
    }
    assert!(found_header, "Expected header 'some_header' not found");
    let mut body = String::new();
    for chunk in response_body {
      body.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
    }
    assert_eq!(body, "response_body_from_callout");

    envoy_filter.send_response(
      200,
      vec![("some_header", b"some_value")],
      Some(b"local_response_body"),
    );
  }
}
