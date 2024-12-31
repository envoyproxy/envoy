use super::*;
use envoy_proxy_dynamic_modules_rust_sdk::EnvoyBuffer;

#[test]
fn test_header_callbacks_filter_on_request_headers() {
  let mut f = HeaderCallbacksFilter {};
  let mut envoy_filter = MockEnvoyHttpFilter::default();

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

  assert_eq!(
    f.on_request_headers(envoy_filter, false),
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  );
}
