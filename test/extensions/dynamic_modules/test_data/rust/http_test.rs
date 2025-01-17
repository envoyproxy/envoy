use super::*;
use envoy_proxy_dynamic_modules_rust_sdk::EnvoyBuffer;

#[test]
fn test_header_callbacks_filter_on_request_headers() {
  let mut f = HeaderCallbacksFilter {};
  let mut envoy_filter = MockEnvoyHttpFilter::default();

  envoy_filter
    .expect_get_request_header_value()
    .withf(|name| name == "single")
    .returning(|_| Some(EnvoyBuffer::new("value")))
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
      let value1 = EnvoyBuffer::new("value1");
      let value2 = EnvoyBuffer::new("value2");
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
    .returning(|_| Some(EnvoyBuffer::new("value")))
    .once();

  envoy_filter
    .expect_get_request_headers()
    .returning(|| {
      let single = (EnvoyBuffer::new("single"), EnvoyBuffer::new("value"));
      let multi1 = (EnvoyBuffer::new("multi"), EnvoyBuffer::new("value1"));
      let multi2 = (EnvoyBuffer::new("multi"), EnvoyBuffer::new("value2"));
      let new = (EnvoyBuffer::new("new"), EnvoyBuffer::new("value"));
      vec![single, multi1, multi2, new]
    })
    .once();

  assert_eq!(
    f.on_request_headers(envoy_filter, false),
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  );
}

#[test]
fn test_header_callbacks_on_request_headers_local_resp() {
  let mut f = SendResponseFilter {};
  let mut envoy_filter = MockEnvoyHttpFilter::default();

  envoy_filter
    .expect_send_response()
    .withf(|status_code, headers, body| {
      *status_code == 200
        && *headers
          == vec![
            ("header1", "value1".as_bytes()),
            ("header2", "value2".as_bytes()),
          ]
        && *body == Some(b"Hello, World!")
    })
    .once()
    .return_const(());

  assert_eq!(
    f.on_request_headers(envoy_filter, false),
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  );
}
