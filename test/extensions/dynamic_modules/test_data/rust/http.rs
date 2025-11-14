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
  envoy_filter_config: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  match name {
    "stats_callbacks" => Some(Box::new(StatsCallbacksFilterConfig {
      streams_total: envoy_filter_config
        .define_counter("streams_total")
        .expect("failed to define counter"),
      concurrent_streams: envoy_filter_config
        .define_gauge("concurrent_streams")
        .expect("failed to define gauge"),
      ones: envoy_filter_config
        .define_histogram("ones")
        .expect("failed to define histogram"),
      magic_number: envoy_filter_config
        .define_gauge("magic_number")
        .expect("failed to define gauge"),
      test_counter_vec: envoy_filter_config
        .define_counter_vec("test_counter_vec", &["test_label"])
        .expect("failed to define counter vec"),
      test_gauge_vec: envoy_filter_config
        .define_gauge_vec("test_gauge_vec", &["test_label"])
        .expect("failed to define gauge vec"),
      test_histogram_vec: envoy_filter_config
        .define_histogram_vec("test_histogram_vec", &["test_label"])
        .expect("failed to define histogram vec"),
    })),
    "header_callbacks" => Some(Box::new(HeaderCallbacksFilterConfig {})),
    "send_response" => Some(Box::new(SendResponseFilterConfig {})),
    "dynamic_metadata_callbacks" => Some(Box::new(DynamicMetadataCallbacksFilterConfig {})),
    "filter_state_callbacks" => Some(Box::new(FilterStateCallbacksFilterConfig {})),
    "body_callbacks" => Some(Box::new(BodyCallbacksFilterConfig {})),
    "config_init_failure" => None,
    _ => panic!("Unknown filter name: {}", name),
  }
}

/// An HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the stats
/// related callbacks.
struct StatsCallbacksFilterConfig {
  streams_total: EnvoyCounterId,
  concurrent_streams: EnvoyGaugeId,
  magic_number: EnvoyGaugeId,
  // It's full of 1s.
  ones: EnvoyHistogramId,
  test_counter_vec: EnvoyCounterVecId,
  test_gauge_vec: EnvoyGaugeVecId,
  test_histogram_vec: EnvoyHistogramVecId,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for StatsCallbacksFilterConfig {
  fn new_http_filter(&mut self, envoy_filter: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    envoy_filter
      .increment_counter(self.streams_total, 1)
      .expect("failed to increment counter");
    envoy_filter
      .increase_gauge(self.concurrent_streams, 1)
      .expect("failed to increase gauge");
    envoy_filter
      .set_gauge(self.magic_number, 42)
      .expect("failed to set gauge");
    envoy_filter
      .increment_counter_vec(self.test_counter_vec, &["increment"], 1)
      .expect("failed to increment counter vec");
    envoy_filter
      .increase_gauge_vec(self.test_gauge_vec, &["increase"], 1)
      .expect("failed to increase gauge vec");
    envoy_filter
      .increase_gauge_vec(self.test_gauge_vec, &["decrease"], 10)
      .expect("failed to increase gauge vec");
    envoy_filter
      .decrease_gauge_vec(self.test_gauge_vec, &["decrease"], 8)
      .expect("failed to decrease gauge vec");
    envoy_filter
      .set_gauge_vec(self.test_gauge_vec, &["set"], 9001)
      .expect("failed to set gauge vec");
    envoy_filter
      .record_histogram_value_vec(self.test_histogram_vec, &["record"], 1)
      .expect("failed to record histogram value vec");
    // Copy the stats handles onto the filter so that we can observe stats while
    // handling requests.
    Box::new(StatsCallbacksFilter {
      concurrent_streams: self.concurrent_streams,
      ones: self.ones,
      test_counter_vec: self.test_counter_vec,
      test_gauge_vec: self.test_gauge_vec,
      test_histogram_vec: self.test_histogram_vec,
    })
  }
}

/// An HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`].
struct StatsCallbacksFilter {
  concurrent_streams: EnvoyGaugeId,
  ones: EnvoyHistogramId,
  test_counter_vec: EnvoyCounterVecId,
  test_gauge_vec: EnvoyGaugeVecId,
  test_histogram_vec: EnvoyHistogramVecId,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for StatsCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter
      .record_histogram_value(self.ones, 1)
      .expect("failed to record histogram value");

