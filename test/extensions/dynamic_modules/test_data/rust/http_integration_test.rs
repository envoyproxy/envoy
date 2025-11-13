use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::any::Any;
use std::vec;

declare_init_functions!(
  init,
  new_http_filter_config_fn,
  new_http_filter_per_route_config_fn
);

fn init() -> bool {
  true
}

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
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
    "http_filter_scheduler" => Some(Box::new(HttpFilterSchedulerConfig {})),
    "fake_external_cache" => Some(Box::new(FakeExternalCachingFilterConfig {})),
    "stats_callbacks" => {
      let config = String::from_utf8(config.to_owned()).unwrap();
      let mut config_iter = config.split(',');
      Some(Box::new(StatsCallbacksFilterConfig {
        requests_total: envoy_filter_config
          .define_counter("requests_total")
          .unwrap(),
        requests_pending: envoy_filter_config
          .define_gauge("requests_pending")
          .unwrap(),
        requests_set_value: envoy_filter_config
          .define_gauge("requests_set_value")
          .unwrap(),
        requests_header_values: envoy_filter_config
          .define_histogram("requests_header_values")
          .unwrap(),
        entrypoint_total: envoy_filter_config
          .define_counter_vec("entrypoint_total", &["entrypoint", "method"])
          .unwrap(),
        entrypoint_set_value: envoy_filter_config
          .define_gauge_vec("entrypoint_set_value", &["entrypoint", "method"])
          .unwrap(),
        entrypoint_pending: envoy_filter_config
          .define_gauge_vec("entrypoint_pending", &["entrypoint", "method"])
          .unwrap(),
        entrypoint_header_values: envoy_filter_config
          .define_histogram_vec("entrypoint_header_values", &["entrypoint", "method"])
          .unwrap(),
        header_to_count: config_iter.next().unwrap().to_owned(),
        header_to_set: config_iter.next().unwrap().to_owned(),
      }))
    },
    "streaming_terminal_filter" => Some(Box::new(StreamingTerminalFilterConfig {})),
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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for PassthroughHttpFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    // Just to test that loggers can be accessible in a filter config callback.
    envoy_log_trace!("new_http_filter called");
    envoy_log_debug!("new_http_filter called");
    envoy_log_info!("new_http_filter called");
    envoy_log_warn!("new_http_filter called");
    envoy_log_error!("new_http_filter called");
    envoy_log_critical!("new_http_filter called");
    Box::new(PassthroughHttpFilter {})
  }
}

struct PassthroughHttpFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PassthroughHttpFilter {
  fn on_request_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Just to test that loggers can be accessible in a filter callback.
    envoy_log_trace!("on_request_headers called");
    envoy_log_debug!("on_request_headers called");
    envoy_log_info!("on_request_headers called");
    envoy_log_warn!("on_request_headers called");
    envoy_log_error!("on_request_headers called");
    envoy_log_critical!("on_request_headers called");
    envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }
}

struct HeadersHttpFilterConfig {
  headers_to_add: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for HeadersHttpFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

    // Test setter and getter API.
    envoy_filter.set_request_header("new", b"value1");
    let new_value = envoy_filter
      .get_request_header_value("new")
      .expect("header new not found");
    assert_eq!(&new_value.as_slice(), b"value1");
    let new_values = envoy_filter.get_request_header_values("new");
    assert_eq!(new_values.len(), 1);
    assert_eq!(new_values[0].as_slice(), b"value1");

    // Test add API.
    envoy_filter.add_request_header("new", b"value2");
    let new_values = envoy_filter.get_request_header_values("new");
    assert_eq!(new_values.len(), 2);
    assert_eq!(new_values[1].as_slice(), b"value2");

    // Test remove API.
    envoy_filter.remove_request_header("new");
    let new_value = envoy_filter.get_request_header_value("new");
    assert!(new_value.is_none());
    let new_values = envoy_filter.get_request_header_values("new");
    assert_eq!(new_values.len(), 0);

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

    // Test setter and getter API.
    envoy_filter.set_request_trailer("new", b"value1");
    let new_value = envoy_filter
      .get_request_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(&new_value.as_slice(), b"value1");
    let new_values = envoy_filter.get_request_trailer_values("new");
    assert_eq!(new_values.len(), 1);
    assert_eq!(new_values[0].as_slice(), b"value1");

    // Test add API.
    envoy_filter.add_request_trailer("new", b"value2");
    let new_values = envoy_filter.get_request_trailer_values("new");
    assert_eq!(new_values.len(), 2);
    assert_eq!(new_values[1].as_slice(), b"value2");

