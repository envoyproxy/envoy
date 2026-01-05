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
  assert!(filter.set_socket_option_int(
    1,
    2,
    abi::envoy_dynamic_module_type_socket_option_state::Prebind,
    42
  ));
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
  assert!(filter.set_socket_option_bytes(
    3,
    4,
    abi::envoy_dynamic_module_type_socket_option_state::Bound,
    b"bytes-val",
  ));
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
  assert!(filter.set_socket_option_int(
    5,
    6,
    abi::envoy_dynamic_module_type_socket_option_state::Prebind,
    11
  ));
  assert!(filter.set_socket_option_bytes(
    7,
    8,
    abi::envoy_dynamic_module_type_socket_option_state::Listening,
    b"data",
  ));

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
