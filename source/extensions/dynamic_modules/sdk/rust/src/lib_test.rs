#![allow(clippy::unnecessary_cast)]
use crate::*;
#[cfg(test)]
use std::sync::atomic::AtomicBool; // This is used for testing the drop, not for the actual concurrency.

#[test]
fn test_loggers() {
  // Test that the loggers are defined and can be used during the unit tests, i.e., not trying to
  // find the symbol implemented by Envoy.
  envoy_log_trace!("message with an argument: {}", "argument");
  envoy_log_debug!("message with an argument: {}", "argument");
  envoy_log_info!("message with an argument: {}", "argument");
  envoy_log_warn!("message with an argument: {}", "argument");
  envoy_log_error!("message with an argument: {}", "argument");
}

#[test]
fn test_envoy_dynamic_module_on_http_filter_config_new_impl() {
  struct TestHttpFilterConfig;
  impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for TestHttpFilterConfig {}

  let mut envoy_filter_config = EnvoyHttpFilterConfigImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut new_fn: NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl> =
    |_, _, _| Some(Box::new(TestHttpFilterConfig));
  let result = envoy_dynamic_module_on_http_filter_config_new_impl(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
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
    b"test_config",
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
  impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for TestHttpFilterConfig {}
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
    b"test_config",
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
  impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for TestHttpFilterConfig {
    fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
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
    &mut EnvoyHttpFilterImpl {
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
  impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for TestHttpFilterConfig {
    fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
      Box::new(TestHttpFilter)
    }
  }

  static ON_REQUEST_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_REQUEST_BODY_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_REQUEST_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_HEADERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_BODY_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_RESPONSE_TRAILERS_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_STREAM_COMPLETE_CALLED: AtomicBool = AtomicBool::new(false);

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

    fn on_stream_complete(&mut self, _envoy_filter: &mut EHF) {
      ON_STREAM_COMPLETE_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestHttpFilterConfig;
  let filter = envoy_dynamic_module_on_http_filter_new_impl(
    &mut EnvoyHttpFilterImpl {
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
    envoy_dynamic_module_on_http_filter_stream_complete(std::ptr::null_mut(), filter);
    envoy_dynamic_module_on_http_filter_destroy(filter);
  }

  assert!(ON_REQUEST_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_REQUEST_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_REQUEST_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_HEADERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_BODY_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_RESPONSE_TRAILERS_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_STREAM_COMPLETE_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

// =============================================================================
// Listener Filter Tests
// =============================================================================

#[test]
fn test_envoy_dynamic_module_on_listener_filter_config_new_impl() {
  struct TestListenerFilterConfig;
  impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for TestListenerFilterConfig {
    fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
      Box::new(TestListenerFilter)
    }
  }

  struct TestListenerFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for TestListenerFilter {}

  let mut envoy_filter_config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };
  let mut new_fn: NewListenerFilterConfigFunction<
    EnvoyListenerFilterConfigImpl,
    EnvoyListenerFilterImpl,
  > = |_, _, _| Some(Box::new(TestListenerFilterConfig));
  let result = init_listener_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(!result.is_null());

  unsafe {
    envoy_dynamic_module_on_listener_filter_config_destroy(result);
  }

  // None should result in null pointer.
  new_fn = |_, _, _| None;
  let result = init_listener_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(result.is_null());
}

#[test]
fn test_envoy_dynamic_module_on_listener_filter_config_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestListenerFilterConfig;
  impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for TestListenerFilterConfig {
    fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
      Box::new(TestListenerFilter)
    }
  }
  impl Drop for TestListenerFilterConfig {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestListenerFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for TestListenerFilter {}

  let new_fn: NewListenerFilterConfigFunction<
    EnvoyListenerFilterConfigImpl,
    EnvoyListenerFilterImpl,
  > = |_, _, _| Some(Box::new(TestListenerFilterConfig));
  let config_ptr = init_listener_filter_config(
    &mut EnvoyListenerFilterConfigImpl {
      raw: std::ptr::null_mut(),
    },
    "test_name",
    b"test_config",
    &new_fn,
  );

  unsafe {
    envoy_dynamic_module_on_listener_filter_config_destroy(config_ptr);
  }
  // Now that the drop is called, DROPPED must be set to true.
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_listener_filter_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestListenerFilterConfig;
  impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for TestListenerFilterConfig {
    fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
      Box::new(TestListenerFilter)
    }
  }

  struct TestListenerFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for TestListenerFilter {}
  impl Drop for TestListenerFilter {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestListenerFilterConfig;
  let result = envoy_dynamic_module_on_listener_filter_new_impl(
    &mut EnvoyListenerFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );
  assert!(!result.is_null());

  envoy_dynamic_module_on_listener_filter_destroy(result);

  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_listener_filter_callbacks() {
  struct TestListenerFilterConfig;
  impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for TestListenerFilterConfig {
    fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
      Box::new(TestListenerFilter)
    }
  }

  static ON_ACCEPT_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_DATA_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_CLOSE_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestListenerFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for TestListenerFilter {
    fn on_accept(
      &mut self,
      _envoy_filter: &mut ELF,
    ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
      ON_ACCEPT_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
    }

    fn on_data(
      &mut self,
      _envoy_filter: &mut ELF,
    ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
      ON_DATA_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
    }

    fn on_close(&mut self, _envoy_filter: &mut ELF) {
      ON_CLOSE_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestListenerFilterConfig;
  let filter = envoy_dynamic_module_on_listener_filter_new_impl(
    &mut EnvoyListenerFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );

  assert_eq!(
    envoy_dynamic_module_on_listener_filter_on_accept(std::ptr::null_mut(), filter),
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  );
  assert_eq!(
    envoy_dynamic_module_on_listener_filter_on_data(std::ptr::null_mut(), filter),
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  );
  envoy_dynamic_module_on_listener_filter_on_close(std::ptr::null_mut(), filter);
  envoy_dynamic_module_on_listener_filter_destroy(filter);

  assert!(ON_ACCEPT_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_DATA_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_CLOSE_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

// =============================================================================
// Network Filter Tests
// =============================================================================

#[test]
fn test_envoy_dynamic_module_on_network_filter_config_new_impl() {
  struct TestNetworkFilterConfig;
  impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for TestNetworkFilterConfig {
    fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
      Box::new(TestNetworkFilter)
    }
  }

  struct TestNetworkFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestNetworkFilter {}

  let mut envoy_filter_config = EnvoyNetworkFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };
  let mut new_fn: NewNetworkFilterConfigFunction<
    EnvoyNetworkFilterConfigImpl,
    EnvoyNetworkFilterImpl,
  > = |_, _, _| Some(Box::new(TestNetworkFilterConfig));
  let result = init_network_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(!result.is_null());

  unsafe {
    envoy_dynamic_module_on_network_filter_config_destroy(result);
  }

  // None should result in null pointer.
  new_fn = |_, _, _| None;
  let result = init_network_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(result.is_null());
}

#[test]
fn test_envoy_dynamic_module_on_network_filter_config_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestNetworkFilterConfig;
  impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for TestNetworkFilterConfig {
    fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
      Box::new(TestNetworkFilter)
    }
  }
  impl Drop for TestNetworkFilterConfig {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestNetworkFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestNetworkFilter {}

  let new_fn: NewNetworkFilterConfigFunction<EnvoyNetworkFilterConfigImpl, EnvoyNetworkFilterImpl> =
    |_, _, _| Some(Box::new(TestNetworkFilterConfig));
  let config_ptr = init_network_filter_config(
    &mut EnvoyNetworkFilterConfigImpl {
      raw: std::ptr::null_mut(),
    },
    "test_name",
    b"test_config",
    &new_fn,
  );

  unsafe {
    envoy_dynamic_module_on_network_filter_config_destroy(config_ptr);
  }
  // Now that the drop is called, DROPPED must be set to true.
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_network_filter_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestNetworkFilterConfig;
  impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for TestNetworkFilterConfig {
    fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
      Box::new(TestNetworkFilter)
    }
  }

  struct TestNetworkFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestNetworkFilter {}
  impl Drop for TestNetworkFilter {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestNetworkFilterConfig;
  let result = envoy_dynamic_module_on_network_filter_new_impl(
    &mut EnvoyNetworkFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );
  assert!(!result.is_null());

  envoy_dynamic_module_on_network_filter_destroy(result);

  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_network_filter_callbacks() {
  struct TestNetworkFilterConfig;
  impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for TestNetworkFilterConfig {
    fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
      Box::new(TestNetworkFilter)
    }
  }

  static ON_NEW_CONNECTION_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_READ_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_WRITE_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_EVENT_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestNetworkFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestNetworkFilter {
    fn on_new_connection(
      &mut self,
      _envoy_filter: &mut ENF,
    ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
      ON_NEW_CONNECTION_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
    }

    fn on_read(
      &mut self,
      _envoy_filter: &mut ENF,
      _data_length: usize,
      _end_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
      ON_READ_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
    }

    fn on_write(
      &mut self,
      _envoy_filter: &mut ENF,
      _data_length: usize,
      _end_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
      ON_WRITE_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
    }

    fn on_event(
      &mut self,
      _envoy_filter: &mut ENF,
      _event: abi::envoy_dynamic_module_type_network_connection_event,
    ) {
      ON_EVENT_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestNetworkFilterConfig;
  let filter = envoy_dynamic_module_on_network_filter_new_impl(
    &mut EnvoyNetworkFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );

  assert_eq!(
    envoy_dynamic_module_on_network_filter_new_connection(std::ptr::null_mut(), filter),
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  );
  assert_eq!(
    envoy_dynamic_module_on_network_filter_read(std::ptr::null_mut(), filter, 100, false),
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  );
  assert_eq!(
    envoy_dynamic_module_on_network_filter_write(std::ptr::null_mut(), filter, 100, false),
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  );
  envoy_dynamic_module_on_network_filter_event(
    std::ptr::null_mut(),
    filter,
    abi::envoy_dynamic_module_type_network_connection_event::RemoteClose,
  );
  envoy_dynamic_module_on_network_filter_destroy(filter);

  assert!(ON_NEW_CONNECTION_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_READ_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_WRITE_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_EVENT_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

// =============================================================================
// Socket option FFI stubs for testing.
// =============================================================================

#[derive(Clone)]
struct StoredOption {
  level: i64,
  name: i64,
  state: abi::envoy_dynamic_module_type_socket_option_state,
  value: Option<Vec<u8>>,
  int_value: Option<i64>,
}

static STORED_OPTIONS: std::sync::Mutex<Vec<StoredOption>> = std::sync::Mutex::new(Vec::new());

fn reset_socket_options() {
  STORED_OPTIONS.lock().unwrap().clear();
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_set_socket_option_int(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  level: i64,
  name: i64,
  state: abi::envoy_dynamic_module_type_socket_option_state,
  value: i64,
) -> bool {
  STORED_OPTIONS.lock().unwrap().push(StoredOption {
    level,
    name,
    state,
    value: None,
    int_value: Some(value),
  });
  true
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_set_socket_option_bytes(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  level: i64,
  name: i64,
  state: abi::envoy_dynamic_module_type_socket_option_state,
  value: abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  let slice = unsafe { std::slice::from_raw_parts(value.ptr as *const u8, value.length) };
  STORED_OPTIONS.lock().unwrap().push(StoredOption {
    level,
    name,
    state,
    value: Some(slice.to_vec()),
    int_value: None,
  });
  true
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_get_socket_option_int(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  level: i64,
  name: i64,
  state: abi::envoy_dynamic_module_type_socket_option_state,
  value_out: *mut i64,
) -> bool {
  let options = STORED_OPTIONS.lock().unwrap();
  options.iter().any(|opt| {
    if opt.level == level && opt.name == name && opt.state == state {
      if let Some(v) = opt.int_value {
        if !value_out.is_null() {
          unsafe {
            *value_out = v;
          }
        }
        return true;
      }
    }
    false
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_get_socket_option_bytes(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  level: i64,
  name: i64,
  state: abi::envoy_dynamic_module_type_socket_option_state,
  value_out: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  let options = STORED_OPTIONS.lock().unwrap();
  options.iter().any(|opt| {
    if opt.level == level && opt.name == name && opt.state == state {
      if let Some(ref bytes) = opt.value {
        if !value_out.is_null() {
          unsafe {
            (*value_out).ptr = bytes.as_ptr() as *const _;
            (*value_out).length = bytes.len();
          }
        }
        return true;
      }
    }
    false
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_get_socket_options_size(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> usize {
  STORED_OPTIONS.lock().unwrap().len()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_get_socket_options(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  options_out: *mut abi::envoy_dynamic_module_type_socket_option,
) {
  if options_out.is_null() {
    return;
  }
  let options = STORED_OPTIONS.lock().unwrap();
  let mut written = 0usize;
  for opt in options.iter() {
    unsafe {
      let out = options_out.add(written);
      (*out).level = opt.level;
      (*out).name = opt.name;
      (*out).state = opt.state;
      match opt.int_value {
        Some(v) => {
          (*out).value_type = abi::envoy_dynamic_module_type_socket_option_value_type::Int;
          (*out).int_value = v;
          (*out).byte_value.ptr = std::ptr::null();
          (*out).byte_value.length = 0;
        },
        None => {
          (*out).value_type = abi::envoy_dynamic_module_type_socket_option_value_type::Bytes;
          if let Some(ref bytes) = opt.value {
            (*out).byte_value.ptr = bytes.as_ptr() as *const _;
            (*out).byte_value.length = bytes.len();
          } else {
            (*out).byte_value.ptr = std::ptr::null();
            (*out).byte_value.length = 0;
          }
          (*out).int_value = 0;
        },
      }
    }
    written += 1;
  }
}

#[test]
fn test_socket_option_int_round_trip() {
  reset_socket_options();
  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };
  filter.set_socket_option_int(
    1,
    2,
    abi::envoy_dynamic_module_type_socket_option_state::Prebind,
    42,
  );
  let value = filter.get_socket_option_int(
    1,
    2,
    abi::envoy_dynamic_module_type_socket_option_state::Prebind,
  );
  assert_eq!(Some(42), value);
}

#[test]
fn test_socket_option_bytes_round_trip() {
  reset_socket_options();
  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };
  filter.set_socket_option_bytes(
    3,
    4,
    abi::envoy_dynamic_module_type_socket_option_state::Bound,
    b"bytes-val",
  );
  let value = filter.get_socket_option_bytes(
    3,
    4,
    abi::envoy_dynamic_module_type_socket_option_state::Bound,
  );
  assert_eq!(Some(b"bytes-val".to_vec()), value);
}

#[test]
fn test_socket_option_list() {
  reset_socket_options();
  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };
  filter.set_socket_option_int(
    5,
    6,
    abi::envoy_dynamic_module_type_socket_option_state::Prebind,
    11,
  );
  filter.set_socket_option_bytes(
    7,
    8,
    abi::envoy_dynamic_module_type_socket_option_state::Listening,
    b"data",
  );

  let options = filter.get_socket_options();
  assert_eq!(2, options.len());
  match &options[0].value {
    SocketOptionValue::Int(v) => assert_eq!(&11, v),
    _ => panic!("expected int"),
  }
  match &options[1].value {
    SocketOptionValue::Bytes(bytes) => assert_eq!(b"data".to_vec(), *bytes),
    _ => panic!("expected bytes"),
  }
}

// =============================================================================
// UDP Listener Filter Tests
// =============================================================================

#[test]
fn test_envoy_dynamic_module_on_udp_listener_filter_config_new_impl() {
  struct TestUdpListenerFilterConfig;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for TestUdpListenerFilterConfig {
    fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
      Box::new(TestUdpListenerFilter)
    }
  }

  struct TestUdpListenerFilter;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for TestUdpListenerFilter {}

  let mut envoy_filter_config = EnvoyUdpListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };
  let mut new_fn: NewUdpListenerFilterConfigFunction<
    EnvoyUdpListenerFilterConfigImpl,
    EnvoyUdpListenerFilterImpl,
  > = |_, _, _| Some(Box::new(TestUdpListenerFilterConfig));
  let result = init_udp_listener_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(!result.is_null());

  unsafe {
    envoy_dynamic_module_on_udp_listener_filter_config_destroy(result);
  }

  // None should result in null pointer.
  new_fn = |_, _, _| None;
  let result = init_udp_listener_filter_config(
    &mut envoy_filter_config,
    "test_name",
    b"test_config",
    &new_fn,
  );
  assert!(result.is_null());
}

#[test]
fn test_envoy_dynamic_module_on_udp_listener_filter_config_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestUdpListenerFilterConfig;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for TestUdpListenerFilterConfig {
    fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
      Box::new(TestUdpListenerFilter)
    }
  }
  impl Drop for TestUdpListenerFilterConfig {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestUdpListenerFilter;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for TestUdpListenerFilter {}

  let new_fn: NewUdpListenerFilterConfigFunction<
    EnvoyUdpListenerFilterConfigImpl,
    EnvoyUdpListenerFilterImpl,
  > = |_, _, _| Some(Box::new(TestUdpListenerFilterConfig));
  let config_ptr = init_udp_listener_filter_config(
    &mut EnvoyUdpListenerFilterConfigImpl {
      raw: std::ptr::null_mut(),
    },
    "test_name",
    b"test_config",
    &new_fn,
  );

  unsafe {
    envoy_dynamic_module_on_udp_listener_filter_config_destroy(config_ptr);
  }
  // Now that the drop is called, DROPPED must be set to true.
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_udp_listener_filter_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);
  struct TestUdpListenerFilterConfig;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for TestUdpListenerFilterConfig {
    fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
      Box::new(TestUdpListenerFilter)
    }
  }

  struct TestUdpListenerFilter;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for TestUdpListenerFilter {}
  impl Drop for TestUdpListenerFilter {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestUdpListenerFilterConfig;
  let result = envoy_dynamic_module_on_udp_listener_filter_new_impl(
    &mut EnvoyUdpListenerFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );
  assert!(!result.is_null());

  envoy_dynamic_module_on_udp_listener_filter_destroy(result);

  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_envoy_dynamic_module_on_udp_listener_filter_callbacks() {
  struct TestUdpListenerFilterConfig;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilterConfig<ELF> for TestUdpListenerFilterConfig {
    fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>> {
      Box::new(TestUdpListenerFilter)
    }
  }

  static ON_DATA_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestUdpListenerFilter;
  impl<ELF: EnvoyUdpListenerFilter> UdpListenerFilter<ELF> for TestUdpListenerFilter {
    fn on_data(
      &mut self,
      _envoy_filter: &mut ELF,
    ) -> abi::envoy_dynamic_module_type_on_udp_listener_filter_status {
      ON_DATA_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      abi::envoy_dynamic_module_type_on_udp_listener_filter_status::Continue
    }
  }

  let mut filter_config = TestUdpListenerFilterConfig;
  let filter = envoy_dynamic_module_on_udp_listener_filter_new_impl(
    &mut EnvoyUdpListenerFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );

  assert_eq!(
    envoy_dynamic_module_on_udp_listener_filter_on_data(std::ptr::null_mut(), filter),
    abi::envoy_dynamic_module_type_on_udp_listener_filter_status::Continue
  );
  envoy_dynamic_module_on_udp_listener_filter_destroy(filter);

  assert!(ON_DATA_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

// =============================================================================
// Upstream Host Access and StartTLS FFI stubs for testing.
// =============================================================================

#[derive(Clone, Default)]
struct MockUpstreamHost {
  address: Option<String>,
  port: u32,
  hostname: Option<String>,
  cluster_name: Option<String>,
}

static MOCK_UPSTREAM_HOST: std::sync::Mutex<Option<MockUpstreamHost>> = std::sync::Mutex::new(None);
static MOCK_START_TLS_RESULT: std::sync::atomic::AtomicBool =
  std::sync::atomic::AtomicBool::new(false);

fn reset_upstream_host_mock() {
  *MOCK_UPSTREAM_HOST.lock().unwrap() = None;
  MOCK_START_TLS_RESULT.store(false, std::sync::atomic::Ordering::SeqCst);
}

fn set_upstream_host_mock(host: MockUpstreamHost) {
  *MOCK_UPSTREAM_HOST.lock().unwrap() = Some(host);
}

fn set_start_tls_result(result: bool) {
  MOCK_START_TLS_RESULT.store(result, std::sync::atomic::Ordering::SeqCst);
}

// Keep a static buffer for the address string to ensure it remains valid.
static MOCK_ADDRESS_BUFFER: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
static MOCK_HOSTNAME_BUFFER: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
static MOCK_CLUSTER_BUFFER: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  address_out: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  port_out: *mut u32,
) -> bool {
  let guard = MOCK_UPSTREAM_HOST.lock().unwrap();
  match &*guard {
    Some(host) => match &host.address {
      Some(addr) => {
        // Store address in static buffer to maintain lifetime.
        let mut buf = MOCK_ADDRESS_BUFFER.lock().unwrap();
        *buf = addr.clone();
        unsafe {
          (*address_out).ptr = buf.as_ptr() as *const _;
          (*address_out).length = buf.len();
          *port_out = host.port;
        }
        true
      },
      None => {
        unsafe {
          (*address_out).ptr = std::ptr::null();
          (*address_out).length = 0;
          *port_out = 0;
        }
        false
      },
    },
    None => {
      unsafe {
        (*address_out).ptr = std::ptr::null();
        (*address_out).length = 0;
        *port_out = 0;
      }
      false
    },
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  hostname_out: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  let guard = MOCK_UPSTREAM_HOST.lock().unwrap();
  match &*guard {
    Some(host) => match &host.hostname {
      Some(hostname) if !hostname.is_empty() => {
        // Store hostname in static buffer to maintain lifetime.
        let mut buf = MOCK_HOSTNAME_BUFFER.lock().unwrap();
        *buf = hostname.clone();
        unsafe {
          (*hostname_out).ptr = buf.as_ptr() as *const _;
          (*hostname_out).length = buf.len();
        }
        true
      },
      _ => {
        unsafe {
          (*hostname_out).ptr = std::ptr::null();
          (*hostname_out).length = 0;
        }
        false
      },
    },
    None => {
      unsafe {
        (*hostname_out).ptr = std::ptr::null();
        (*hostname_out).length = 0;
      }
      false
    },
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  cluster_name_out: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  let guard = MOCK_UPSTREAM_HOST.lock().unwrap();
  match &*guard {
    Some(host) => match &host.cluster_name {
      Some(cluster) => {
        // Store cluster name in static buffer to maintain lifetime.
        let mut buf = MOCK_CLUSTER_BUFFER.lock().unwrap();
        *buf = cluster.clone();
        unsafe {
          (*cluster_name_out).ptr = buf.as_ptr() as *const _;
          (*cluster_name_out).length = buf.len();
        }
        true
      },
      None => {
        unsafe {
          (*cluster_name_out).ptr = std::ptr::null();
          (*cluster_name_out).length = 0;
        }
        false
      },
    },
    None => {
      unsafe {
        (*cluster_name_out).ptr = std::ptr::null();
        (*cluster_name_out).length = 0;
      }
      false
    },
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_has_upstream_host(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> bool {
  MOCK_UPSTREAM_HOST.lock().unwrap().is_some()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> bool {
  MOCK_START_TLS_RESULT.load(std::sync::atomic::Ordering::SeqCst)
}

// =============================================================================
// Upstream Host Access Tests
// =============================================================================

#[test]
fn test_get_upstream_host_address_with_host() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("192.168.1.100".to_string()),
    port: 8080,
    hostname: Some("backend.local".to_string()),
    cluster_name: Some("my_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_address();
  assert!(result.is_some());
  let (addr, port) = result.unwrap();
  assert_eq!(addr, "192.168.1.100");
  assert_eq!(port, 8080);
}

#[test]
fn test_get_upstream_host_address_no_host() {
  reset_upstream_host_mock();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_address();
  assert!(result.is_none());
}

#[test]
fn test_get_upstream_host_address_no_ip() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: None, // No IP address (e.g., pipe address).
    port: 0,
    hostname: Some("backend.local".to_string()),
    cluster_name: Some("my_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_address();
  assert!(result.is_none());
}

#[test]
fn test_get_upstream_host_hostname_with_host() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("10.0.0.1".to_string()),
    port: 443,
    hostname: Some("api.example.com".to_string()),
    cluster_name: Some("api_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_hostname();
  assert!(result.is_some());
  assert_eq!(result.unwrap(), "api.example.com");
}

#[test]
fn test_get_upstream_host_hostname_no_host() {
  reset_upstream_host_mock();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_hostname();
  assert!(result.is_none());
}

#[test]
fn test_get_upstream_host_hostname_empty() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("10.0.0.1".to_string()),
    port: 443,
    hostname: Some("".to_string()), // Empty hostname.
    cluster_name: Some("api_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_hostname();
  assert!(result.is_none());
}

#[test]
fn test_get_upstream_host_cluster_with_host() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("172.16.0.50".to_string()),
    port: 9000,
    hostname: Some("service.internal".to_string()),
    cluster_name: Some("backend_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_cluster();
  assert!(result.is_some());
  assert_eq!(result.unwrap(), "backend_cluster");
}

#[test]
fn test_get_upstream_host_cluster_no_host() {
  reset_upstream_host_mock();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_upstream_host_cluster();
  assert!(result.is_none());
}

#[test]
fn test_has_upstream_host_true() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("10.0.0.1".to_string()),
    port: 80,
    hostname: None,
    cluster_name: Some("test_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  assert!(filter.has_upstream_host());
}

#[test]
fn test_has_upstream_host_false() {
  reset_upstream_host_mock();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  assert!(!filter.has_upstream_host());
}

// =============================================================================
// StartTLS Tests
// =============================================================================

#[test]
fn test_start_upstream_secure_transport_success() {
  reset_upstream_host_mock();
  set_start_tls_result(true);

  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  assert!(filter.start_upstream_secure_transport());
}

#[test]
fn test_start_upstream_secure_transport_failure() {
  reset_upstream_host_mock();
  set_start_tls_result(false);

  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  assert!(!filter.start_upstream_secure_transport());
}

// =============================================================================
// Combined Upstream Host Access Tests
// =============================================================================

#[test]
fn test_upstream_host_full_info() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("10.20.30.40".to_string()),
    port: 8443,
    hostname: Some("secure-backend.example.com".to_string()),
    cluster_name: Some("secure_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Verify all fields are accessible.
  assert!(filter.has_upstream_host());

  let addr_result = filter.get_upstream_host_address();
  assert!(addr_result.is_some());
  let (addr, port) = addr_result.unwrap();
  assert_eq!(addr, "10.20.30.40");
  assert_eq!(port, 8443);

  let hostname_result = filter.get_upstream_host_hostname();
  assert!(hostname_result.is_some());
  assert_eq!(hostname_result.unwrap(), "secure-backend.example.com");

  let cluster_result = filter.get_upstream_host_cluster();
  assert!(cluster_result.is_some());
  assert_eq!(cluster_result.unwrap(), "secure_cluster");
}

#[test]
fn test_upstream_host_partial_info() {
  reset_upstream_host_mock();
  // Host with address but no hostname.
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("192.168.0.1".to_string()),
    port: 3000,
    hostname: None,
    cluster_name: Some("partial_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  assert!(filter.has_upstream_host());

  // Address should be available.
  let addr_result = filter.get_upstream_host_address();
  assert!(addr_result.is_some());
  let (addr, port) = addr_result.unwrap();
  assert_eq!(addr, "192.168.0.1");
  assert_eq!(port, 3000);

  // Hostname should be None.
  assert!(filter.get_upstream_host_hostname().is_none());

  // Cluster should be available.
  let cluster_result = filter.get_upstream_host_cluster();
  assert!(cluster_result.is_some());
  assert_eq!(cluster_result.unwrap(), "partial_cluster");
}

#[test]
fn test_upstream_host_ipv6_address() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("::1".to_string()),
    port: 8080,
    hostname: Some("localhost".to_string()),
    cluster_name: Some("ipv6_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let addr_result = filter.get_upstream_host_address();
  assert!(addr_result.is_some());
  let (addr, port) = addr_result.unwrap();
  assert_eq!(addr, "::1");
  assert_eq!(port, 8080);
}

#[test]
fn test_upstream_host_full_ipv6_address() {
  reset_upstream_host_mock();
  set_upstream_host_mock(MockUpstreamHost {
    address: Some("2001:0db8:85a3:0000:0000:8a2e:0370:7334".to_string()),
    port: 443,
    hostname: Some("ipv6-host.example.com".to_string()),
    cluster_name: Some("ipv6_full_cluster".to_string()),
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let addr_result = filter.get_upstream_host_address();
  assert!(addr_result.is_some());
  let (addr, port) = addr_result.unwrap();
  assert_eq!(addr, "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  assert_eq!(port, 443);
}

// =============================================================================
// Connection State and Flow Control FFI stubs for testing.
// =============================================================================

static MOCK_CONNECTION_STATE: std::sync::Mutex<
  abi::envoy_dynamic_module_type_network_connection_state,
> = std::sync::Mutex::new(abi::envoy_dynamic_module_type_network_connection_state::Open);
static MOCK_HALF_CLOSE_ENABLED: AtomicBool = AtomicBool::new(false);
static MOCK_READ_ENABLED: AtomicBool = AtomicBool::new(true);
static MOCK_BUFFER_LIMIT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
static MOCK_ABOVE_HIGH_WATERMARK: AtomicBool = AtomicBool::new(false);

fn set_mock_connection_state(state: abi::envoy_dynamic_module_type_network_connection_state) {
  *MOCK_CONNECTION_STATE.lock().unwrap() = state;
}

fn reset_mock_connection_state() {
  *MOCK_CONNECTION_STATE.lock().unwrap() =
    abi::envoy_dynamic_module_type_network_connection_state::Open;
  MOCK_HALF_CLOSE_ENABLED.store(false, std::sync::atomic::Ordering::SeqCst);
  MOCK_READ_ENABLED.store(true, std::sync::atomic::Ordering::SeqCst);
  MOCK_BUFFER_LIMIT.store(0, std::sync::atomic::Ordering::SeqCst);
  MOCK_ABOVE_HIGH_WATERMARK.store(false, std::sync::atomic::Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_connection_state(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_network_connection_state {
  *MOCK_CONNECTION_STATE.lock().unwrap()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_is_half_close_enabled(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> bool {
  MOCK_HALF_CLOSE_ENABLED.load(std::sync::atomic::Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_enable_half_close(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  enabled: bool,
) {
  MOCK_HALF_CLOSE_ENABLED.store(enabled, std::sync::atomic::Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_read_disable(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  disable: bool,
) -> abi::envoy_dynamic_module_type_network_read_disable_status {
  let was_enabled = MOCK_READ_ENABLED.load(std::sync::atomic::Ordering::SeqCst);
  MOCK_READ_ENABLED.store(!disable, std::sync::atomic::Ordering::SeqCst);
  // Return the appropriate status based on transition.
  if was_enabled && disable {
    abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadDisabled
  } else if !was_enabled && !disable {
    abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadEnabled
  } else if !was_enabled && disable {
    abi::envoy_dynamic_module_type_network_read_disable_status::StillReadDisabled
  } else {
    abi::envoy_dynamic_module_type_network_read_disable_status::NoTransition
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_read_enabled(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> bool {
  MOCK_READ_ENABLED.load(std::sync::atomic::Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_buffer_limit(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> u32 {
  MOCK_BUFFER_LIMIT.load(std::sync::atomic::Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_set_buffer_limits(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  limit: u32,
) {
  MOCK_BUFFER_LIMIT.store(limit, std::sync::atomic::Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_above_high_watermark(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
) -> bool {
  MOCK_ABOVE_HIGH_WATERMARK.load(std::sync::atomic::Ordering::SeqCst)
}

// =============================================================================
// Connection State and Flow Control Tests
// =============================================================================

#[test]
fn test_get_connection_state() {
  reset_mock_connection_state();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Default state is Open.
  assert_eq!(
    filter.get_connection_state(),
    abi::envoy_dynamic_module_type_network_connection_state::Open
  );

  // Test Closing state.
  set_mock_connection_state(abi::envoy_dynamic_module_type_network_connection_state::Closing);
  assert_eq!(
    filter.get_connection_state(),
    abi::envoy_dynamic_module_type_network_connection_state::Closing
  );

  // Test Closed state.
  set_mock_connection_state(abi::envoy_dynamic_module_type_network_connection_state::Closed);
  assert_eq!(
    filter.get_connection_state(),
    abi::envoy_dynamic_module_type_network_connection_state::Closed
  );
}

#[test]
fn test_half_close_enabled() {
  reset_mock_connection_state();

  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Default is disabled.
  assert!(!filter.is_half_close_enabled());

  // Enable half-close.
  filter.enable_half_close(true);
  assert!(filter.is_half_close_enabled());

  // Disable half-close.
  filter.enable_half_close(false);
  assert!(!filter.is_half_close_enabled());
}

#[test]
fn test_read_disable() {
  reset_mock_connection_state();

  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Default read is enabled.
  assert!(filter.read_enabled());

  // Disable reads (should transition to disabled).
  let status = filter.read_disable(true);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadDisabled
  );
  assert!(!filter.read_enabled());

  // Disable reads again (should indicate still disabled).
  let status = filter.read_disable(true);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_network_read_disable_status::StillReadDisabled
  );
  assert!(!filter.read_enabled());

  // Enable reads (should transition to enabled).
  let status = filter.read_disable(false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadEnabled
  );
  assert!(filter.read_enabled());

  // Enable reads again (should indicate no transition).
  let status = filter.read_disable(false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_network_read_disable_status::NoTransition
  );
  assert!(filter.read_enabled());
}

#[test]
fn test_buffer_limits() {
  reset_mock_connection_state();

  let mut filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Default buffer limit is 0.
  assert_eq!(filter.get_buffer_limit(), 0);

  // Set buffer limit.
  filter.set_buffer_limits(16384);
  assert_eq!(filter.get_buffer_limit(), 16384);

  // Set different buffer limit.
  filter.set_buffer_limits(32768);
  assert_eq!(filter.get_buffer_limit(), 32768);
}

#[test]
fn test_above_high_watermark() {
  reset_mock_connection_state();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Default is not above high watermark.
  assert!(!filter.above_high_watermark());

  // Set above high watermark.
  MOCK_ABOVE_HIGH_WATERMARK.store(true, std::sync::atomic::Ordering::SeqCst);
  assert!(filter.above_high_watermark());

  // Clear above high watermark.
  MOCK_ABOVE_HIGH_WATERMARK.store(false, std::sync::atomic::Ordering::SeqCst);
  assert!(!filter.above_high_watermark());
}

#[test]
fn test_network_filter_watermark_callbacks() {
  struct TestNetworkFilterConfig;
  impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for TestNetworkFilterConfig {
    fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
      Box::new(TestNetworkFilter)
    }
  }

  static ON_ABOVE_HIGH_WATERMARK_CALLED: AtomicBool = AtomicBool::new(false);
  static ON_BELOW_LOW_WATERMARK_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestNetworkFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for TestNetworkFilter {
    fn on_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut ENF) {
      ON_ABOVE_HIGH_WATERMARK_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }

    fn on_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut ENF) {
      ON_BELOW_LOW_WATERMARK_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let mut filter_config = TestNetworkFilterConfig;
  let filter = envoy_dynamic_module_on_network_filter_new_impl(
    &mut EnvoyNetworkFilterImpl {
      raw: std::ptr::null_mut(),
    },
    &mut filter_config,
  );

  // Call the watermark event hooks.
  envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
    std::ptr::null_mut(),
    filter,
  );
  envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
    std::ptr::null_mut(),
    filter,
  );

  envoy_dynamic_module_on_network_filter_destroy(filter);

  assert!(ON_ABOVE_HIGH_WATERMARK_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(ON_BELOW_LOW_WATERMARK_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}
