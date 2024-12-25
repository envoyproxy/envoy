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
    // TODO: add various congigs for body, etc.
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
    assert_eq!(std::str::from_utf8(&single_value).unwrap(), "value");
    let non_exist = envoy_filter.get_request_header_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_header_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(std::str::from_utf8(&multi_value[0]).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&multi_value[1]).unwrap(), "value2");
    let non_exist = envoy_filter.get_request_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_request_header("new", b"value");
    let new_value = envoy_filter
      .get_request_header_value("new")
      .expect("header new not found");
    assert_eq!(std::str::from_utf8(&new_value).unwrap(), "value");

    // Test all getter API.
    let all_headers = envoy_filter.get_request_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(std::str::from_utf8(&all_headers[0].0).unwrap(), "single");
    assert_eq!(std::str::from_utf8(&all_headers[0].1).unwrap(), "value");
    assert_eq!(std::str::from_utf8(&all_headers[1].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_headers[1].1).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&all_headers[2].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_headers[2].1).unwrap(), "value2");
    assert_eq!(std::str::from_utf8(&all_headers[3].0).unwrap(), "new");
    assert_eq!(std::str::from_utf8(&all_headers[3].1).unwrap(), "value");

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
    assert_eq!(std::str::from_utf8(&single_value).unwrap(), "value");
    let non_exist = envoy_filter.get_request_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_request_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(std::str::from_utf8(&multi_value[0]).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&multi_value[1]).unwrap(), "value2");
    let non_exist = envoy_filter.get_request_trailer_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_request_trailer("new", b"value");
    let new_value = envoy_filter
      .get_request_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(std::str::from_utf8(&new_value).unwrap(), "value");

    // Test all getter API.
    let all_trailers = envoy_filter.get_request_trailers();
    assert_eq!(all_trailers.len(), 4);
    assert_eq!(std::str::from_utf8(&all_trailers[0].0).unwrap(), "single");
    assert_eq!(std::str::from_utf8(&all_trailers[0].1).unwrap(), "value");
    assert_eq!(std::str::from_utf8(&all_trailers[1].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_trailers[1].1).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&all_trailers[2].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_trailers[2].1).unwrap(), "value2");
    assert_eq!(std::str::from_utf8(&all_trailers[3].0).unwrap(), "new");
    assert_eq!(std::str::from_utf8(&all_trailers[3].1).unwrap(), "value");

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
    assert_eq!(std::str::from_utf8(&single_value).unwrap(), "value");
    let non_exist = envoy_filter.get_response_header_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_response_header_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(std::str::from_utf8(&multi_value[0]).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&multi_value[1]).unwrap(), "value2");
    let non_exist = envoy_filter.get_response_header_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_response_header("new", b"value");
    let new_value = envoy_filter
      .get_response_header_value("new")
      .expect("header new not found");
    assert_eq!(std::str::from_utf8(&new_value).unwrap(), "value");

    // Test all getter API.
    let all_headers = envoy_filter.get_response_headers();
    assert_eq!(all_headers.len(), 4);
    assert_eq!(std::str::from_utf8(&all_headers[0].0).unwrap(), "single");
    assert_eq!(std::str::from_utf8(&all_headers[0].1).unwrap(), "value");
    assert_eq!(std::str::from_utf8(&all_headers[1].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_headers[1].1).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&all_headers[2].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_headers[2].1).unwrap(), "value2");
    assert_eq!(std::str::from_utf8(&all_headers[3].0).unwrap(), "new");
    assert_eq!(std::str::from_utf8(&all_headers[3].1).unwrap(), "value");

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
    assert_eq!(std::str::from_utf8(&single_value).unwrap(), "value");
    let non_exist = envoy_filter.get_response_trailer_value("non-exist");
    assert!(non_exist.is_none());

    // Test multi getter API.
    let multi_value = envoy_filter.get_response_trailer_values("multi");
    assert_eq!(multi_value.len(), 2);
    assert_eq!(std::str::from_utf8(&multi_value[0]).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&multi_value[1]).unwrap(), "value2");
    let non_exist = envoy_filter.get_response_trailer_values("non-exist");
    assert!(non_exist.is_empty());

    // Test setter API.
    envoy_filter.set_response_trailer("new", b"value");
    let new_value = envoy_filter
      .get_response_trailer_value("new")
      .expect("trailer new not found");
    assert_eq!(std::str::from_utf8(&new_value).unwrap(), "value");

    // Test all getter API.
    let all_trailers = envoy_filter.get_response_trailers();
    assert_eq!(all_trailers.len(), 4);
    assert_eq!(std::str::from_utf8(&all_trailers[0].0).unwrap(), "single");
    assert_eq!(std::str::from_utf8(&all_trailers[0].1).unwrap(), "value");
    assert_eq!(std::str::from_utf8(&all_trailers[1].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_trailers[1].1).unwrap(), "value1");
    assert_eq!(std::str::from_utf8(&all_trailers[2].0).unwrap(), "multi");
    assert_eq!(std::str::from_utf8(&all_trailers[2].1).unwrap(), "value2");
    assert_eq!(std::str::from_utf8(&all_trailers[3].0).unwrap(), "new");
    assert_eq!(std::str::from_utf8(&all_trailers[3].1).unwrap(), "value");

    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}