    // Test remove API.
    envoy_filter.remove_request_trailer("new");
    let new_value = envoy_filter.get_request_trailer_value("new");
    assert!(new_value.is_none());
    let new_values = envoy_filter.get_request_trailer_values("new");
    assert_eq!(new_values.len(), 0);

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

    // Test setter and getter API.
    envoy_filter.set_response_header("new", b"value1");
    let new_value = envoy_filter
      .get_response_header_value("new")
      .expect("header new not found");
    assert_eq!(&new_value.as_slice(), b"value1");
    let new_values = envoy_filter.get_response_header_values("new");
    assert_eq!(new_values.len(), 1);
    assert_eq!(new_values[0].as_slice(), b"value1");

    // Test add API.
    envoy_filter.add_response_header("new", b"value2");
    let new_values = envoy_filter.get_response_header_values("new");
    assert_eq!(new_values.len(), 2);
    assert_eq!(new_values[1].as_slice(), b"value2");

    // Test remove API.
    envoy_filter.remove_response_header("new");
    let new_value = envoy_filter.get_response_header_value("new");
    assert!(new_value.is_none());
    let new_values = envoy_filter.get_response_header_values("new");
    assert_eq!(new_values.len(), 0);

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

    // Test setter and getter API.
    envoy_filter.set_response_trailer("new", b"value1");
    let new_value = envoy_filter
      .get_response_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(&new_value.as_slice(), b"value1");
    let new_values = envoy_filter.get_response_trailer_values("new");
    assert_eq!(new_values.len(), 1);
    assert_eq!(new_values[0].as_slice(), b"value1");

    // Test add API.
    envoy_filter.add_response_trailer("new", b"value2");
    let new_values = envoy_filter.get_response_trailer_values("new");
    assert_eq!(new_values.len(), 2);
    assert_eq!(new_values[1].as_slice(), b"value2");

    // Test remove API.
    envoy_filter.remove_response_trailer("new");
    let new_value = envoy_filter.get_response_trailer_value("new");
    assert!(new_value.is_none());
    let new_values = envoy_filter.get_response_trailer_values("new");
    assert_eq!(new_values.len(), 0);

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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for PerRouteFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for BodyCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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
  fn on_request_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

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

    let mut received_body_len: usize = 0;
    let mut buffered_body_len: usize = 0;
    let mut body_content = String::new();

    let buffered_body = envoy_filter.get_buffered_request_body();
    if buffered_body.is_some() {
      for chunk in buffered_body.unwrap() {
        buffered_body_len += chunk.as_slice().len();
        body_content.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
      }
      let buffered_body_len_directly = envoy_filter
        .get_buffered_request_body_size()
        .expect("buffered body size");
      assert_eq!(buffered_body_len, buffered_body_len_directly);
    }

    let received_body = envoy_filter.get_received_request_body();
    if received_body.is_some() {
      for chunk in received_body.unwrap() {
        received_body_len += chunk.as_slice().len();
        body_content.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
      }
      let received_body_len_directly = envoy_filter
        .get_received_request_body_size()
        .expect("received body size");
      assert_eq!(received_body_len, received_body_len_directly);
    }

    assert_eq!(body_content, "request_body");

    // Drain the request body.
    envoy_filter.drain_received_request_body(received_body_len);
    envoy_filter.drain_buffered_request_body(buffered_body_len);

    // Append the new request body.
    envoy_filter.append_received_request_body(b"new_request_body");
    // Plus we need to set the content length.
    envoy_filter.set_request_header("content-length", b"16");

    envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration
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

    let mut buffered_body_len: usize = 0;
    let mut received_body_len: usize = 0;
    let mut body_content = String::new();

    let buffered_body = envoy_filter.get_buffered_response_body();
    if buffered_body.is_some() {
      for chunk in buffered_body.unwrap() {
        buffered_body_len += chunk.as_slice().len();
        body_content.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
      }
      let buffered_body_len_directly = envoy_filter
        .get_buffered_response_body_size()
        .expect("buffered body size");
      assert_eq!(buffered_body_len, buffered_body_len_directly);
    }

    let received_body = envoy_filter.get_received_response_body();
    if received_body.is_some() {
      for chunk in received_body.unwrap() {
        received_body_len += chunk.as_slice().len();
        body_content.push_str(std::str::from_utf8(chunk.as_slice()).unwrap());
      }
      let received_body_len_directly = envoy_filter
        .get_received_response_body_size()
        .expect("received body size");
      assert_eq!(received_body_len, received_body_len_directly);
    }