    let header = envoy_filter.get_request_header_value("header").unwrap();
    let header = std::str::from_utf8(header.as_slice()).unwrap();
    envoy_filter
      .increment_counter_vec(self.test_counter_vec, &[header], 1)
      .expect("failed to increment counter vec");
    envoy_filter
      .increase_gauge_vec(self.test_gauge_vec, &[header], 1)
      .expect("failed to increase gauge vec");
    envoy_filter
      .record_histogram_value_vec(self.test_histogram_vec, &[header], 1)
      .expect("failed to record histogram value vec");

    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_stream_complete(&mut self, envoy_filter: &mut EHF) {
    envoy_filter
      .decrease_gauge(self.concurrent_streams, 1)
      .expect("failed to decrease gauge");

    let local_var = "local_var".to_owned();
    envoy_filter
      .increment_counter_vec(self.test_counter_vec, &[&local_var], 1)
      .expect("failed to increment counter vec");
    envoy_filter
      .increase_gauge_vec(self.test_gauge_vec, &[&local_var], 1)
      .expect("failed to increase gauge vec");
    envoy_filter
      .record_histogram_value_vec(self.test_histogram_vec, &[&local_var], 1)
      .expect("failed to record histogram value vec");
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the header/trailer
/// related callbacks.
struct HeaderCallbacksFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for HeaderCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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
    envoy_filter.remove_request_header("to-be-deleted");

    // Test add API.
    envoy_filter.add_request_header("multi", b"value3");
    let multi_value = envoy_filter.get_request_header_values("multi");
    assert_eq!(multi_value.len(), 3);
    assert_eq!(multi_value[2].as_slice(), b"value3");

    // Test all getter API.
    let all_headers = envoy_filter.get_request_headers();
    assert_eq!(all_headers.len(), 5);
    assert_eq!(all_headers[0].0.as_slice(), b"single");
    assert_eq!(all_headers[0].1.as_slice(), b"value");
    assert_eq!(all_headers[1].0.as_slice(), b"multi");
    assert_eq!(all_headers[1].1.as_slice(), b"value1");
    assert_eq!(all_headers[2].0.as_slice(), b"multi");
    assert_eq!(all_headers[2].1.as_slice(), b"value2");
    assert_eq!(all_headers[3].0.as_slice(), b"new");
    assert_eq!(all_headers[3].1.as_slice(), b"value");
    assert_eq!(all_headers[4].0.as_slice(), b"multi");
    assert_eq!(all_headers[4].1.as_slice(), b"value3");


    let downstream_port =
      envoy_filter.get_attribute_int(abi::envoy_dynamic_module_type_attribute_id::SourcePort);
    assert_eq!(downstream_port, Some(1234));
    let downstream_addr =
      envoy_filter.get_attribute_string(abi::envoy_dynamic_module_type_attribute_id::SourceAddress);
    assert!(downstream_addr.is_some());
    assert_eq!(
      std::str::from_utf8(&downstream_addr.unwrap().as_slice()).unwrap(),
      "1.1.1.1:1234"
    );

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
    envoy_filter.remove_request_trailer("to-be-deleted");

    // Test add API.
    envoy_filter.add_request_trailer("multi", b"value3");
    let multi_value = envoy_filter.get_request_trailer_values("multi");
    assert_eq!(multi_value.len(), 3);
    assert_eq!(multi_value[2].as_slice(), b"value3");

    // Test all getter API.
    let all_trailers = envoy_filter.get_request_trailers();
    assert_eq!(all_trailers.len(), 5);
    assert_eq!(all_trailers[0].0.as_slice(), b"single");
    assert_eq!(all_trailers[0].1.as_slice(), b"value");
    assert_eq!(all_trailers[1].0.as_slice(), b"multi");
    assert_eq!(all_trailers[1].1.as_slice(), b"value1");
    assert_eq!(all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(all_trailers[3].0.as_slice(), b"new");
    assert_eq!(all_trailers[3].1.as_slice(), b"value");
    assert_eq!(all_trailers[4].0.as_slice(), b"multi");
    assert_eq!(all_trailers[4].1.as_slice(), b"value3");


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
    envoy_filter.remove_response_header("to-be-deleted");

    // Test add API.
    envoy_filter.add_response_header("multi", b"value3");
    let multi_value = envoy_filter.get_response_header_values("multi");
    assert_eq!(multi_value.len(), 3);
    assert_eq!(multi_value[2].as_slice(), b"value3");

    // Test all getter API.
    let all_headers = envoy_filter.get_response_headers();
    assert_eq!(all_headers.len(), 5);
    assert_eq!(all_headers[0].0.as_slice(), b"single");
    assert_eq!(all_headers[0].1.as_slice(), b"value");
    assert_eq!(all_headers[1].0.as_slice(), b"multi");
    assert_eq!(all_headers[1].1.as_slice(), b"value1");
    assert_eq!(all_headers[2].0.as_slice(), b"multi");
    assert_eq!(all_headers[2].1.as_slice(), b"value2");
    assert_eq!(all_headers[3].0.as_slice(), b"new");
    assert_eq!(all_headers[3].1.as_slice(), b"value");
    assert_eq!(all_headers[4].0.as_slice(), b"multi");
    assert_eq!(all_headers[4].1.as_slice(), b"value3");

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
    envoy_filter.remove_response_trailer("to-be-deleted");

    // Test add API.
    envoy_filter.add_response_trailer("multi", b"value3");
    let multi_value = envoy_filter.get_response_trailer_values("multi");
    assert_eq!(multi_value.len(), 3);
    assert_eq!(multi_value[2].as_slice(), b"value3");

    // Test all getter API.
    let all_trailers = envoy_filter.get_response_trailers();
    assert_eq!(all_trailers.len(), 5);
    assert_eq!(all_trailers[0].0.as_slice(), b"single",);
    assert_eq!(all_trailers[0].1.as_slice(), b"value");
    assert_eq!(all_trailers[1].0.as_slice(), b"multi",);
    assert_eq!(all_trailers[1].1.as_slice(), b"value1",);
    assert_eq!(all_trailers[2].0.as_slice(), b"multi");
    assert_eq!(all_trailers[2].1.as_slice(), b"value2");
    assert_eq!(all_trailers[3].0.as_slice(), b"new");
    assert_eq!(all_trailers[3].1.as_slice(), b"value");
    assert_eq!(all_trailers[4].0.as_slice(), b"multi");
    assert_eq!(all_trailers[4].1.as_slice(), b"value3");


    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the `send_response()`
/// callback
struct SendResponseFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for SendResponseFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for DynamicMetadataCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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
    let no_namespace = envoy_filter.get_metadata_number(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "no_namespace",
      "key",
    );
    assert!(no_namespace.is_none());
    // Set a number.
    envoy_filter.set_dynamic_metadata_number("ns_req_header", "key", 123f64);
    let ns_req_header = envoy_filter.get_metadata_number(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_req_header",
      "key",
    );
    assert_eq!(ns_req_header, Some(123f64));
    // Try getting a number as string.
    let ns_req_header = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_req_header",
      "key",
    );
    assert!(ns_req_header.is_none());

    // Try getting metadata from rotuer cluster and host.
    let metadata = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Route,
      "metadata",
      "route_key",
    );
    assert_eq!(metadata.unwrap().as_slice(), b"route");
    let metadata = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Cluster,
      "metadata",
      "cluster_key",
    );
    assert_eq!(metadata.unwrap().as_slice(), b"cluster");
    let metadata = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Host,
      "metadata",
      "host_key",
    );
    assert_eq!(metadata.unwrap().as_slice(), b"host");

    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    // No namespace.
    let no_namespace = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "no_namespace",
      "key",
    );
    assert!(no_namespace.is_none());
    // Set a string.
    envoy_filter.set_dynamic_metadata_string("ns_req_body", "key", "value");
    let ns_req_body = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_req_body",
      "key",
    );
    assert!(ns_req_body.is_some());
    assert_eq!(ns_req_body.unwrap().as_slice(), b"value");
    // Try getting a string as number.
    let ns_req_body = envoy_filter.get_metadata_number(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_req_body",
      "key",
    );
    assert!(ns_req_body.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    // No namespace.
    let no_namespace = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "no_namespace",
      "key",
    );
    assert!(no_namespace.is_none());
    // Set a number.
    envoy_filter.set_dynamic_metadata_number("ns_res_header", "key", 123f64);
    let ns_res_header = envoy_filter.get_metadata_number(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_res_header",
      "key",
    );
    assert_eq!(ns_res_header, Some(123f64));
    // Try getting a number as string.
    let ns_res_header = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_res_header",
      "key",
    );
    assert!(ns_res_header.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    // No namespace.
    let no_namespace = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "no_namespace",
      "key",
    );
    assert!(no_namespace.is_none());
    // Set a string.
    envoy_filter.set_dynamic_metadata_string("ns_res_body", "key", "value");
    let ns_res_body = envoy_filter.get_metadata_string(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_res_body",
      "key",
    );
    assert!(ns_res_body.is_some());
    // Try getting a string as number.
    let ns_res_body = envoy_filter.get_metadata_number(
      abi::envoy_dynamic_module_type_metadata_source::Dynamic,
      "ns_res_body",
      "key",
    );
    assert!(ns_res_body.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] to test the filter state related
