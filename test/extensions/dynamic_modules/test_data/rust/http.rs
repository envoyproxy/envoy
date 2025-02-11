use envoy_proxy_dynamic_modules_rust_sdk::*;

#[cfg(test)]
#[path = "./http_test.rs"]
mod http_test;

declare_init_functions!(init, new_http_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewHttpFilterConfigFunction`]
/// signature.
fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  _config: &str,
) -> Option<Box<dyn HttpFilterConfig<EC, EHF>>> {
  match name {
    "header_callbacks" => Some(Box::new(HeaderCallbacksFilterConfig {})),
    "send_response" => Some(Box::new(SendResponseFilterConfig {})),
    "dynamic_metadata_callbacks" => Some(Box::new(DynamicMetadataCallbacksFilterConfig {})),
    "body_callbacks" => Some(Box::new(BodyCallbacksFilterConfig {})),
    _ => panic!("Unknown filter name: {}", name),
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the header/trailer
/// related callbacks.
struct HeaderCallbacksFilterConfig {}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for HeaderCallbacksFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(HeaderCallbacksFilter {})
  }
}

/// A HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`].
struct HeaderCallbacksFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for HeaderCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter.clear_route_cache();

    // Test single getter API.
    let single_value = envoy_filter
      .get_request_header_value("single")
      .expect("header single not found");
    assert_eq!(single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_request_header_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_header_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(multi_value[0].as_slice(), b"value1");
    assert_eq!(multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_request_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_request_header("new", b"value");
    let new_value = envoy_filter
      .get_request_header_value("new")
      .expect("header new not found");
    assert_eq!(new_value.as_slice(), b"value");

    // Test all getter API.
    let all_headers = envoy_filter.get_request_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(all_headers[0].0.as_slice(), b"single");
    assert_eq!(all_headers[0].1.as_slice(), b"value");
    assert_eq!(all_headers[1].0.as_slice(), b"multi");
    assert_eq!(all_headers[1].1.as_slice(), b"value1");
    assert_eq!(all_headers[2].0.as_slice(), b"multi");
    assert_eq!(all_headers[2].1.as_slice(), b"value2");
    assert_eq!(all_headers[3].0.as_slice(), b"new");
    assert_eq!(all_headers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_request_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_request_trailer_value("single")
      .expect("trailer single not found");
    assert_eq!(single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_request_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(multi_value[0].as_slice(), b"value1");
    assert_eq!(multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_request_trailer_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_request_trailer("new", b"value");
    let new_value = envoy_filter
      .get_request_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(&new_value.as_slice(), b"value");

    // Test all getter API.
    let all_trailers = envoy_filter.get_request_trailers();
    assert_eq!(all_trailers.len(), 4);
    assert_eq!(all_trailers[0].0.as_slice(), b"single");
    assert_eq!(all_trailers[0].1.as_slice(), b"value");
    assert_eq!(all_trailers[1].0.as_slice(), b"multi");
    assert_eq!(all_trailers[1].1.as_slice(), b"value1");
    assert_eq!(all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(all_trailers[3].0.as_slice(), b"new");
    assert_eq!(all_trailers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_response_header_value("single")
      .expect("header single not found");
    assert_eq!(&single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_response_header_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_response_header_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(multi_value[0].as_slice(), b"value1");
    assert_eq!(multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_response_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_response_header("new", b"value");
    let new_value = envoy_filter
      .get_response_header_value("new")
      .expect("header new not found");
    assert_eq!(&new_value.as_slice(), b"value");

    // Test all getter API.
    let all_headers = envoy_filter.get_response_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(all_headers[0].0.as_slice(), b"single");
    assert_eq!(all_headers[0].1.as_slice(), b"value");
    assert_eq!(all_headers[1].0.as_slice(), b"multi");
    assert_eq!(all_headers[1].1.as_slice(), b"value1");
    assert_eq!(all_headers[2].0.as_slice(), b"multi");
    assert_eq!(all_headers[2].1.as_slice(), b"value2");
    assert_eq!(all_headers[3].0.as_slice(), b"new");
    assert_eq!(all_headers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  fn on_response_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_response_trailer_value("single")
      .expect("trailer single not found");
    assert_eq!(single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_response_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_response_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(multi_value[0].as_slice(), b"value1");
    assert_eq!(multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_response_trailer_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_response_trailer("new", b"value");
    let new_value = envoy_filter
      .get_response_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(&new_value.as_slice(), b"value");

    // Test all getter API.
    let all_trailers = envoy_filter.get_response_trailers();
    assert_eq!(all_trailers.len(), 4);
    assert_eq!(all_trailers[0].0.as_slice(), b"single",);
    assert_eq!(all_trailers[0].1.as_slice(), b"value");
    assert_eq!(all_trailers[1].0.as_slice(), b"multi",);
    assert_eq!(all_trailers[1].1.as_slice(), b"value1",);
    assert_eq!(all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(all_trailers[3].0.as_slice(), b"new");
    assert_eq!(all_trailers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the `send_response()`
/// callback
struct SendResponseFilterConfig {}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for SendResponseFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(SendResponseFilter {})
  }
}

/// A no-op HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`]
/// as well as the [`Drop`] to test the cleanup of the filter.
struct SendResponseFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for SendResponseFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter.send_response(
      200,
      vec![
        ("header1", "value1".as_bytes()),
        ("header2", "value2".as_bytes()),
      ],
      Some(b"Hello, World!"),
    );
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the dynamic metadata related
/// callbacks.
struct DynamicMetadataCallbacksFilterConfig {}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for DynamicMetadataCallbacksFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(DynamicMetadataCallbacksFilter {})
  }
}

/// A HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`].
struct DynamicMetadataCallbacksFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for DynamicMetadataCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // No namespace.
    let no_namespace = envoy_filter.get_dynamic_metadata_number("no_namespace", "key");
    assert!(no_namespace.is_none());
    // Set a number.
    envoy_filter.set_dynamic_metadata_number("ns_req_header", "key", 123f64);
    let ns_req_header = envoy_filter.get_dynamic_metadata_number("ns_req_header", "key");
    assert_eq!(ns_req_header, Some(123f64));
    // Try getting a number as string.
    let ns_req_header = envoy_filter.get_dynamic_metadata_string("ns_req_header", "key");
    assert!(ns_req_header.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    // No namespace.
    let no_namespace = envoy_filter.get_dynamic_metadata_string("no_namespace", "key");
    assert!(no_namespace.is_none());
    // Set a string.
    envoy_filter.set_dynamic_metadata_string("ns_req_body", "key", "value");
    let ns_req_body = envoy_filter.get_dynamic_metadata_string("ns_req_body", "key");
    assert!(ns_req_body.is_some());
    assert_eq!(ns_req_body.unwrap().as_slice(), b"value");
    // Try getting a string as number.
    let ns_req_body = envoy_filter.get_dynamic_metadata_number("ns_req_body", "key");
    assert!(ns_req_body.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    // No namespace.
    let no_namespace = envoy_filter.get_dynamic_metadata_string("no_namespace", "key");
    assert!(no_namespace.is_none());
    // Set a number.
    envoy_filter.set_dynamic_metadata_number("ns_res_header", "key", 123f64);
    let ns_res_header = envoy_filter.get_dynamic_metadata_number("ns_res_header", "key");
    assert_eq!(ns_res_header, Some(123f64));
    // Try getting a number as string.
    let ns_res_header = envoy_filter.get_dynamic_metadata_string("ns_res_header", "key");
    assert!(ns_res_header.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    // No namespace.
    let no_namespace = envoy_filter.get_dynamic_metadata_string("no_namespace", "key");
    assert!(no_namespace.is_none());
    // Set a string.
    envoy_filter.set_dynamic_metadata_string("ns_res_body", "key", "value");
    let ns_res_body = envoy_filter.get_dynamic_metadata_string("ns_res_body", "key");
    assert!(ns_res_body.is_some());
    // Try getting a string as number.
    let ns_res_body = envoy_filter.get_dynamic_metadata_number("ns_res_body", "key");
    assert!(ns_res_body.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`]
/// to test the body related callbacks.
struct BodyCallbacksFilterConfig {}

impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
  for BodyCallbacksFilterConfig
{
  fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
    Box::new(BodyCallbacksFilter::default())
  }
}

/// A HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`].
///
/// This filter tests the body related callbacks.
struct BodyCallbacksFilter {
  request_body: Vec<u8>,
  response_body: Vec<u8>,
}

impl Drop for BodyCallbacksFilter {
  fn drop(&mut self) {
    assert_eq!(
      std::str::from_utf8(&self.request_body).unwrap(),
      "nicenicenice"
    );
    assert_eq!(
      std::str::from_utf8(&self.response_body).unwrap(),
      "coolcoolcool"
    );
  }
}

impl Default for BodyCallbacksFilter {
  fn default() -> Self {
    Self {
      request_body: vec![],
      response_body: vec![],
    }
  }
}

/// This demonstrates a custom reader that reads from the request/response body.
///
/// Imlements the [`std::io::Read`].
struct BodyReader<'a> {
  data: Vec<EnvoyMutBuffer<'a>>,
  vec_idx: usize,
  buf_idx: usize,
}

impl<'a> BodyReader<'a> {
  fn new(data: Vec<EnvoyMutBuffer<'a>>) -> Self {
    Self {
      data,
      vec_idx: 0,
      buf_idx: 0,
    }
  }
}

impl std::io::Read for BodyReader<'_> {
  fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
    if self.vec_idx >= self.data.len() {
      return Ok(0);
    }
    let mut n = 0;
    while n < buf.len() && self.vec_idx < self.data.len() {
      let slice = self.data[self.vec_idx].as_slice();
      let remaining = slice.len() - self.buf_idx;
      let to_copy = std::cmp::min(remaining, buf.len() - n);
      buf[n .. n + to_copy].copy_from_slice(&slice[self.buf_idx .. self.buf_idx + to_copy]);
      n += to_copy;
      self.buf_idx += to_copy;
      if self.buf_idx >= slice.len() {
        self.vec_idx += 1;
        self.buf_idx = 0;
      }
    }
    Ok(n)
  }
}

/// This demonstrates a writer that writes to the request/response body.
///
/// Implements the [`std::io::Write`].
struct BodyWriter<'a, EHF: EnvoyHttpFilter> {
  envoy_filter: &'a mut EHF,
  request: bool,
}

impl<'a, EHF: EnvoyHttpFilter> BodyWriter<'a, EHF> {
  fn new(envoy_filter: &'a mut EHF, request: bool) -> Self {
    // Before starting to write, drain the existing buffer content.
    let current_vec = if request {
      envoy_filter
        .get_request_body()
        .expect("request body is None")
    } else {
      envoy_filter
        .get_response_body()
        .expect("response body is None")
    };


    let buffer_bytes = current_vec
      .iter()
      .map(|buf| buf.as_slice().len())
      .sum::<usize>();

    if request {
      assert!(envoy_filter.drain_request_body(buffer_bytes));
    } else {
      assert!(envoy_filter.drain_response_body(buffer_bytes));
    }
    Self {
      envoy_filter,
      request,
    }
  }
}

impl<'a, EHF: EnvoyHttpFilter> std::io::Write for BodyWriter<'a, EHF> {
  fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
    if self.request {
      if !self.envoy_filter.append_request_body(buf) {
        return Err(std::io::Error::new(
          std::io::ErrorKind::Other,
          "Buffer is not available",
        ));
      }
    } else {
      if !self.envoy_filter.append_response_body(buf) {
        return Err(std::io::Error::new(
          std::io::ErrorKind::Other,
          "Buffer is not available",
        ));
      }
    }

    Ok(buf.len())
  }

  fn flush(&mut self) -> std::io::Result<()> {
    Ok(())
  }
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for BodyCallbacksFilter {
  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    // Test reading request body.
    let body = envoy_filter
      .get_request_body()
      .expect("request body is None");
    let mut reader = BodyReader::new(body);
    let mut buf = vec![0; 1024];
    let n = std::io::Read::read(&mut reader, &mut buf).unwrap();
    self.request_body.extend_from_slice(&buf[.. n]);
    // Drop the reader and try writing to the writer.
    drop(reader);

    // Test writing to request body.
    let mut writer = BodyWriter::new(envoy_filter, true);
    std::io::Write::write(&mut writer, b"foo").unwrap();
    if end_of_stream {
      std::io::Write::write(&mut writer, b"end").unwrap();
    }
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    // Test reading response body.
    let body = envoy_filter
      .get_response_body()
      .expect("response body is None");
    let mut reader = BodyReader::new(body);
    let mut buffer = Vec::new();
    std::io::Read::read_to_end(&mut reader, &mut buffer).unwrap();
    self.response_body.extend_from_slice(&buffer);
    // Drop the reader and try writing to the writer.
    drop(reader);

    // Test writing to response body.
    let mut writer = BodyWriter::new(envoy_filter, false);
    std::io::Write::write(&mut writer, b"bar").unwrap();
    if end_of_stream {
      std::io::Write::write(&mut writer, b"end").unwrap();
    }
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }
}