    assert_eq!(body_content, "response_body");

    // Drain the response body.
    envoy_filter.drain_received_response_body(received_body_len);
    envoy_filter.drain_buffered_response_body(buffered_body_len);
    // Append the new response body.
    envoy_filter.append_received_response_body(b"new_response_body");
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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for SendResponseHttpFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for HttpCalloutsFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

struct HttpFilterSchedulerConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for HttpFilterSchedulerConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(HttpFilterScheduler {
      event_ids: vec![],
      thread_handles: vec![],
    })
  }
}

/// This spawns a thread for each request and response header callback and stops iteration at these
/// event hooks.
struct HttpFilterScheduler {
  event_ids: Vec<u64>,
  thread_handles: Vec<std::thread::JoinHandle<()>>,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for HttpFilterScheduler {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    let scheduler = envoy_filter.new_scheduler();
    let thread = std::thread::spawn(move || {
      scheduler.commit(0);
      scheduler.commit(1);
    });
    self.thread_handles.push(thread);
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_response_headers_status {
    let scheduler = envoy_filter.new_scheduler();
    let thread = std::thread::spawn(move || {
      scheduler.commit(2);
      scheduler.commit(3);
    });
    self.thread_handles.push(thread);
    envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration
  }

  fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
    self.event_ids.push(event_id);
    if event_id == 1 {
      envoy_filter.continue_decoding()
    } else if event_id == 3 {
      envoy_filter.continue_encoding()
    }
  }
}

impl Drop for HttpFilterScheduler {
  fn drop(&mut self) {
    assert_eq!(self.event_ids, vec![0, 1, 2, 3]);
    assert_eq!(self.thread_handles.len(), 2);
    for thread in self.thread_handles.drain(..) {
      thread.join().expect("Failed to join thread");
    }
  }
}

/// This implements a fake external caching filter that simulates an asynchronous cache lookup.
struct FakeExternalCachingFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for FakeExternalCachingFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(FakeExternalCachingFilter { rx: None })
  }
}

struct FakeExternalCachingFilter {
  rx: Option<std::sync::mpsc::Receiver<String>>,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for FakeExternalCachingFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Get the cache key from the request header, which is owned by the Envoy filter.
    let cache_key_header_value = envoy_filter.get_request_header_value("cacahe-key").unwrap();
    // Construct the cache key String from the Envoy buffer so that it can be sent to the different
    // thread.
    let mut cache_key = String::new();
    cache_key.push_str(std::str::from_utf8(cache_key_header_value.as_slice()).unwrap());
    // We need to send the found cached response body back to the worker thread,
    // so we use a channel to communicate between the filter and the worker thread safely.
    //
    // Alternatively, you can use Arc<Mutex<>> or similar constructs.
    let (cx, rx) = std::sync::mpsc::channel();
    self.rx = Some(rx);
    // In real world scenarios, rather than spawning a thread per request,
    // you would typically use a thread pool or an async runtime to handle
    // the asynchronous I/O or computation.
    let scheduler = envoy_filter.new_scheduler();
    _ = std::thread::spawn(move || {
      // Simulate some processing to check if the cache key exists.
      let cache_hit = if cache_key == "existing" {
        // Do some processing to get the cached response body in real world.
        let cached_body = "cached_response_body".to_string();
        // If the cache key exists, we send it back to the Envoy filter.
        cx.send(cached_body).unwrap();
        1
      } else {
        0
      };
      // We use the event_id pased to the commit method to indicate if the cache key was found.
      scheduler.commit(cache_hit);
    });
    // Return StopIteration to indicate that we will continue the processing
    // once the scheduled event is completed.
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
    match event_id {
      // Event from the on_request_headers when the cache key was not found.
      0 => {
        // Ensure that it is possible to set response headers on the generic scheduled event.
        envoy_filter.set_request_header("on-scheduled", b"req");
        envoy_filter.continue_decoding();
      },
      // Event from the on_scheduled when the cache key was found.
      1 => {
        let result = self.rx.take().unwrap().recv().unwrap();
        envoy_filter.send_response(200, vec![("cached", b"yes")], Some(result.as_bytes()));
      },
      // Event from the on_response_headers.
      2 => {
        // Ensure that it is possible to set response headers on the generic scheduled event.
        envoy_filter.set_response_header("on-scheduled", b"res");
        envoy_filter.continue_encoding();
      },
      _ => unreachable!(),
    }
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    let scheduler = envoy_filter.new_scheduler();
    _ = std::thread::spawn(move || {
      scheduler.commit(2);
    });