/// callbacks.
struct FilterStateCallbacksFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for FilterStateCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(FilterStateCallbacksFilter {})
  }
}

/// A HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`].
struct FilterStateCallbacksFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for FilterStateCallbacksFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    envoy_filter.set_filter_state_bytes(b"req_header_key", b"req_header_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"req_header_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"req_header_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    envoy_filter.set_filter_state_bytes(b"req_body_key", b"req_body_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"req_body_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"req_body_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_request_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    envoy_filter.set_filter_state_bytes(b"req_trailer_key", b"req_trailer_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"req_trailer_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"req_trailer_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    envoy_filter.set_filter_state_bytes(b"res_header_key", b"res_header_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"res_header_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"res_header_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    envoy_filter.set_filter_state_bytes(b"res_body_key", b"res_body_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"res_body_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"res_body_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  fn on_response_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    envoy_filter.set_filter_state_bytes(b"res_trailer_key", b"res_trailer_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"res_trailer_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"res_trailer_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }

  fn on_stream_complete(&mut self, envoy_filter: &mut EHF) {
    envoy_filter.set_filter_state_bytes(b"stream_complete_key", b"stream_complete_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"stream_complete_key");
    assert!(filter_state.is_some());
    assert_eq!(filter_state.unwrap().as_slice(), b"stream_complete_value");
    let filter_state = envoy_filter.get_filter_state_bytes(b"key");
    assert!(filter_state.is_none());
  }
}

/// A HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`]
/// to test the body related callbacks.
struct BodyCallbacksFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for BodyCallbacksFilterConfig {
  fn new_http_filter(&mut self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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

#[cfg(test)]
impl BodyCallbacksFilter {
  fn get_final_read_request_body<'a>(&'a self) -> &'a Vec<u8> {
    &self.request_body
  }
  fn get_final_read_response_body<'a>(&'a self) -> &'a Vec<u8> {
    &self.response_body
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
  received: bool, // true: new received body, false: old buffered body
}

impl<'a, EHF: EnvoyHttpFilter> BodyWriter<'a, EHF> {
  fn new(envoy_filter: &'a mut EHF, request: bool, received: bool) -> Self {
    // Before starting to write, drain the existing buffer content.
    if received {
      let optional_vec = if request {
        envoy_filter.get_received_request_body()
      } else {
        envoy_filter.get_received_response_body()
      };

      if optional_vec.is_some() {
        let received_vec = optional_vec.unwrap();

        let buffer_bytes = received_vec
          .iter()
          .map(|buf| buf.as_slice().len())
          .sum::<usize>();

        if request {
          assert!(envoy_filter.drain_received_request_body(buffer_bytes));
        } else {
          assert!(envoy_filter.drain_received_response_body(buffer_bytes));
        }
      }
    } else {
      let optional_vec = if request {
        envoy_filter.get_buffered_request_body()
      } else {
        envoy_filter.get_buffered_response_body()
      };

      if optional_vec.is_some() {
        let buffered_vec = optional_vec.unwrap();

        let buffer_bytes = buffered_vec
          .iter()
          .map(|buf| buf.as_slice().len())
          .sum::<usize>();

        if request {
          assert!(envoy_filter.drain_buffered_request_body(buffer_bytes));
        } else {
          assert!(envoy_filter.drain_buffered_response_body(buffer_bytes));
        }
      }
    }

    Self {
      envoy_filter,
      request,
      received,
    }
  }
}

impl<'a, EHF: EnvoyHttpFilter> std::io::Write for BodyWriter<'a, EHF> {
  fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
    if self.request {
      if self.received {
        if !self.envoy_filter.append_received_request_body(buf) {
          return Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Buffer is not available",
          ));
        }
      } else {
        if !self.envoy_filter.append_buffered_request_body(buf) {
          return Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Buffer is not available",
          ));
        }
      }
    } else {
      if self.received {
        if !self.envoy_filter.append_received_response_body(buf) {
          return Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Buffer is not available",
          ));
        }
      } else {
        if !self.envoy_filter.append_buffered_response_body(buf) {
          return Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Buffer is not available",
          ));
        }
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
    {
      // Test reading new received request body.
      let body = envoy_filter.get_received_request_body();
      if body.is_some() {
        let mut reader = BodyReader::new(body.unwrap());
        let mut buf = vec![0; 1024];
        let n = std::io::Read::read(&mut reader, &mut buf).unwrap();
        self.request_body.extend_from_slice(&buf[.. n]);
        // Drop the reader and try writing to the writer.
        drop(reader);

        // Test writing to request body.
        let mut writer = BodyWriter::new(envoy_filter, true, true);
        std::io::Write::write(&mut writer, b"foo").unwrap();
        if end_of_stream {
          std::io::Write::write(&mut writer, b"end").unwrap();
        }
      }
    }
    {
      // Test reading old buffered request body.
      let body = envoy_filter.get_buffered_request_body();
      if body.is_some() {
        let mut reader = BodyReader::new(body.unwrap());
        let mut buf = vec![0; 1024];
        let n = std::io::Read::read(&mut reader, &mut buf).unwrap();
        self.request_body.extend_from_slice(&buf[.. n]);
        // Drop the reader and try writing to the writer.
        drop(reader);

        // Test writing to request body.
        let mut writer = BodyWriter::new(envoy_filter, true, false);
        std::io::Write::write(&mut writer, b"foo").unwrap();
        if end_of_stream {
          std::io::Write::write(&mut writer, b"end").unwrap();
        }
      }
    }

    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    {
      // Test reading new received response body.
      let body = envoy_filter.get_received_response_body();
      if body.is_some() {
        let mut reader = BodyReader::new(body.unwrap());
        let mut buffer = Vec::new();
        std::io::Read::read_to_end(&mut reader, &mut buffer).unwrap();
        self.response_body.extend_from_slice(&buffer);
        // Drop the reader and try writing to the writer.
        drop(reader);

        // Test writing to response body.
        let mut writer = BodyWriter::new(envoy_filter, false, true);
        std::io::Write::write(&mut writer, b"bar").unwrap();
        if end_of_stream {
          std::io::Write::write(&mut writer, b"end").unwrap();
        }
      }
    }
    {
      // Test reading old buffered response body.
      let body = envoy_filter.get_buffered_response_body();
      if body.is_some() {
        let mut reader = BodyReader::new(body.unwrap());
        let mut buffer = Vec::new();
        std::io::Read::read_to_end(&mut reader, &mut buffer).unwrap();
        self.response_body.extend_from_slice(&buffer);
        // Drop the reader and try writing to the writer.
        drop(reader);

        // Test writing to response body.
        let mut writer = BodyWriter::new(envoy_filter, false, false);
        std::io::Write::write(&mut writer, b"bar").unwrap();
        if end_of_stream {
          std::io::Write::write(&mut writer, b"end").unwrap();
        }
      }
    }

    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }
}
