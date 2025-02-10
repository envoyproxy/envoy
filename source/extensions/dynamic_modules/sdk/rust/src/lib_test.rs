use crate::*;
#[cfg(test)]
use std::sync::atomic::AtomicBool; // This is used for testing the drop, not for the actual concurrency.

#[test]
fn test_envoy_dynamic_module_on_http_filter_config_new_impl() {
  struct TestHttpFilterConfig;
  impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
    for TestHttpFilterConfig
  {
  }

  let mut envoy_filter_config = EnvoyHttpFilterConfigImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut new_fn: NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl> =
    |_, _, _| Some(Box::new(TestHttpFilterConfig));
  let result = envoy_dynamic_module_on_http_filter_config_new_impl(
    &mut envoy_filter_config,
    "test_name",
    "test_config",
    &new_fn,
  );
  assert!(!result.is_null());

  unsafe {
    envoy_dynamic_module_on_http_filter_config_destroy(result);
  }

  // None should result in null pointer.
  new_fn = |_, _, _| None;
  let result = envoy_dynamic_module_on_http_filter_config_new_impl(
    &mut envoy_filter_config,
    "test_name",
    "test_config",
    &new_fn,
  );
  assert!(result.is_null());
}

#[test]
fn test_envoy_dynamic_module_on_http_filter_config_destroy() {
  // This test is mainly to check if the drop is called correctly after wrapping/unwrapping the
  // Box.
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestHttpFilterConfig;
  impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
    for TestHttpFilterConfig
  {
  }
  impl Drop for TestHttpFilterConfig {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  // This is a sort of round-trip to ensure the same control flow as the actual usage.
  let new_fn: NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl> =
    |_, _, _| Some(Box::new(TestHttpFilterConfig));
  let config_ptr = envoy_dynamic_module_on_http_filter_config_new_impl(
    &mut EnvoyHttpFilterConfigImpl {
      raw_ptr: std::ptr::null_mut(),
    },
    "test_name",
    "test_config",
    &new_fn,
  );

  unsafe {
    envoy_dynamic_module_on_http_filter_config_destroy(config_ptr);
  }
  // Now that the drop is called, DROPPED must be set to true.
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
// This tests the round-trip of the envoy_dynamic_module_on_http_filter_new_impl and
// envoy_dynamic_module_on_http_filter_destroy.
fn test_envoy_dynamic_module_on_http_filter_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestHttpFilterConfig;
  impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
    for TestHttpFilterConfig
  {
    fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
      Box::new(TestHttpFilter)
    }
  }

  struct TestHttpFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for TestHttpFilter {}
  impl Drop for TestHttpFilter {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestHttpFilterConfig;
  let result = envoy_dynamic_module_on_http_filter_new_impl(
    &mut EnvoyHttpFilterConfigImpl {
      raw_ptr: std::ptr::null_mut(),
    },
    &mut filter_config,
  );
  assert!(!result.is_null());

  unsafe {
    envoy_dynamic_module_on_http_filter_destroy(result);
  }

  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
// This tests all the on_* methods on the HttpFilter trait through the actual entry points.
fn test_envoy_dynamic_module_on_http_filter_callbacks() {
  struct TestHttpFilterConfig;
  impl<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter> HttpFilterConfig<EC, EHF>
    for TestHttpFilterConfig
  {
    fn new_http_filter(&mut self, _envoy: &mut EC) -> Box<dyn HttpFilter<EHF>> {
      Box::new(TestHttpFilter)
    }
  }

  static ON_REQUEST_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_REQUEST_BODY_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_REQUEST_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_BODY_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestHttpFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for TestHttpFilter {
    fn on_request_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
      ON_REQUEST_HEADERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
    }

    fn on_request_body(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
      ON_REQUEST_BODY_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
    }

    fn on_request_trailers(
      &mut self,
      _envoy_filter: &mut EHF,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
      ON_REQUEST_TRAILERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
    }

    fn on_response_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
      ON_RESPONSE_HEADERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
    }

    fn on_response_body(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
      ON_RESPONSE_BODY_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
    }

    fn on_response_trailers(
      &mut self,
      _envoy_filter: &mut EHF,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
      ON_RESPONSE_TRAILERS_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
    }
  }

  let mut filter_config = TestHttpFilterConfig;
  let filter = envoy_dynamic_module_on_http_filter_new_impl(
    &mut EnvoyHttpFilterConfigImpl {
      raw_ptr: std::ptr::null_mut(),
    },
    &mut filter_config,
  );

  unsafe {
    assert_eq!(
      envoy_dynamic_module_on_http_filter_request_headers(std::ptr::null_mut(), filter, false),
      abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
    );
    assert_eq!(
      envoy_dynamic_module_on_http_filter_request_body(std::ptr::null_mut(), filter, false),
      abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
    );
    assert_eq!(
      envoy_dynamic_module_on_http_filter_request_trailers(std::ptr::null_mut(), filter),
      abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
    );
    assert_eq!(
      envoy_dynamic_module_on_http_filter_response_headers(std::ptr::null_mut(), filter, false),
      abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
    );
    assert_eq!(
      envoy_dynamic_module_on_http_filter_response_body(std::ptr::null_mut(), filter, false),
      abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
    );
    assert_eq!(
      envoy_dynamic_module_on_http_filter_response_trailers(std::ptr::null_mut(), filter),
      abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
    );
    envoy_dynamic_module_on_http_filter_destroy(filter);
  }

  assert!(ON_REQUEST_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_REQUEST_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_REQUEST_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}