    // Return StopIteration to indicate that we will continue the processing
    // once the scheduled event is completed.
    envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration
  }
}

struct StatsCallbacksFilterConfig {
  requests_total: EnvoyCounterId,
  requests_pending: EnvoyGaugeId,
  requests_header_values: EnvoyHistogramId,
  requests_set_value: EnvoyGaugeId,
  entrypoint_total: EnvoyCounterVecId,
  entrypoint_pending: EnvoyGaugeVecId,
  entrypoint_header_values: EnvoyHistogramVecId,
  entrypoint_set_value: EnvoyGaugeVecId,
  header_to_count: String,
  header_to_set: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for StatsCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(StatsCallbacksFilter {
      requests_total: self.requests_total,
      requests_pending: self.requests_pending,
      requests_header_values: self.requests_header_values,
      requests_set_value: self.requests_set_value,
      entrypoint_total: self.entrypoint_total,
      entrypoint_pending: self.entrypoint_pending,
      entrypoint_header_values: self.entrypoint_header_values,
      entrypoint_set_value: self.entrypoint_set_value,
      header_to_count: self.header_to_count.clone(),
      header_to_set: self.header_to_set.clone(),
      method: None,
    })
  }
}

struct StatsCallbacksFilter {
  requests_total: EnvoyCounterId,
  requests_pending: EnvoyGaugeId,
  requests_set_value: EnvoyGaugeId,
  requests_header_values: EnvoyHistogramId,

  entrypoint_total: EnvoyCounterVecId,
  entrypoint_pending: EnvoyGaugeVecId,
  entrypoint_set_value: EnvoyGaugeVecId,
  entrypoint_header_values: EnvoyHistogramVecId,
  header_to_count: String,
  header_to_set: String,
  method: Option<String>,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for StatsCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter
      .increment_counter(self.requests_total, 1)
      .unwrap();
    envoy_filter
      .increase_gauge(self.requests_pending, 1)
      .unwrap();
    let method = envoy_filter.get_request_header_value(":method").unwrap();
    let method = std::str::from_utf8(method.as_slice()).unwrap();
    envoy_filter
      .increment_counter_vec(self.entrypoint_total, &["on_request_headers", method], 1)
      .unwrap();
    envoy_filter
      .increase_gauge_vec(self.entrypoint_pending, &["on_request_headers", method], 1)
      .unwrap();
    self.method = Some(method.to_owned());

    // Record histogram value to provided value in header
    if let Some(header_val) = envoy_filter.get_request_header_value(self.header_to_count.as_str()) {
      let header_val = std::str::from_utf8(header_val.as_slice())
        .unwrap()
        .parse()
        .unwrap();
      envoy_filter
        .record_histogram_value(self.requests_header_values, header_val)
        .unwrap();
      envoy_filter
        .record_histogram_value_vec(
          self.entrypoint_header_values,
          &["on_request_headers", method],
          header_val,
        )
        .unwrap();
    }

    // Set gauges to provided value in header
    if let Some(header_val) = envoy_filter.get_request_header_value(self.header_to_set.as_str()) {
      let header_val = std::str::from_utf8(header_val.as_slice())
        .unwrap()
        .parse()
        .unwrap();
      envoy_filter
        .set_gauge(self.requests_set_value, header_val)
        .unwrap();
      envoy_filter
        .set_gauge_vec(
          self.entrypoint_set_value,
          &["on_request_headers", method],
          header_val,
        )
        .unwrap();
    }

    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    envoy_filter
      .increment_counter_vec(
        self.entrypoint_total,
        &["on_response_headers", self.method.as_ref().unwrap()],
        1,
      )
      .unwrap();
    envoy_filter
      .decrease_gauge(self.requests_pending, 1)
      .unwrap();
    envoy_filter
      .decrease_gauge_vec(
        self.entrypoint_pending,
        &["on_request_headers", self.method.as_ref().unwrap()],
        1,
      )
      .unwrap();
    envoy_filter
      .increase_gauge_vec(
        self.entrypoint_pending,
        &["on_response_headers", self.method.as_ref().unwrap()],
        1,
      )
      .unwrap();

    // Record histogram value to provided value in header
    if let Some(header_val) = envoy_filter.get_response_header_value(self.header_to_count.as_str())
    {
      let header_val = std::str::from_utf8(header_val.as_slice())
        .unwrap()
        .parse()
        .unwrap();
      envoy_filter
        .record_histogram_value_vec(
          self.entrypoint_header_values,
          &["on_response_headers", self.method.as_ref().unwrap()],
          header_val,
        )
        .unwrap();
    }

