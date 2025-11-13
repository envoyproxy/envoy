use super::*;
use envoy_proxy_dynamic_modules_rust_sdk::EnvoyBuffer;

#[test]
fn test_header_callbacks_filter_on_request_headers() {
  let mut f = HeaderCallbacksFilter {};
  let mut envoy_filter = MockEnvoyHttpFilter::default();

  envoy_filter
    .expect_clear_route_cache()
    .return_const(())
    .once();

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
    .expect_remove_request_header()
    .withf(|name| name == "to-be-deleted")
    .return_const(true)
    .once();

  envoy_filter
    .expect_add_request_header()
    .withf(|name, value| name == "multi" && value == b"value3")
    .return_const(true)
    .once();

  envoy_filter
    .expect_get_request_header_values()
    .withf(|name| name == "multi")
    .returning(|_| {
      let value1 = EnvoyBuffer::new("value1");
      let value2 = EnvoyBuffer::new("value2");
      let value3 = EnvoyBuffer::new("value3");
      vec![value1, value2, value3]
    })
    .once();

  envoy_filter
    .expect_get_request_headers()
    .returning(|| {
      let single = (EnvoyBuffer::new("single"), EnvoyBuffer::new("value"));
      let multi1 = (EnvoyBuffer::new("multi"), EnvoyBuffer::new("value1"));
      let multi2 = (EnvoyBuffer::new("multi"), EnvoyBuffer::new("value2"));
      let new = (EnvoyBuffer::new("new"), EnvoyBuffer::new("value"));
      let multi3 = (EnvoyBuffer::new("multi"), EnvoyBuffer::new("value3"));
      vec![single, multi1, multi2, new, multi3]
    })
    .once();

  envoy_filter
    .expect_get_attribute_int()
    .withf(|id| *id == abi::envoy_dynamic_module_type_attribute_id::SourcePort)
    .return_const(1234)
    .once();

  envoy_filter
    .expect_get_attribute_string()
    .withf(|id| *id == abi::envoy_dynamic_module_type_attribute_id::SourceAddress)
    .returning(|_| Some(EnvoyBuffer::new("1.1.1.1:1234")))
    .once();

  assert_eq!(
    f.on_request_headers(&mut envoy_filter, false),
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  );
}

#[test]
fn test_send_response_filter() {
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
    f.on_request_headers(&mut envoy_filter, false),
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  );
}

#[test]
fn test_body_callbacks_filter_on_bodies() {
  let mut f = BodyCallbacksFilter::default();
  let mut envoy_filter = MockEnvoyHttpFilter::default();

  envoy_filter
    .expect_get_received_request_body()
    .returning(|| {
      static mut BUF: [[u8; 4]; 3] = [*b"nice", *b"nice", *b"nice"];
      Some(vec![
        EnvoyMutBuffer::new(unsafe { &mut BUF[0] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF[1] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF[2] }),
      ])
    })
    .times(2);
  envoy_filter
    .expect_get_buffered_request_body()
    .returning(|| {
      static mut BUF: [[u8; 4]; 3] = [*b"nice", *b"nice", *b"nice"];
      Some(vec![
        EnvoyMutBuffer::new(unsafe { &mut BUF[0] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF[1] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF[2] }),
      ])
    })
    .times(2);

  envoy_filter
    .expect_drain_received_request_body()
    .return_const(true)
    .once();
  envoy_filter
    .expect_drain_buffered_request_body()
    .return_const(true)
    .once();

  envoy_filter
    .expect_append_received_request_body()
    .return_const(true)
    .times(2);
  envoy_filter
    .expect_append_buffered_request_body()
    .return_const(true)
    .times(2);

  f.on_request_body(&mut envoy_filter, true);

  assert_eq!(
    std::str::from_utf8(&f.get_final_read_request_body()).unwrap(),
    "nicenicenicenicenicenice"
  );

  envoy_filter
    .expect_get_received_response_body()
    .returning(|| {
      static mut BUF2: [[u8; 4]; 3] = [*b"cool", *b"cool", *b"cool"];
      Some(vec![
        EnvoyMutBuffer::new(unsafe { &mut BUF2[0] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF2[1] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF2[2] }),
      ])
    })
    .times(2);
  envoy_filter
    .expect_get_buffered_response_body()
    .returning(|| {
      static mut BUF2: [[u8; 4]; 3] = [*b"cool", *b"cool", *b"cool"];
      Some(vec![
        EnvoyMutBuffer::new(unsafe { &mut BUF2[0] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF2[1] }),
        EnvoyMutBuffer::new(unsafe { &mut BUF2[2] }),
      ])
    })
    .times(2);

  envoy_filter
    .expect_drain_received_response_body()
    .return_const(true)
    .once();
  envoy_filter
    .expect_drain_buffered_response_body()
    .return_const(true)
    .once();

  envoy_filter
    .expect_append_received_response_body()
    .return_const(true)
    .times(2);
  envoy_filter
    .expect_append_buffered_response_body()
    .return_const(true)
    .times(2);

  f.on_response_body(&mut envoy_filter, true);

  assert_eq!(
    std::str::from_utf8(&f.get_final_read_response_body()).unwrap(),
    "coolcoolcoolcoolcoolcool"
  );
}
