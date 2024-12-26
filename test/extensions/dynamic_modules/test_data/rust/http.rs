use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_init_functions!(init, new_http_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewHttpFilterConfigFunction`]
/// signature.
fn new_http_filter_config_fn(
  _envoy_filter_factory: EnvoyHttpFilterConfig,
  name: &str,
  _config: &str,
) -> Option<Box<dyn HttpFilterConfig>> {
  match name {
    "header_callbacks" => Some(Box::new(HeaderCallbacksFilterConfig {})),
    // TODO: add various configs for body, etc.
    _ => panic!("Unknown filter name: {}", name),
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the header/trailer
/// related callbacks.
struct HeaderCallbacksFilterConfig {}

impl HttpFilterConfig for HeaderCallbacksFilterConfig {
  fn new_http_filter(&self, _envoy: EnvoyHttpFilterConfig) -> Box<dyn HttpFilter> {
    Box::new(HeaderCallbacksFilter {})
  }
}

/// A no-op HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`]
/// as well as the [`Drop`] to test the cleanup of the filter.
struct HeaderCallbacksFilter {}

impl HttpFilter for HeaderCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: EnvoyHttpFilter,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_request_header_value("single")
      .expect("header single not found");
    assert_eq!(&single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_request_header_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_header_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(&multi_value[0].as_slice(), b"value1");
    assert_eq!(&multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_request_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_request_header("new", b"value");
    let new_value = envoy_filter
      .get_request_header_value("new")
      .expect("header new not found");
    assert_eq!(&new_value.as_slice(), b"value");

    // Test all getter API.
    let all_headers = envoy_filter.get_request_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(&all_headers[0].0.as_slice(), b"single");
    assert_eq!(&all_headers[0].1.as_slice(), b"value");
    assert_eq!(&all_headers[1].0.as_slice(), b"multi");
    assert_eq!(&all_headers[1].1.as_slice(), b"value1");
    assert_eq!(&all_headers[2].0.as_slice(), b"multi");
    assert_eq!(&all_headers[2].1.as_slice(), b"value2");
    assert_eq!(&all_headers[3].0.as_slice(), b"new");
    assert_eq!(&all_headers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    _envoy_filter: EnvoyHttpFilter,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_request_trailers(
    &mut self,
    envoy_filter: EnvoyHttpFilter,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_request_trailer_value("single")
      .expect("trailer single not found");
    assert_eq!(&single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_request_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(&multi_value[0].as_slice(), b"value1");
    assert_eq!(&multi_value[1].as_slice(), b"value2");
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
    assert_eq!(&all_trailers[0].0.as_slice(), b"single");
    assert_eq!(&all_trailers[0].1.as_slice(), b"value");
    assert_eq!(&all_trailers[1].0.as_slice(), b"multi");
    assert_eq!(&all_trailers[1].1.as_slice(), b"value1");
    assert_eq!(&all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(&all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(&all_trailers[3].0.as_slice(), b"new");
    assert_eq!(&all_trailers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: EnvoyHttpFilter,
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
    assert_eq!(&multi_value[0].as_slice(), b"value1");
    assert_eq!(&multi_value[1].as_slice(), b"value2");
    let non_exist = envoy_filter.get_response_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_response_header("new", b"value");
    let new_value = envoy_filter
      .get_response_header_value("new")
      .expect("header new not found");
    assert_eq!(std::str::from_utf8(&new_value.as_slice()).unwrap(), "value");

    // Test all getter API.
    let all_headers = envoy_filter.get_response_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(&all_headers[0].0.as_slice(), b"single");
    assert_eq!(&all_headers[0].1.as_slice(), b"value");
    assert_eq!(&all_headers[1].0.as_slice(), b"multi");
    assert_eq!(&all_headers[1].1.as_slice(), b"value1");
    assert_eq!(&all_headers[2].0.as_slice(), b"multi");
    assert_eq!(&all_headers[2].1.as_slice(), b"value2");
    assert_eq!(&all_headers[3].0.as_slice(), b"new");
    assert_eq!(&all_headers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    _envoy_filter: EnvoyHttpFilter,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  fn on_response_trailers(
    &mut self,
    envoy_filter: EnvoyHttpFilter,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    // Test single getter API.
    let single_value = envoy_filter
      .get_response_trailer_value("single")
      .expect("trailer single not found");
    assert_eq!(&single_value.as_slice(), b"value");
    let non_exist = envoy_filter.get_response_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_response_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(&multi_value[0].as_slice(), b"value1");
    assert_eq!(&multi_value[1].as_slice(), b"value2");
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
    assert_eq!(&all_trailers[0].0.as_slice(), b"single",);
    assert_eq!(&all_trailers[0].1.as_slice(), b"value");
    assert_eq!(&all_trailers[1].0.as_slice(), b"multi",);
    assert_eq!(&all_trailers[1].1.as_slice(), b"value1",);
    assert_eq!(&all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(&all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(&all_trailers[3].0.as_slice(), b"new");
    assert_eq!(&all_trailers[3].1.as_slice(), b"value");

    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}

#[cfg(test)]
// This is mostly for demonstrating how to test the filter with using the mock provided by the
// SDK.
mod test {
  use super::*;
  use envoy_proxy_dynamic_modules_rust_sdk::EnvoyBuffer;

  #[test]
  fn test_header_callbacks_filter_on_request_headers() {
    let mut f = HeaderCallbacksFilter {};
    let mut envoy_filter = envoy_proxy_dynamic_modules_rust_sdk::EnvoyHttpFilter::default();

    envoy_filter
      .expect_get_request_header_value()
      .withf(|name| name == "single")
      .returning(|_| Some(EnvoyBuffer::new("value".as_ptr(), "value".len())))
      .once();

    envoy_filter
      .expect_get_request_header_value()
      .withf(|name| name == "non-exist")
      .returning(|_| None)
      .once();

    envoy_filter
      .expect_get_request_header_values()
      .withf(|name| name == "multi")
      .returning(|_| {
        let value1 = EnvoyBuffer::new("value1".as_ptr(), "value1".len());
        let value2 = EnvoyBuffer::new("value2".as_ptr(), "value2".len());
        vec![value1, value2]
      })
      .once();

    envoy_filter
      .expect_get_request_header_values()
      .withf(|name| name == "non-exist")
      .returning(|_| vec![])
      .once();

    envoy_filter
      .expect_set_request_header()
      .withf(|name, value| name == "new" && value == b"value")
      .return_const(true)
      .once();

    envoy_filter
      .expect_get_request_header_value()
      .withf(|name| name == "new")
      .returning(|_| {
        const VALUE: &str = "value";
        Some(EnvoyBuffer::new(VALUE.as_ptr(), VALUE.len()))
      })
      .once();

    envoy_filter
      .expect_get_request_headers()
      .returning(|| {
        let single = (
          EnvoyBuffer::new("single".as_ptr(), "single".len()),
          EnvoyBuffer::new("value".as_ptr(), "value".len()),
        );
        let multi1 = (
          EnvoyBuffer::new("multi".as_ptr(), "multi".len()),
          EnvoyBuffer::new("value1".as_ptr(), "value1".len()),
        );
        let multi2 = (
          EnvoyBuffer::new("multi".as_ptr(), "multi".len()),
          EnvoyBuffer::new("value2".as_ptr(), "value2".len()),
        );
        let new = (
          EnvoyBuffer::new("new".as_ptr(), "new".len()),
          EnvoyBuffer::new("value".as_ptr(), "value".len()),
        );
        vec![single, multi1, multi2, new]
      })
      .once();

    f.on_request_headers(envoy_filter, false);
  }
}