    // Set gauges to provided value in header
    if let Some(header_val) = envoy_filter.get_response_header_value(self.header_to_set.as_str()) {
      let header_val = std::str::from_utf8(header_val.as_slice())
        .unwrap()
        .parse()
        .unwrap();
      envoy_filter
        .set_gauge_vec(
          self.entrypoint_set_value,
          &["on_response_headers", self.method.as_ref().unwrap()],
          header_val,
        )
        .unwrap();
    }

    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_stream_complete(&mut self, envoy_filter: &mut EHF) {
    envoy_filter
      .decrease_gauge_vec(
        self.entrypoint_pending,
        &["on_response_headers", self.method.as_ref().unwrap()],
        1,
      )
      .unwrap();
  }
}

// Terminal filter that creates a response without an upstream.
// This filter demonstrates a bidirectional stream with trailers - response processing
// can happen in filter callbacks or scheduled events. We test scheduled events here
// since it will be more common for terminal filters.
//
// The terminal filter test configures the connection buffer to 1024 bytes. We test
// watermark events by writing 8 1024 byte chunks, once immediately and the remaining
// in response to low watermark events.
//
// Request flow:
//   - Client sends headers
//   - Filter returns headers and body
//   - Client sends body
//   - Filter returns large body chunks triggering watermark events
//   - Client closes request
//   - Filter returns body and trailers
struct StreamingTerminalFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for StreamingTerminalFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(StreamingTerminalHttpFilter {
      request_closed: false,
      above_watermark_count: 0,
      below_watermark_count: 0,
      large_response_bytes_sent: 0,
    })
  }
}

const EVENT_ID_START_RESPONSE: u64 = 1;
const EVENT_ID_READ_REQUEST: u64 = 2;

struct StreamingTerminalHttpFilter {
  request_closed: bool,
  above_watermark_count: usize,
  below_watermark_count: usize,
  large_response_bytes_sent: usize,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for StreamingTerminalHttpFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter.new_scheduler().commit(EVENT_ID_START_RESPONSE);
    return envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue;
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_body_status {
    if end_of_stream {
      self.request_closed = true;
    }
    envoy_filter.new_scheduler().commit(EVENT_ID_READ_REQUEST);
    return envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationAndBuffer;
  }

  fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
    match event_id {
      EVENT_ID_START_RESPONSE => {
        envoy_filter.send_response_headers(
          vec![
            (":status", b"200"),
            ("x-filter", b"terminal"),
            ("trailers", b"x-status"),
          ],
          false,
        );
        envoy_filter.send_response_data(b"Who are you?", false);
      },
      EVENT_ID_READ_REQUEST => {
        if !self.request_closed {
          let mut body = Vec::new();
          // The event is scheduled asynchronously and this will be called out of
          // on_request_body. So, we get the buffered body here.
          if let Some(buffers) = envoy_filter.get_buffered_request_body() {
            for buffer in buffers {
              body.extend_from_slice(buffer.as_slice());
            }
          }
          envoy_filter.drain_buffered_request_body(body.len());
          self.send_large_response_chunk(envoy_filter);
        } else {
          envoy_filter.send_response_data(b"Thanks!", false);
          envoy_filter.send_response_trailers(vec![
            ("x-status", b"finished"),
            (
              "x-above-watermark-count",
              self.above_watermark_count.to_string().as_bytes(),
            ),
            (
              "x-below-watermark-count",
              self.below_watermark_count.to_string().as_bytes(),
            ),
          ]);
        }
      },
      _ => unreachable!(),
    }
  }

  fn on_downstream_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut EHF) {
    self.above_watermark_count += 1;
  }

  fn on_downstream_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut EHF) {
    self.below_watermark_count += 1;
    if self.above_watermark_count == self.below_watermark_count {
      // Watermark levels are balanced, we can send more data.
      self.send_large_response_chunk(_envoy_filter);
    }
  }
}

impl StreamingTerminalHttpFilter {
  fn send_large_response_chunk<EHF: EnvoyHttpFilter>(&mut self, envoy_filter: &mut EHF) {
    if self.large_response_bytes_sent >= 8 * 1024 {
      return;
    }
    let chunk_size = 1024;
    let chunk = vec![b'a'; chunk_size];
    envoy_filter.send_response_data(&chunk, false);
    self.large_response_bytes_sent += chunk_size;
  }
}
