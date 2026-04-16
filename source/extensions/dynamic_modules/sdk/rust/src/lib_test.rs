#![allow(clippy::unnecessary_cast)]
use crate::*;
#[cfg(test)]
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicUsize};

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
  let result = listener::init_listener_filter_config(
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
  let result = listener::init_listener_filter_config(
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
  let config_ptr = listener::init_listener_filter_config(
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
  let result = listener::envoy_dynamic_module_on_listener_filter_new_impl(
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
  let filter = listener::envoy_dynamic_module_on_listener_filter_new_impl(
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
// Listener Filter Metrics FFI stubs for testing.
// =============================================================================

/// Tracks the metrics defined and manipulated by listener filter metrics stubs.
struct ListenerFilterMetricEntry {
  name: String,
  value: u64,
}

static LISTENER_FILTER_COUNTERS: std::sync::Mutex<Vec<ListenerFilterMetricEntry>> =
  std::sync::Mutex::new(Vec::new());
static LISTENER_FILTER_GAUGES: std::sync::Mutex<Vec<ListenerFilterMetricEntry>> =
  std::sync::Mutex::new(Vec::new());
static LISTENER_FILTER_HISTOGRAMS: std::sync::Mutex<Vec<ListenerFilterMetricEntry>> =
  std::sync::Mutex::new(Vec::new());

fn reset_listener_filter_metrics() {
  LISTENER_FILTER_COUNTERS.lock().unwrap().clear();
  LISTENER_FILTER_GAUGES.lock().unwrap().clear();
  LISTENER_FILTER_HISTOGRAMS.lock().unwrap().clear();
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_config_define_counter(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_module_buffer,
  counter_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const u8,
      name.length,
    ))
  };
  let mut counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
  let id = counters.len();
  counters.push(ListenerFilterMetricEntry {
    name: name_str.to_string(),
    value: 0,
  });
  unsafe {
    *counter_id_ptr = id;
  }
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_increment_counter(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  id: usize,
  value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let mut counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
  if id >= counters.len() {
    return abi::envoy_dynamic_module_type_metrics_result::MetricNotFound;
  }
  counters[id].value += value;
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_config_define_gauge(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_module_buffer,
  gauge_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const u8,
      name.length,
    ))
  };
  let mut gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
  let id = gauges.len();
  gauges.push(ListenerFilterMetricEntry {
    name: name_str.to_string(),
    value: 0,
  });
  unsafe {
    *gauge_id_ptr = id;
  }
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_set_gauge(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  id: usize,
  value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let mut gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
  if id >= gauges.len() {
    return abi::envoy_dynamic_module_type_metrics_result::MetricNotFound;
  }
  gauges[id].value = value;
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_increment_gauge(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  id: usize,
  value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let mut gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
  if id >= gauges.len() {
    return abi::envoy_dynamic_module_type_metrics_result::MetricNotFound;
  }
  gauges[id].value += value;
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_decrement_gauge(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  id: usize,
  value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let mut gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
  if id >= gauges.len() {
    return abi::envoy_dynamic_module_type_metrics_result::MetricNotFound;
  }
  gauges[id].value = gauges[id].value.saturating_sub(value);
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_config_define_histogram(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_module_buffer,
  histogram_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const u8,
      name.length,
    ))
  };
  let mut histograms = LISTENER_FILTER_HISTOGRAMS.lock().unwrap();
  let id = histograms.len();
  histograms.push(ListenerFilterMetricEntry {
    name: name_str.to_string(),
    value: 0,
  });
  unsafe {
    *histogram_id_ptr = id;
  }
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_record_histogram_value(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  id: usize,
  value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  let mut histograms = LISTENER_FILTER_HISTOGRAMS.lock().unwrap();
  if id >= histograms.len() {
    return abi::envoy_dynamic_module_type_metrics_result::MetricNotFound;
  }
  histograms[id].value = value;
  abi::envoy_dynamic_module_type_metrics_result::Success
}

// =============================================================================
// Listener Filter Metrics Tests
// =============================================================================

#[test]
fn test_listener_filter_config_define_and_increment_counter() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  let counter_id = config.define_counter("test_counter");
  assert!(counter_id.is_ok());
  let counter_id = counter_id.unwrap();

  // Verify the counter was registered with the correct name.
  {
    let counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
    assert_eq!(1, counters.len());
    assert_eq!("test_counter", counters[0].name);
    assert_eq!(0, counters[0].value);
  }

  // Increment the counter via the filter.
  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };
  let result = filter.increment_counter(counter_id, 5);
  assert!(result.is_ok());

  // Verify the counter value was incremented.
  {
    let counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
    assert_eq!(5, counters[0].value);
  }

  // Increment again.
  let result = filter.increment_counter(counter_id, 3);
  assert!(result.is_ok());

  {
    let counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
    assert_eq!(8, counters[0].value);
  }
}

#[test]
fn test_listener_filter_config_define_multiple_counters() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  let id1 = config.define_counter("counter_a").unwrap();
  let id2 = config.define_counter("counter_b").unwrap();

  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };
  filter.increment_counter(id1, 10).unwrap();
  filter.increment_counter(id2, 20).unwrap();

  let counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
  assert_eq!(2, counters.len());
  assert_eq!(10, counters[0].value);
  assert_eq!(20, counters[1].value);
}

#[test]
fn test_listener_filter_counter_invalid_id() {
  reset_listener_filter_metrics();
  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Incrementing a counter with an invalid ID should return an error.
  let result = filter.increment_counter(EnvoyCounterId(999), 1);
  assert!(result.is_err());
}

#[test]
fn test_listener_filter_config_define_and_manipulate_gauge() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  let gauge_id = config.define_gauge("test_gauge").unwrap();

  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Set gauge value.
  filter.set_gauge(gauge_id, 42).unwrap();
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(42, gauges[0].value);
  }

  // Increase gauge.
  filter.increase_gauge(gauge_id, 8).unwrap();
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(50, gauges[0].value);
  }

  // Decrease gauge.
  filter.decrease_gauge(gauge_id, 10).unwrap();
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(40, gauges[0].value);
  }

  // Set gauge to a new value.
  filter.set_gauge(gauge_id, 0).unwrap();
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(0, gauges[0].value);
  }
}

#[test]
fn test_listener_filter_gauge_invalid_id() {
  reset_listener_filter_metrics();
  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // All gauge operations with an invalid ID should return an error.
  assert!(filter.set_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(filter.increase_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(filter.decrease_gauge(EnvoyGaugeId(999), 1).is_err());
}

#[test]
fn test_listener_filter_gauge_decrease_saturates_at_zero() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  let gauge_id = config.define_gauge("saturating_gauge").unwrap();

  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Set gauge to 5 and decrease by 10 - should saturate at 0.
  filter.set_gauge(gauge_id, 5).unwrap();
  filter.decrease_gauge(gauge_id, 10).unwrap();
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(0, gauges[0].value);
  }
}

#[test]
fn test_listener_filter_config_define_and_record_histogram() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  let histogram_id = config.define_histogram("test_histogram").unwrap();

  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Record a value in the histogram.
  filter.record_histogram_value(histogram_id, 100).unwrap();
  {
    let histograms = LISTENER_FILTER_HISTOGRAMS.lock().unwrap();
    assert_eq!(100, histograms[0].value);
  }

  // Record another value.
  filter.record_histogram_value(histogram_id, 250).unwrap();
  {
    let histograms = LISTENER_FILTER_HISTOGRAMS.lock().unwrap();
    assert_eq!(250, histograms[0].value);
  }
}

#[test]
fn test_listener_filter_histogram_invalid_id() {
  reset_listener_filter_metrics();
  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Recording a histogram value with an invalid ID should return an error.
  let result = filter.record_histogram_value(EnvoyHistogramId(999), 1);
  assert!(result.is_err());
}

#[test]
fn test_listener_filter_define_all_metric_types() {
  reset_listener_filter_metrics();
  let mut config = EnvoyListenerFilterConfigImpl {
    raw: std::ptr::null_mut(),
  };

  // Define one of each metric type.
  let counter_id = config.define_counter("my_counter").unwrap();
  let gauge_id = config.define_gauge("my_gauge").unwrap();
  let histogram_id = config.define_histogram("my_histogram").unwrap();

  let filter = EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };

  // Exercise all metric types.
  filter.increment_counter(counter_id, 1).unwrap();
  filter.set_gauge(gauge_id, 42).unwrap();
  filter.record_histogram_value(histogram_id, 100).unwrap();

  // Verify all values.
  {
    let counters = LISTENER_FILTER_COUNTERS.lock().unwrap();
    assert_eq!(1, counters[0].value);
    assert_eq!("my_counter", counters[0].name);
  }
  {
    let gauges = LISTENER_FILTER_GAUGES.lock().unwrap();
    assert_eq!(42, gauges[0].value);
    assert_eq!("my_gauge", gauges[0].name);
  }
  {
    let histograms = LISTENER_FILTER_HISTOGRAMS.lock().unwrap();
    assert_eq!(100, histograms[0].value);
    assert_eq!("my_histogram", histograms[0].name);
  }
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
  let result = network::init_network_filter_config(
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
  let result = network::init_network_filter_config(
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
  let config_ptr = network::init_network_filter_config(
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
  let result = network::envoy_dynamic_module_on_network_filter_new_impl(
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
  let filter = network::envoy_dynamic_module_on_network_filter_new_impl(
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
  let result = udp_listener::init_udp_listener_filter_config(
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
  let result = udp_listener::init_udp_listener_filter_config(
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
  let config_ptr = udp_listener::init_udp_listener_filter_config(
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
  let result = udp_listener::envoy_dynamic_module_on_udp_listener_filter_new_impl(
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
  let filter = udp_listener::envoy_dynamic_module_on_udp_listener_filter_new_impl(
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
// Cluster Host Count FFI stubs and tests.
// =============================================================================

struct MockClusterHostCount {
  total: usize,
  healthy: usize,
  degraded: usize,
}

static MOCK_CLUSTER_HOST_COUNT: std::sync::Mutex<Option<MockClusterHostCount>> =
  std::sync::Mutex::new(None);

fn reset_cluster_host_count_mock() {
  *MOCK_CLUSTER_HOST_COUNT.lock().unwrap() = None;
}

fn set_cluster_host_count_mock(count: MockClusterHostCount) {
  *MOCK_CLUSTER_HOST_COUNT.lock().unwrap() = Some(count);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  _cluster_name: abi::envoy_dynamic_module_type_module_buffer,
  _priority: u32,
  total_count: *mut usize,
  healthy_count: *mut usize,
  degraded_count: *mut usize,
) -> bool {
  let guard = MOCK_CLUSTER_HOST_COUNT.lock().unwrap();
  match &*guard {
    Some(count) => {
      if !total_count.is_null() {
        unsafe {
          *total_count = count.total;
        }
      }
      if !healthy_count.is_null() {
        unsafe {
          *healthy_count = count.healthy;
        }
      }
      if !degraded_count.is_null() {
        unsafe {
          *degraded_count = count.degraded;
        }
      }
      true
    },
    None => false,
  }
}

#[test]
fn test_get_cluster_host_count_success() {
  reset_cluster_host_count_mock();
  set_cluster_host_count_mock(MockClusterHostCount {
    total: 10,
    healthy: 8,
    degraded: 1,
  });

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_cluster_host_count("test_cluster", 0);
  assert!(result.is_some());
  let count = result.unwrap();
  assert_eq!(count.total, 10);
  assert_eq!(count.healthy, 8);
  assert_eq!(count.degraded, 1);
}

#[test]
fn test_get_cluster_host_count_not_found() {
  reset_cluster_host_count_mock();

  let filter = EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };

  let result = filter.get_cluster_host_count("nonexistent_cluster", 0);
  assert!(result.is_none());
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
  let filter = network::envoy_dynamic_module_on_network_filter_new_impl(
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

// =============================================================================
// Bootstrap Extension FFI stubs for testing.
// =============================================================================

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
  _ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
  _ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr,
  _event_id: u64,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_http_callout(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _callout_id_out: *mut u64,
  _cluster_name: abi::envoy_dynamic_module_type_module_buffer,
  _headers: *mut abi::envoy_dynamic_module_type_module_http_header,
  _headers_size: usize,
  _body: abi::envoy_dynamic_module_type_module_buffer,
  _timeout_milliseconds: u64,
) -> abi::envoy_dynamic_module_type_http_callout_init_result {
  abi::envoy_dynamic_module_type_http_callout_init_result::CannotCreateRequest
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
  _extension_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _value_ptr: *mut u64,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
  _extension_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _value_ptr: *mut u64,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
  _extension_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _sample_count_ptr: *mut u64,
  _sample_sum_ptr: *mut f64,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
  _extension_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  _iterator_fn: abi::envoy_dynamic_module_type_counter_iterator_fn,
  _user_data: *mut std::os::raw::c_void,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
  _extension_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  _iterator_fn: abi::envoy_dynamic_module_type_gauge_iterator_fn,
  _user_data: *mut std::os::raw::c_void,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _counter_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _gauge_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _histogram_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_timer_new(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
  _timer_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
  _delay_milliseconds: u64,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_timer_disable(
  _timer_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(
  _timer_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_timer_delete(
  _timer_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _path: abi::envoy_dynamic_module_type_module_buffer,
  _events: u32,
) -> bool {
  false
}

// Thread-local used by the test mock to capture the response body set via the callback.
thread_local! {
  static TEST_ADMIN_RESPONSE: std::cell::RefCell<String> =
    const { std::cell::RefCell::new(String::new()) };
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  response_body: abi::envoy_dynamic_module_type_module_buffer,
) {
  if !response_body.ptr.is_null() && response_body.length > 0 {
    let s = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        response_body.ptr as *const u8,
        response_body.length,
      ))
    };
    TEST_ADMIN_RESPONSE.with(|cell| {
      *cell.borrow_mut() = s.to_string();
    });
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _path_prefix: abi::envoy_dynamic_module_type_module_buffer,
  _help_text: abi::envoy_dynamic_module_type_module_buffer,
  _removable: bool,
  _mutates_server_state: bool,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  _path_prefix: abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle(
  _extension_config_envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
) -> bool {
  false
}

// =============================================================================
// Bootstrap Extension Tests
// =============================================================================

#[test]
fn test_bootstrap_extension_config_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }
  impl Drop for TestBootstrapExtensionConfig {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_bootstrap_extension_new_destroy() {
  static DROPPED: AtomicBool = AtomicBool::new(false);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}
  impl Drop for TestBootstrapExtension {
    fn drop(&mut self) {
      DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let config: Box<dyn BootstrapExtensionConfig> = Box::new(TestBootstrapExtensionConfig);
  let mut envoy_extension = bootstrap::EnvoyBootstrapExtensionImpl::new(std::ptr::null_mut());
  let extension_ptr =
    bootstrap::envoy_dynamic_module_on_bootstrap_extension_new_impl(&mut envoy_extension, &*config);
  assert!(!extension_ptr.is_null());

  envoy_dynamic_module_on_bootstrap_extension_destroy(extension_ptr);
  assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_bootstrap_extension_drain_started() {
  static DRAIN_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {
    fn on_drain_started(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
      DRAIN_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  let config: Box<dyn BootstrapExtensionConfig> = Box::new(TestBootstrapExtensionConfig);
  let mut envoy_extension = bootstrap::EnvoyBootstrapExtensionImpl::new(std::ptr::null_mut());
  let extension_ptr =
    bootstrap::envoy_dynamic_module_on_bootstrap_extension_new_impl(&mut envoy_extension, &*config);

  envoy_dynamic_module_on_bootstrap_extension_drain_started(std::ptr::null_mut(), extension_ptr);

  assert!(DRAIN_CALLED.load(std::sync::atomic::Ordering::SeqCst));

  envoy_dynamic_module_on_bootstrap_extension_destroy(extension_ptr);
}

#[test]
fn test_bootstrap_extension_shutdown() {
  static SHUTDOWN_CALLED: AtomicBool = AtomicBool::new(false);
  static COMPLETION_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {
    fn on_shutdown(
      &mut self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
      completion: CompletionCallback,
    ) {
      SHUTDOWN_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
      completion.done();
    }
  }

  unsafe extern "C" fn test_completion(context: *mut std::os::raw::c_void) {
    let flag = &*(context as *const AtomicBool);
    flag.store(true, std::sync::atomic::Ordering::SeqCst);
  }

  let config: Box<dyn BootstrapExtensionConfig> = Box::new(TestBootstrapExtensionConfig);
  let mut envoy_extension = bootstrap::EnvoyBootstrapExtensionImpl::new(std::ptr::null_mut());
  let extension_ptr =
    bootstrap::envoy_dynamic_module_on_bootstrap_extension_new_impl(&mut envoy_extension, &*config);

  envoy_dynamic_module_on_bootstrap_extension_shutdown(
    std::ptr::null_mut(),
    extension_ptr,
    Some(test_completion),
    &COMPLETION_CALLED as *const AtomicBool as *mut std::os::raw::c_void,
  );

  assert!(SHUTDOWN_CALLED.load(std::sync::atomic::Ordering::SeqCst));
  assert!(COMPLETION_CALLED.load(std::sync::atomic::Ordering::SeqCst));

  envoy_dynamic_module_on_bootstrap_extension_destroy(extension_ptr);
}

#[test]
fn test_bootstrap_extension_shutdown_default_calls_completion() {
  // Verify that the default on_shutdown implementation calls the completion callback.
  static COMPLETION_CALLED: AtomicBool = AtomicBool::new(false);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {
    // Use the default on_shutdown implementation.
  }

  unsafe extern "C" fn test_completion(context: *mut std::os::raw::c_void) {
    let flag = &*(context as *const AtomicBool);
    flag.store(true, std::sync::atomic::Ordering::SeqCst);
  }

  let config: Box<dyn BootstrapExtensionConfig> = Box::new(TestBootstrapExtensionConfig);
  let mut envoy_extension = bootstrap::EnvoyBootstrapExtensionImpl::new(std::ptr::null_mut());
  let extension_ptr =
    bootstrap::envoy_dynamic_module_on_bootstrap_extension_new_impl(&mut envoy_extension, &*config);

  envoy_dynamic_module_on_bootstrap_extension_shutdown(
    std::ptr::null_mut(),
    extension_ptr,
    Some(test_completion),
    &COMPLETION_CALLED as *const AtomicBool as *mut std::os::raw::c_void,
  );

  assert!(COMPLETION_CALLED.load(std::sync::atomic::Ordering::SeqCst));

  envoy_dynamic_module_on_bootstrap_extension_destroy(extension_ptr);
}

#[test]
fn test_bootstrap_extension_admin_request() {
  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_admin_request(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      method: &str,
      path: &str,
      _body: &[u8],
    ) -> (u32, String) {
      (200, format!("method={} path={}", method, path))
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let method = "GET";
  let path = "/test_admin?key=val";
  let body = b"";

  let method_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: method.as_ptr() as *mut _,
    length: method.len(),
  };
  let path_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: path.as_ptr() as *mut _,
    length: path.len(),
  };
  let body_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: body.as_ptr() as *mut _,
    length: body.len(),
  };

  // Clear the test mock before calling.
  TEST_ADMIN_RESPONSE.with(|cell| cell.borrow_mut().clear());

  let status = unsafe {
    envoy_dynamic_module_on_bootstrap_extension_admin_request(
      std::ptr::null_mut(),
      config_ptr,
      method_buf,
      path_buf,
      body_buf,
    )
  };

  assert_eq!(status, 200);
  TEST_ADMIN_RESPONSE.with(|cell| {
    assert_eq!(*cell.borrow(), "method=GET path=/test_admin?key=val");
  });

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_admin_request_default() {
  // Verify that the default on_admin_request returns 404 with empty body.
  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let method = "GET";
  let path = "/test";
  let body = b"";

  let method_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: method.as_ptr() as *mut _,
    length: method.len(),
  };
  let path_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: path.as_ptr() as *mut _,
    length: path.len(),
  };
  let body_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: body.as_ptr() as *mut _,
    length: body.len(),
  };

  // Clear the test mock before calling.
  TEST_ADMIN_RESPONSE.with(|cell| cell.borrow_mut().clear());

  let status = unsafe {
    envoy_dynamic_module_on_bootstrap_extension_admin_request(
      std::ptr::null_mut(),
      config_ptr,
      method_buf,
      path_buf,
      body_buf,
    )
  };

  assert_eq!(status, 404);
  TEST_ADMIN_RESPONSE.with(|cell| {
    assert!(cell.borrow().is_empty());
  });

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_timer_fired_identity() {
  // Verify that the timer identity passed to on_timer_fired matches the raw pointer.
  static FIRED_TIMER_ID: AtomicUsize = AtomicUsize::new(0);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_timer_fired(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      timer: &dyn EnvoyBootstrapExtensionTimer,
    ) {
      FIRED_TIMER_ID.store(timer.id(), std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  // Use two different fake pointer values as timer identities.
  let fake_timer_a = 0xAAAA_usize as *mut std::os::raw::c_void;
  let fake_timer_b = 0xBBBB_usize as *mut std::os::raw::c_void;

  // Fire timer A and verify the recorded id matches.
  FIRED_TIMER_ID.store(0, std::sync::atomic::Ordering::SeqCst);
  envoy_dynamic_module_on_bootstrap_extension_timer_fired(
    std::ptr::null_mut(),
    config_ptr,
    fake_timer_a,
  );
  assert_eq!(
    FIRED_TIMER_ID.load(std::sync::atomic::Ordering::SeqCst),
    fake_timer_a as usize
  );

  // Fire timer B and verify the recorded id matches a different value.
  FIRED_TIMER_ID.store(0, std::sync::atomic::Ordering::SeqCst);
  envoy_dynamic_module_on_bootstrap_extension_timer_fired(
    std::ptr::null_mut(),
    config_ptr,
    fake_timer_b,
  );
  assert_eq!(
    FIRED_TIMER_ID.load(std::sync::atomic::Ordering::SeqCst),
    fake_timer_b as usize
  );

  // The two timer ids must be different.
  assert_ne!(fake_timer_a as usize, fake_timer_b as usize);

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_file_changed() {
  // Verify that path and events are correctly passed through on_file_changed.
  static FIRED_PATH: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
  static FIRED_EVENTS: AtomicU32 = AtomicU32::new(0);

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_file_changed(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      path: &str,
      events: u32,
    ) {
      *FIRED_PATH.lock().unwrap() = path.to_string();
      FIRED_EVENTS.store(events, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  // Fire with MovedTo event.
  let path = "test/path_a.txt";
  let path_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: path.as_ptr() as *mut _,
    length: path.len(),
  };
  envoy_dynamic_module_on_bootstrap_extension_file_changed(
    std::ptr::null_mut(),
    config_ptr,
    path_buf,
    FILE_WATCHER_EVENT_MOVED_TO,
  );
  assert_eq!(FIRED_PATH.lock().unwrap().as_str(), "test/path_a.txt");
  assert_eq!(
    FIRED_EVENTS.load(std::sync::atomic::Ordering::SeqCst),
    FILE_WATCHER_EVENT_MOVED_TO
  );

  // Fire with Modified event on different path.
  let path2 = "test/path_b.txt";
  let path_buf2 = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: path2.as_ptr() as *mut _,
    length: path2.len(),
  };
  envoy_dynamic_module_on_bootstrap_extension_file_changed(
    std::ptr::null_mut(),
    config_ptr,
    path_buf2,
    FILE_WATCHER_EVENT_MODIFIED,
  );
  assert_eq!(FIRED_PATH.lock().unwrap().as_str(), "test/path_b.txt");
  assert_eq!(
    FIRED_EVENTS.load(std::sync::atomic::Ordering::SeqCst),
    FILE_WATCHER_EVENT_MODIFIED
  );

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

// =============================================================================
// Cert Validator callback stubs.
// =============================================================================

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cert_validator_set_error_details(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
  _error_details: abi::envoy_dynamic_module_type_module_buffer,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cert_validator_set_filter_state(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _value: abi::envoy_dynamic_module_type_module_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cert_validator_get_filter_state(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _value_out: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

// =============================================================================
// Cert Validator tests.
// =============================================================================

#[test]
fn test_cert_validator_config_new_and_destroy() {
  struct TestCertValidatorConfig;
  impl cert_validator::CertValidatorConfig for TestCertValidatorConfig {
    fn do_verify_cert_chain(
      &self,
      _envoy_cert_validator: &cert_validator::EnvoyCertValidator,
      _certs: &[&[u8]],
      _host_name: &str,
      _is_server: bool,
    ) -> cert_validator::ValidationResult {
      cert_validator::ValidationResult::successful()
    }
    fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
      0x03
    }
    fn update_digest(&self) -> &[u8] {
      b"test"
    }
  }

  let new_config_fn: NewCertValidatorConfigFunction =
    |_name: &str, _config: &[u8]| -> Option<Box<dyn cert_validator::CertValidatorConfig>> {
      Some(Box::new(TestCertValidatorConfig))
    };

  let config_ptr = cert_validator::init_cert_validator_config("test", b"config", &new_config_fn);
  assert!(!config_ptr.is_null());

  unsafe {
    envoy_dynamic_module_on_cert_validator_config_destroy(config_ptr);
  }
}

#[test]
fn test_cert_validator_do_verify_cert_chain_successful() {
  struct TestCertValidatorConfig;
  impl cert_validator::CertValidatorConfig for TestCertValidatorConfig {
    fn do_verify_cert_chain(
      &self,
      _envoy_cert_validator: &cert_validator::EnvoyCertValidator,
      certs: &[&[u8]],
      host_name: &str,
      _is_server: bool,
    ) -> cert_validator::ValidationResult {
      assert_eq!(certs.len(), 1);
      assert_eq!(certs[0], b"cert_data");
      assert_eq!(host_name, "example.com");
      cert_validator::ValidationResult::successful()
    }
    fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
      0x03
    }
    fn update_digest(&self) -> &[u8] {
      b"test"
    }
  }

  let config: Box<dyn cert_validator::CertValidatorConfig> = Box::new(TestCertValidatorConfig);
  let config_ptr = Box::into_raw(Box::new(config)) as *const ::std::os::raw::c_void;

  let cert_data = b"cert_data";
  let mut cert_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: cert_data.as_ptr() as *const _,
    length: cert_data.len(),
  };
  let host_name = "example.com";
  let host_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: host_name.as_ptr() as *const _,
    length: host_name.len(),
  };

  let result = unsafe {
    envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
      std::ptr::null_mut(),
      config_ptr,
      &mut cert_buf as *mut _,
      1,
      host_name_buf,
      false,
    )
  };
  assert_eq!(
    result.status,
    abi::envoy_dynamic_module_type_cert_validator_validation_status::Successful
  );
  assert_eq!(
    result.detailed_status,
    abi::envoy_dynamic_module_type_cert_validator_client_validation_status::Validated
  );
  assert!(!result.has_tls_alert);

  unsafe {
    envoy_dynamic_module_on_cert_validator_config_destroy(config_ptr);
  }
}

#[test]
fn test_cert_validator_do_verify_cert_chain_failed() {
  struct TestCertValidatorConfig;
  impl cert_validator::CertValidatorConfig for TestCertValidatorConfig {
    fn do_verify_cert_chain(
      &self,
      _envoy_cert_validator: &cert_validator::EnvoyCertValidator,
      _certs: &[&[u8]],
      _host_name: &str,
      _is_server: bool,
    ) -> cert_validator::ValidationResult {
      cert_validator::ValidationResult::failed(
        cert_validator::ClientValidationStatus::Failed,
        Some(42),
        Some("test error".to_string()),
      )
    }
    fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
      0x03
    }
    fn update_digest(&self) -> &[u8] {
      b"test"
    }
  }

  let config: Box<dyn cert_validator::CertValidatorConfig> = Box::new(TestCertValidatorConfig);
  let config_ptr = Box::into_raw(Box::new(config)) as *const ::std::os::raw::c_void;

  let cert_data = b"cert_data";
  let mut cert_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: cert_data.as_ptr() as *const _,
    length: cert_data.len(),
  };
  let host_name = "example.com";
  let host_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: host_name.as_ptr() as *const _,
    length: host_name.len(),
  };

  let result = unsafe {
    envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
      std::ptr::null_mut(),
      config_ptr,
      &mut cert_buf as *mut _,
      1,
      host_name_buf,
      false,
    )
  };
  assert_eq!(
    result.status,
    abi::envoy_dynamic_module_type_cert_validator_validation_status::Failed
  );
  assert_eq!(
    result.detailed_status,
    abi::envoy_dynamic_module_type_cert_validator_client_validation_status::Failed
  );
  assert!(result.has_tls_alert);
  assert_eq!(result.tls_alert, 42);

  unsafe {
    envoy_dynamic_module_on_cert_validator_config_destroy(config_ptr);
  }
}

#[test]
fn test_cert_validator_filter_state_methods() {
  // Test that EnvoyCertValidator filter state methods call the ABI functions correctly.
  // In unit tests, the ABI functions are weak stubs that return false, so we verify
  // the methods handle the failure case gracefully.
  let envoy_validator = cert_validator::EnvoyCertValidator::new(std::ptr::null_mut());

  // set_filter_state should return false because the weak stub returns false.
  let result = envoy_validator.set_filter_state(b"key", b"value");
  assert!(!result);

  // get_filter_state should return None because the weak stub returns false.
  let result = envoy_validator.get_filter_state(b"key");
  assert!(result.is_none());
}

#[test]
fn test_cert_validator_get_ssl_verify_mode() {
  struct TestCertValidatorConfig;
  impl cert_validator::CertValidatorConfig for TestCertValidatorConfig {
    fn do_verify_cert_chain(
      &self,
      _envoy_cert_validator: &cert_validator::EnvoyCertValidator,
      _certs: &[&[u8]],
      _host_name: &str,
      _is_server: bool,
    ) -> cert_validator::ValidationResult {
      cert_validator::ValidationResult::successful()
    }
    fn get_ssl_verify_mode(&self, handshaker_provides_certificates: bool) -> i32 {
      if handshaker_provides_certificates {
        0x01
      } else {
        0x03
      }
    }
    fn update_digest(&self) -> &[u8] {
      b"test"
    }
  }

  let config: Box<dyn cert_validator::CertValidatorConfig> = Box::new(TestCertValidatorConfig);
  let config_ptr = Box::into_raw(Box::new(config)) as *const ::std::os::raw::c_void;

  let result =
    unsafe { envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(config_ptr, false) };
  assert_eq!(result, 0x03);

  let result =
    unsafe { envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(config_ptr, true) };
  assert_eq!(result, 0x01);

  unsafe {
    envoy_dynamic_module_on_cert_validator_config_destroy(config_ptr);
  }
}

#[test]
fn test_cert_validator_update_digest() {
  struct TestCertValidatorConfig;
  impl cert_validator::CertValidatorConfig for TestCertValidatorConfig {
    fn do_verify_cert_chain(
      &self,
      _envoy_cert_validator: &cert_validator::EnvoyCertValidator,
      _certs: &[&[u8]],
      _host_name: &str,
      _is_server: bool,
    ) -> cert_validator::ValidationResult {
      cert_validator::ValidationResult::successful()
    }
    fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
      0x03
    }
    fn update_digest(&self) -> &[u8] {
      b"my_digest_data"
    }
  }

  let config: Box<dyn cert_validator::CertValidatorConfig> = Box::new(TestCertValidatorConfig);
  let config_ptr = Box::into_raw(Box::new(config)) as *const ::std::os::raw::c_void;

  let mut out_data = abi::envoy_dynamic_module_type_module_buffer {
    ptr: std::ptr::null(),
    length: 0,
  };
  unsafe {
    envoy_dynamic_module_on_cert_validator_update_digest(config_ptr, &mut out_data);
  }
  assert!(!out_data.ptr.is_null());
  assert_eq!(out_data.length, 14);
  let digest = unsafe { std::slice::from_raw_parts(out_data.ptr as *const u8, out_data.length) };
  assert_eq!(digest, b"my_digest_data");

  unsafe {
    envoy_dynamic_module_on_cert_validator_config_destroy(config_ptr);
  }
}

// =============================================================================
// Load Balancer Metrics Tests
// =============================================================================

#[test]
fn test_lb_config_define_counter() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_counter()
    .with(mockall::predicate::eq("test_counter"))
    .returning(|_| Ok(EnvoyCounterId(1)));
  let result = mock_config.define_counter("test_counter");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyCounterId(1));
}

#[test]
fn test_lb_config_define_gauge() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_gauge()
    .with(mockall::predicate::eq("test_gauge"))
    .returning(|_| Ok(EnvoyGaugeId(1)));
  let result = mock_config.define_gauge("test_gauge");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyGaugeId(1));
}

#[test]
fn test_lb_config_define_histogram() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_histogram()
    .with(mockall::predicate::eq("test_histogram"))
    .returning(|_| Ok(EnvoyHistogramId(1)));
  let result = mock_config.define_histogram("test_histogram");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyHistogramId(1));
}

#[test]
fn test_lb_config_increment_counter() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_increment_counter()
    .with(
      mockall::predicate::eq(EnvoyCounterId(1)),
      mockall::predicate::eq(5u64),
    )
    .returning(|_, _| Ok(()));
  let result = mock_config.increment_counter(EnvoyCounterId(1), 5);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_increment_counter_invalid_id() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_increment_counter()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  let result = mock_config.increment_counter(EnvoyCounterId(999), 1);
  assert!(result.is_err());
}

#[test]
fn test_lb_config_gauge_operations() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_set_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(100u64),
    )
    .returning(|_, _| Ok(()));
  mock_config
    .expect_increase_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(10u64),
    )
    .returning(|_, _| Ok(()));
  mock_config
    .expect_decrease_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(5u64),
    )
    .returning(|_, _| Ok(()));

  assert!(mock_config.set_gauge(EnvoyGaugeId(1), 100).is_ok());
  assert!(mock_config.increase_gauge(EnvoyGaugeId(1), 10).is_ok());
  assert!(mock_config.decrease_gauge(EnvoyGaugeId(1), 5).is_ok());
}

#[test]
fn test_lb_config_gauge_invalid_id() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_set_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock_config
    .expect_increase_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock_config
    .expect_decrease_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));

  assert!(mock_config.set_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(mock_config.increase_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(mock_config.decrease_gauge(EnvoyGaugeId(999), 1).is_err());
}

#[test]
fn test_lb_config_record_histogram_value() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_record_histogram_value()
    .with(
      mockall::predicate::eq(EnvoyHistogramId(1)),
      mockall::predicate::eq(42u64),
    )
    .returning(|_, _| Ok(()));
  let result = mock_config.record_histogram_value(EnvoyHistogramId(1), 42);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_record_histogram_value_invalid_id() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_record_histogram_value()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  let result = mock_config.record_histogram_value(EnvoyHistogramId(999), 1);
  assert!(result.is_err());
}

#[test]
fn test_lb_config_define_all_metric_types_and_use() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_counter()
    .returning(|_| Ok(EnvoyCounterId(1)));
  mock_config
    .expect_define_gauge()
    .returning(|_| Ok(EnvoyGaugeId(1)));
  mock_config
    .expect_define_histogram()
    .returning(|_| Ok(EnvoyHistogramId(1)));
  mock_config
    .expect_increment_counter()
    .returning(|_, _| Ok(()));
  mock_config.expect_set_gauge().returning(|_, _| Ok(()));
  mock_config
    .expect_record_histogram_value()
    .returning(|_, _| Ok(()));

  let counter_id = mock_config.define_counter("my_counter").unwrap();
  let gauge_id = mock_config.define_gauge("my_gauge").unwrap();
  let histogram_id = mock_config.define_histogram("my_histogram").unwrap();

  assert!(mock_config.increment_counter(counter_id, 1).is_ok());
  assert!(mock_config.set_gauge(gauge_id, 42).is_ok());
  assert!(mock_config
    .record_histogram_value(histogram_id, 100)
    .is_ok());
}

#[test]
fn test_lb_config_define_counter_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_counter_vec()
    .returning(|_, _| Ok(EnvoyCounterVecId(1)));
  let result = mock_config.define_counter_vec("requests_total", &["method", "status"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyCounterVecId(1));
}

#[test]
fn test_lb_config_define_gauge_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_gauge_vec()
    .returning(|_, _| Ok(EnvoyGaugeVecId(1)));
  let result = mock_config.define_gauge_vec("connections", &["backend"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyGaugeVecId(1));
}

#[test]
fn test_lb_config_define_histogram_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_define_histogram_vec()
    .returning(|_, _| Ok(EnvoyHistogramVecId(1)));
  let result = mock_config.define_histogram_vec("latency", &["endpoint", "method"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyHistogramVecId(1));
}

#[test]
fn test_lb_config_increment_counter_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_increment_counter_vec()
    .returning(|_, _, _| Ok(()));
  let result = mock_config.increment_counter_vec(EnvoyCounterVecId(1), &["GET", "200"], 1);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_set_gauge_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_set_gauge_vec()
    .returning(|_, _, _| Ok(()));
  let result = mock_config.set_gauge_vec(EnvoyGaugeVecId(1), &["backend1"], 42);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_increase_gauge_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_increase_gauge_vec()
    .returning(|_, _, _| Ok(()));
  let result = mock_config.increase_gauge_vec(EnvoyGaugeVecId(1), &["backend1"], 5);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_decrease_gauge_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_decrease_gauge_vec()
    .returning(|_, _, _| Ok(()));
  let result = mock_config.decrease_gauge_vec(EnvoyGaugeVecId(1), &["backend1"], 3);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_record_histogram_value_vec() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_record_histogram_value_vec()
    .returning(|_, _, _| Ok(()));
  let result =
    mock_config.record_histogram_value_vec(EnvoyHistogramVecId(1), &["endpoint1", "GET"], 150);
  assert!(result.is_ok());
}

#[test]
fn test_lb_config_vec_metric_invalid_id() {
  let mut mock_config = load_balancer::MockEnvoyLbConfig::new();
  mock_config
    .expect_increment_counter_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock_config
    .expect_set_gauge_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock_config
    .expect_record_histogram_value_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));

  assert!(mock_config
    .increment_counter_vec(EnvoyCounterVecId(999), &["v1"], 1)
    .is_err());
  assert!(mock_config
    .set_gauge_vec(EnvoyGaugeVecId(999), &["v1"], 1)
    .is_err());
  assert!(mock_config
    .record_histogram_value_vec(EnvoyHistogramVecId(999), &["v1"], 1)
    .is_err());
}

// =============================================================================
// CatchUnwind Tests
// =============================================================================

static SEND_RESPONSE_STATUS_CODE: AtomicU32 = AtomicU32::new(0);

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_http_send_response(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  status_code: u32,
  _headers_vector: *mut abi::envoy_dynamic_module_type_module_http_header,
  _headers_vector_size: usize,
  _body: abi::envoy_dynamic_module_type_module_buffer,
  _details: abi::envoy_dynamic_module_type_module_buffer,
) {
  SEND_RESPONSE_STATUS_CODE.store(status_code, std::sync::atomic::Ordering::SeqCst);
}

static RESET_STREAM_CALLED: AtomicBool = AtomicBool::new(false);

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_http_filter_reset_stream(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  _reason: abi::envoy_dynamic_module_type_http_filter_stream_reset_reason,
  _details: abi::envoy_dynamic_module_type_module_buffer,
) {
  RESET_STREAM_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
}

static NETWORK_CLOSE_CALLED: AtomicBool = AtomicBool::new(false);

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_network_filter_close(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_network_filter_envoy_ptr,
  _close_type: abi::envoy_dynamic_module_type_network_connection_close_type,
) {
  NETWORK_CLOSE_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
}

static LISTENER_CLOSE_SOCKET_CALLED: AtomicBool = AtomicBool::new(false);

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_listener_filter_close_socket(
  _filter_envoy_ptr: abi::envoy_dynamic_module_type_listener_filter_envoy_ptr,
  _details: abi::envoy_dynamic_module_type_module_buffer,
) {
  LISTENER_CLOSE_SOCKET_CALLED.store(true, std::sync::atomic::Ordering::SeqCst);
}

#[test]
fn test_catch_unwind_http_filter_panic() {
  struct PanicFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PanicFilter {
    fn on_request_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
      panic!("intentional panic in on_request_headers");
    }
  }

  SEND_RESPONSE_STATUS_CODE.store(0, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = http::EnvoyHttpFilterImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = HttpFilter::on_request_headers(&mut wrapper, &mut envoy_filter, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration,
  );
  assert_eq!(
    SEND_RESPONSE_STATUS_CODE.load(std::sync::atomic::Ordering::SeqCst),
    500,
  );
}

#[test]
fn test_catch_unwind_network_filter_panic() {
  struct PanicFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for PanicFilter {
    fn on_read(
      &mut self,
      _envoy_filter: &mut ENF,
      _data_length: usize,
      _end_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
      panic!("intentional panic in on_read");
    }
  }

  NETWORK_CLOSE_CALLED.store(false, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = network::EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = NetworkFilter::on_read(&mut wrapper, &mut envoy_filter, 0, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration,
  );
  assert!(NETWORK_CLOSE_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_catch_unwind_listener_filter_panic() {
  struct PanicFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for PanicFilter {
    fn on_accept(
      &mut self,
      _envoy_filter: &mut ELF,
    ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
      panic!("intentional panic in on_accept");
    }
  }

  LISTENER_CLOSE_SOCKET_CALLED.store(false, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = listener::EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = ListenerFilter::on_accept(&mut wrapper, &mut envoy_filter);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration,
  );
  assert!(LISTENER_CLOSE_SOCKET_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_catch_unwind_http_response_headers_panic() {
  struct PanicFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PanicFilter {
    fn on_response_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
      panic!("intentional panic in on_response_headers");
    }
  }

  RESET_STREAM_CALLED.store(false, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = http::EnvoyHttpFilterImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = HttpFilter::on_response_headers(&mut wrapper, &mut envoy_filter, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration,
  );
  assert!(RESET_STREAM_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_catch_unwind_network_on_write_panic() {
  struct PanicFilter;
  impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for PanicFilter {
    fn on_write(
      &mut self,
      _envoy_filter: &mut ENF,
      _data_length: usize,
      _end_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
      panic!("intentional panic in on_write");
    }
  }

  NETWORK_CLOSE_CALLED.store(false, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = network::EnvoyNetworkFilterImpl {
    raw: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = NetworkFilter::on_write(&mut wrapper, &mut envoy_filter, 0, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration,
  );
  assert!(NETWORK_CLOSE_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_catch_unwind_listener_on_data_panic() {
  struct PanicFilter;
  impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for PanicFilter {
    fn on_data(
      &mut self,
      _envoy_filter: &mut ELF,
    ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
      panic!("intentional panic in on_data");
    }
  }

  LISTENER_CLOSE_SOCKET_CALLED.store(false, std::sync::atomic::Ordering::SeqCst);

  let mut envoy_filter = listener::EnvoyListenerFilterImpl {
    raw: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = ListenerFilter::on_data(&mut wrapper, &mut envoy_filter);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration,
  );
  assert!(LISTENER_CLOSE_SOCKET_CALLED.load(std::sync::atomic::Ordering::SeqCst));
}

#[test]
fn test_catch_unwind_http_callout_done_after_poison_is_skipped() {
  struct PanicFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PanicFilter {
    fn on_request_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
      panic!("intentional panic in on_request_headers");
    }
  }

  let mut envoy_filter = http::EnvoyHttpFilterImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = HttpFilter::on_request_headers(&mut wrapper, &mut envoy_filter, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration,
  );

  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    HttpFilter::on_http_callout_done(
      &mut wrapper,
      &mut envoy_filter,
      1,
      abi::envoy_dynamic_module_type_http_callout_result::Success,
      None,
      None,
    );
  }));
  assert!(
    result.is_ok(),
    "late on_http_callout_done should be skipped after CatchUnwind is poisoned",
  );
}

#[test]
fn test_catch_unwind_http_scheduled_after_poison_is_skipped() {
  struct PanicFilter;
  impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for PanicFilter {
    fn on_request_headers(
      &mut self,
      _envoy_filter: &mut EHF,
      _end_of_stream: bool,
    ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
      panic!("intentional panic in on_request_headers");
    }
  }

  let mut envoy_filter = http::EnvoyHttpFilterImpl {
    raw_ptr: std::ptr::null_mut(),
  };
  let mut wrapper = CatchUnwind::new(PanicFilter);

  let status = HttpFilter::on_request_headers(&mut wrapper, &mut envoy_filter, false);
  assert_eq!(
    status,
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration,
  );

  let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
    HttpFilter::on_scheduled(&mut wrapper, &mut envoy_filter, 1);
  }));
  assert!(
    result.is_ok(),
    "late on_scheduled should be skipped after CatchUnwind is poisoned",
  );
}

// =============================================================================
// Cluster Extension FFI stubs for testing.
// =============================================================================

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_add_hosts(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  _priority: u32,
  _addresses: *const abi::envoy_dynamic_module_type_module_buffer,
  _weights: *const u32,
  _regions: *const abi::envoy_dynamic_module_type_module_buffer,
  _zones: *const abi::envoy_dynamic_module_type_module_buffer,
  _sub_zones: *const abi::envoy_dynamic_module_type_module_buffer,
  _metadata_pairs: *const abi::envoy_dynamic_module_type_module_buffer,
  _metadata_pairs_per_host: usize,
  _count: usize,
  _result_host_ptrs: *mut abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_remove_hosts(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  _host_envoy_ptrs: *const abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
  _count: usize,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_pre_init_complete(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_update_host_health(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  _host_envoy_ptr: abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
  _health_status: abi::envoy_dynamic_module_type_host_health,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_find_host_by_address(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  _address: abi::envoy_dynamic_module_type_module_buffer,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_find_host_by_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _address: abi::envoy_dynamic_module_type_module_buffer,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _index: usize,
  _is_added: bool,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _host: abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
  _details: *const std::ffi::c_char,
  _details_length: usize,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_cluster_name(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_hosts_count(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> u32 {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_health(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> abi::envoy_dynamic_module_type_host_health {
  abi::envoy_dynamic_module_type_host_health::Unhealthy
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _address: abi::envoy_dynamic_module_type_module_buffer,
  _result: *mut abi::envoy_dynamic_module_type_host_health,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_weight(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> u32 {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_stat(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _stat: abi::envoy_dynamic_module_type_host_stat,
) -> u64 {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_locality(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _region: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  _zone: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  _sub_zone: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_set_host_data(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _data: usize,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_data(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _data: *mut usize,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _filter_name: abi::envoy_dynamic_module_type_module_buffer,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _filter_name: abi::envoy_dynamic_module_type_module_buffer,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _result: *mut f64,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _index: usize,
  _filter_name: abi::envoy_dynamic_module_type_module_buffer,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _result: *mut bool,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_locality_count(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _locality_index: usize,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _locality_index: usize,
  _host_index: usize,
  _result: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _priority: u32,
  _locality_index: usize,
) -> u32 {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_scheduler_new(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_scheduler_module_ptr {
  std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_scheduler_delete(
  _scheduler_module_ptr: abi::envoy_dynamic_module_type_cluster_scheduler_module_ptr,
) {
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_scheduler_commit(
  _scheduler_module_ptr: abi::envoy_dynamic_module_type_cluster_scheduler_module_ptr,
  _event_id: u64,
) {
}

// Cluster config metrics FFI stubs for testing.

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_define_counter(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _counter_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_increment_counter(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_define_gauge(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _gauge_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_set_gauge(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_increment_gauge(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_decrement_gauge(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_define_histogram(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _name: abi::envoy_dynamic_module_type_module_buffer,
  _label_names: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_names_length: usize,
  _histogram_id_ptr: *mut usize,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_config_record_histogram_value(
  _cluster_config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  _id: usize,
  _label_values: *mut abi::envoy_dynamic_module_type_module_buffer,
  _label_values_length: usize,
  _value: u64,
) -> abi::envoy_dynamic_module_type_metrics_result {
  abi::envoy_dynamic_module_type_metrics_result::Success
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _hash_out: *mut u64,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
) -> usize {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _result_headers: *mut abi::envoy_dynamic_module_type_envoy_http_header,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _key: abi::envoy_dynamic_module_type_module_buffer,
  _result_buffer: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  _index: usize,
  _optional_size: *mut usize,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
) -> u32 {
  0
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
  _lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _priority: u32,
  _index: usize,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _address: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  _strict: *mut bool,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
  _context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  _result_buffer: *mut abi::envoy_dynamic_module_type_envoy_buffer,
) -> bool {
  false
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_callback_cluster_http_callout(
  _cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  _callout_id_out: *mut u64,
  _cluster_name: abi::envoy_dynamic_module_type_module_buffer,
  _headers: *mut abi::envoy_dynamic_module_type_module_http_header,
  _headers_size: usize,
  _body: abi::envoy_dynamic_module_type_module_buffer,
  _timeout_milliseconds: u64,
) -> abi::envoy_dynamic_module_type_http_callout_init_result {
  abi::envoy_dynamic_module_type_http_callout_init_result::CannotCreateRequest
}

// =============================================================================
// Cluster Extension Rust SDK tests.
// =============================================================================

#[test]
fn test_cluster_scheduler_mock() {
  let mut mock_scheduler = cluster::MockEnvoyClusterScheduler::new();
  mock_scheduler
    .expect_commit()
    .with(mockall::predicate::eq(42u64))
    .times(1)
    .return_const(());
  mock_scheduler.commit(42);
}

#[test]
fn test_cluster_mock_envoy_cluster_new_scheduler() {
  let mut mock_cluster = cluster::MockEnvoyCluster::new();
  mock_cluster.expect_new_scheduler().times(1).returning(|| {
    let mut mock_scheduler = cluster::MockEnvoyClusterScheduler::new();
    mock_scheduler.expect_commit().return_const(());
    Box::new(mock_scheduler)
  });
  let scheduler = mock_cluster.new_scheduler();
  scheduler.commit(100);
}

// =============================================================================
// Cluster Metrics Tests
// =============================================================================

#[test]
fn test_cluster_metrics_define_counter() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_counter()
    .with(mockall::predicate::eq("test_counter"))
    .returning(|_| Ok(EnvoyCounterId(1)));
  let result = mock.define_counter("test_counter");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyCounterId(1));
}

#[test]
fn test_cluster_metrics_define_gauge() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_gauge()
    .with(mockall::predicate::eq("test_gauge"))
    .returning(|_| Ok(EnvoyGaugeId(1)));
  let result = mock.define_gauge("test_gauge");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyGaugeId(1));
}

#[test]
fn test_cluster_metrics_define_histogram() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_histogram()
    .with(mockall::predicate::eq("test_histogram"))
    .returning(|_| Ok(EnvoyHistogramId(1)));
  let result = mock.define_histogram("test_histogram");
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyHistogramId(1));
}

#[test]
fn test_cluster_metrics_increment_counter() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_increment_counter()
    .with(
      mockall::predicate::eq(EnvoyCounterId(1)),
      mockall::predicate::eq(5u64),
    )
    .returning(|_, _| Ok(()));
  assert!(mock.increment_counter(EnvoyCounterId(1), 5).is_ok());
}

#[test]
fn test_cluster_metrics_increment_counter_invalid_id() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_increment_counter()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  assert!(mock.increment_counter(EnvoyCounterId(999), 1).is_err());
}

#[test]
fn test_cluster_metrics_gauge_operations() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_set_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(42u64),
    )
    .returning(|_, _| Ok(()));
  mock
    .expect_increase_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(10u64),
    )
    .returning(|_, _| Ok(()));
  mock
    .expect_decrease_gauge()
    .with(
      mockall::predicate::eq(EnvoyGaugeId(1)),
      mockall::predicate::eq(5u64),
    )
    .returning(|_, _| Ok(()));
  assert!(mock.set_gauge(EnvoyGaugeId(1), 42).is_ok());
  assert!(mock.increase_gauge(EnvoyGaugeId(1), 10).is_ok());
  assert!(mock.decrease_gauge(EnvoyGaugeId(1), 5).is_ok());
}

#[test]
fn test_cluster_metrics_gauge_invalid_id() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_set_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock
    .expect_increase_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock
    .expect_decrease_gauge()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  assert!(mock.set_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(mock.increase_gauge(EnvoyGaugeId(999), 1).is_err());
  assert!(mock.decrease_gauge(EnvoyGaugeId(999), 1).is_err());
}

#[test]
fn test_cluster_metrics_record_histogram_value() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_record_histogram_value()
    .with(
      mockall::predicate::eq(EnvoyHistogramId(1)),
      mockall::predicate::eq(42u64),
    )
    .returning(|_, _| Ok(()));
  assert!(mock.record_histogram_value(EnvoyHistogramId(1), 42).is_ok());
}

#[test]
fn test_cluster_metrics_record_histogram_value_invalid_id() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_record_histogram_value()
    .returning(|_, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  assert!(mock
    .record_histogram_value(EnvoyHistogramId(999), 1)
    .is_err());
}

#[test]
fn test_cluster_metrics_define_all_metric_types_and_use() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_counter()
    .returning(|_| Ok(EnvoyCounterId(1)));
  mock
    .expect_define_gauge()
    .returning(|_| Ok(EnvoyGaugeId(1)));
  mock
    .expect_define_histogram()
    .returning(|_| Ok(EnvoyHistogramId(1)));
  mock.expect_increment_counter().returning(|_, _| Ok(()));
  mock.expect_set_gauge().returning(|_, _| Ok(()));
  mock
    .expect_record_histogram_value()
    .returning(|_, _| Ok(()));

  let counter_id = mock.define_counter("c").unwrap();
  let gauge_id = mock.define_gauge("g").unwrap();
  let histogram_id = mock.define_histogram("h").unwrap();
  assert!(mock.increment_counter(counter_id, 1).is_ok());
  assert!(mock.set_gauge(gauge_id, 100).is_ok());
  assert!(mock.record_histogram_value(histogram_id, 50).is_ok());
}

#[test]
fn test_cluster_metrics_define_counter_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_counter_vec()
    .returning(|_, _| Ok(EnvoyCounterVecId(1)));
  let result = mock.define_counter_vec("test_counter", &["region", "zone"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyCounterVecId(1));
}

#[test]
fn test_cluster_metrics_define_gauge_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_gauge_vec()
    .returning(|_, _| Ok(EnvoyGaugeVecId(1)));
  let result = mock.define_gauge_vec("test_gauge", &["env"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyGaugeVecId(1));
}

#[test]
fn test_cluster_metrics_define_histogram_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_define_histogram_vec()
    .returning(|_, _| Ok(EnvoyHistogramVecId(1)));
  let result = mock.define_histogram_vec("test_histogram", &["method"]);
  assert!(result.is_ok());
  assert_eq!(result.unwrap(), EnvoyHistogramVecId(1));
}

#[test]
fn test_cluster_metrics_increment_counter_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_increment_counter_vec()
    .returning(|_, _, _| Ok(()));
  assert!(mock
    .increment_counter_vec(EnvoyCounterVecId(1), &["us-east-1"], 1)
    .is_ok());
}

#[test]
fn test_cluster_metrics_set_gauge_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock.expect_set_gauge_vec().returning(|_, _, _| Ok(()));
  assert!(mock
    .set_gauge_vec(EnvoyGaugeVecId(1), &["prod"], 42)
    .is_ok());
}

#[test]
fn test_cluster_metrics_increase_gauge_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock.expect_increase_gauge_vec().returning(|_, _, _| Ok(()));
  assert!(mock
    .increase_gauge_vec(EnvoyGaugeVecId(1), &["prod"], 10)
    .is_ok());
}

#[test]
fn test_cluster_metrics_decrease_gauge_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock.expect_decrease_gauge_vec().returning(|_, _, _| Ok(()));
  assert!(mock
    .decrease_gauge_vec(EnvoyGaugeVecId(1), &["prod"], 5)
    .is_ok());
}

#[test]
fn test_cluster_metrics_record_histogram_value_vec() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_record_histogram_value_vec()
    .returning(|_, _, _| Ok(()));
  assert!(mock
    .record_histogram_value_vec(EnvoyHistogramVecId(1), &["GET"], 100)
    .is_ok());
}

#[test]
fn test_cluster_metrics_vec_metric_invalid_id() {
  let mut mock = cluster::MockEnvoyClusterMetrics::new();
  mock
    .expect_increment_counter_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock
    .expect_set_gauge_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  mock
    .expect_record_histogram_value_vec()
    .returning(|_, _, _| Err(abi::envoy_dynamic_module_type_metrics_result::MetricNotFound));
  assert!(mock
    .increment_counter_vec(EnvoyCounterVecId(999), &["v1"], 1)
    .is_err());
  assert!(mock
    .set_gauge_vec(EnvoyGaugeVecId(999), &["v1"], 1)
    .is_err());
  assert!(mock
    .record_histogram_value_vec(EnvoyHistogramVecId(999), &["v1"], 1)
    .is_err());
}

// =================================================================================================
// ClusterLbContext tests
// =================================================================================================

#[test]
fn test_cluster_lb_context_compute_hash_key() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx.expect_compute_hash_key().returning(|| Some(42));
  assert_eq!(mock_ctx.compute_hash_key(), Some(42));
}

#[test]
fn test_cluster_lb_context_compute_hash_key_none() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx.expect_compute_hash_key().returning(|| None);
  assert_eq!(mock_ctx.compute_hash_key(), None);
}

#[test]
fn test_cluster_lb_context_get_downstream_headers_size() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_headers_size()
    .returning(|| 3);
  assert_eq!(mock_ctx.get_downstream_headers_size(), 3);
}

#[test]
fn test_cluster_lb_context_get_downstream_headers() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx.expect_get_downstream_headers().returning(|| {
    Some(vec![
      (":method".to_string(), "GET".to_string()),
      ("host".to_string(), "example.com".to_string()),
    ])
  });
  let headers = mock_ctx.get_downstream_headers().unwrap();
  assert_eq!(headers.len(), 2);
  assert_eq!(headers[0], (":method".to_string(), "GET".to_string()));
  assert_eq!(headers[1], ("host".to_string(), "example.com".to_string()));
}

#[test]
fn test_cluster_lb_context_get_downstream_headers_none() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx.expect_get_downstream_headers().returning(|| None);
  assert!(mock_ctx.get_downstream_headers().is_none());
}

#[test]
fn test_cluster_lb_context_get_downstream_header() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_header()
    .withf(|key, index| key == "host" && *index == 0)
    .returning(|_, _| Some(("example.com".to_string(), 1)));
  let result = mock_ctx.get_downstream_header("host", 0).unwrap();
  assert_eq!(result.0, "example.com");
  assert_eq!(result.1, 1);
}

#[test]
fn test_cluster_lb_context_get_downstream_header_not_found() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_header()
    .returning(|_, _| None);
  assert!(mock_ctx.get_downstream_header("missing", 0).is_none());
}

#[test]
fn test_cluster_lb_context_get_host_selection_retry_count() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_host_selection_retry_count()
    .returning(|| 5);
  assert_eq!(mock_ctx.get_host_selection_retry_count(), 5);
}

#[test]
fn test_cluster_lb_context_should_select_another_host() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_should_select_another_host()
    .withf(|priority, index| *priority == 0 && *index == 1)
    .returning(|_, _| true);
  assert!(mock_ctx.should_select_another_host(0, 1));
}

#[test]
fn test_cluster_lb_context_should_select_another_host_false() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_should_select_another_host()
    .returning(|_, _| false);
  assert!(!mock_ctx.should_select_another_host(0, 0));
}

#[test]
fn test_cluster_lb_context_get_override_host() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_override_host()
    .returning(|| Some(("10.0.0.1:8080".to_string(), true)));
  let result = mock_ctx.get_override_host().unwrap();
  assert_eq!(result.0, "10.0.0.1:8080");
  assert!(result.1);
}

#[test]
fn test_cluster_lb_context_get_override_host_non_strict() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_override_host()
    .returning(|| Some(("10.0.0.2:9090".to_string(), false)));
  let result = mock_ctx.get_override_host().unwrap();
  assert_eq!(result.0, "10.0.0.2:9090");
  assert!(!result.1);
}

#[test]
fn test_cluster_lb_context_get_override_host_none() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx.expect_get_override_host().returning(|| None);
  assert!(mock_ctx.get_override_host().is_none());
}

#[test]
fn test_cluster_lb_context_get_downstream_connection_sni() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_connection_sni()
    .returning(|| Some("example.com".to_string()));
  assert_eq!(
    mock_ctx.get_downstream_connection_sni(),
    Some("example.com".to_string())
  );
}

#[test]
fn test_cluster_lb_context_get_downstream_connection_sni_none() {
  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_connection_sni()
    .returning(|| None);
  assert!(mock_ctx.get_downstream_connection_sni().is_none());
}

#[test]
fn test_cluster_lb_choose_host_with_context() {
  struct TestClusterLb;
  impl cluster::ClusterLb for TestClusterLb {
    fn choose_host(
      &mut self,
      context: Option<&dyn cluster::ClusterLbContext>,
      _async_completion: Box<dyn cluster::EnvoyAsyncHostSelectionComplete>,
    ) -> cluster::HostSelectionResult {
      let ctx = context.expect("context should be Some");
      assert_eq!(ctx.get_host_selection_retry_count(), 3);
      assert_eq!(ctx.compute_hash_key(), Some(12345));
      cluster::HostSelectionResult::Selected(0x1234 as *mut _)
    }
  }

  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_host_selection_retry_count()
    .returning(|| 3);
  mock_ctx.expect_compute_hash_key().returning(|| Some(12345));

  let mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();

  let mut lb = TestClusterLb;
  let result = lb.choose_host(Some(&mock_ctx), Box::new(mock_completion));
  match result {
    cluster::HostSelectionResult::Selected(host) => assert_eq!(host, 0x1234 as *mut _),
    _ => panic!("Expected Selected"),
  }
}

#[test]
fn test_cluster_lb_choose_host_without_context() {
  struct TestClusterLb;
  impl cluster::ClusterLb for TestClusterLb {
    fn choose_host(
      &mut self,
      context: Option<&dyn cluster::ClusterLbContext>,
      _async_completion: Box<dyn cluster::EnvoyAsyncHostSelectionComplete>,
    ) -> cluster::HostSelectionResult {
      assert!(context.is_none());
      cluster::HostSelectionResult::NoHost
    }
  }

  let mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  let mut lb = TestClusterLb;
  let result = lb.choose_host(None, Box::new(mock_completion));
  match result {
    cluster::HostSelectionResult::NoHost => {},
    _ => panic!("Expected NoHost"),
  }
}

#[test]
fn test_cluster_lb_choose_host_async_pending() {
  struct TestAsyncHandle {
    cancelled: std::sync::Arc<std::sync::atomic::AtomicBool>,
  }
  impl cluster::AsyncHostSelectionHandle for TestAsyncHandle {
    fn cancel(&mut self) {
      self
        .cancelled
        .store(true, std::sync::atomic::Ordering::SeqCst);
    }
  }

  struct TestAsyncLb {
    cancelled: std::sync::Arc<std::sync::atomic::AtomicBool>,
  }
  impl cluster::ClusterLb for TestAsyncLb {
    fn choose_host(
      &mut self,
      _context: Option<&dyn cluster::ClusterLbContext>,
      _async_completion: Box<dyn cluster::EnvoyAsyncHostSelectionComplete>,
    ) -> cluster::HostSelectionResult {
      cluster::HostSelectionResult::AsyncPending(Box::new(TestAsyncHandle {
        cancelled: self.cancelled.clone(),
      }))
    }
  }

  let cancelled = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
  let mut lb = TestAsyncLb {
    cancelled: cancelled.clone(),
  };
  let mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  let result = lb.choose_host(None, Box::new(mock_completion));
  match result {
    cluster::HostSelectionResult::AsyncPending(mut handle) => {
      assert!(!cancelled.load(std::sync::atomic::Ordering::SeqCst));
      handle.cancel();
      assert!(cancelled.load(std::sync::atomic::Ordering::SeqCst));
    },
    _ => panic!("Expected AsyncPending"),
  }
}

#[test]
fn test_cluster_lb_context_full_workflow() {
  struct SniBasedLb;
  impl cluster::ClusterLb for SniBasedLb {
    fn choose_host(
      &mut self,
      context: Option<&dyn cluster::ClusterLbContext>,
      _async_completion: Box<dyn cluster::EnvoyAsyncHostSelectionComplete>,
    ) -> cluster::HostSelectionResult {
      let ctx = match context {
        Some(c) => c,
        None => return cluster::HostSelectionResult::NoHost,
      };

      let sni = match ctx.get_downstream_connection_sni() {
        Some(s) => s,
        None => return cluster::HostSelectionResult::NoHost,
      };
      assert_eq!(sni, "backend.example.com");

      let (host_header, _) = match ctx.get_downstream_header("host", 0) {
        Some(h) => h,
        None => return cluster::HostSelectionResult::NoHost,
      };
      assert_eq!(host_header, "backend.example.com");

      let hash = match ctx.compute_hash_key() {
        Some(h) => h,
        None => return cluster::HostSelectionResult::NoHost,
      };
      assert_eq!(hash, 99999);

      if ctx.should_select_another_host(0, 0) {
        return cluster::HostSelectionResult::NoHost;
      }

      cluster::HostSelectionResult::Selected(0xABCD as *mut _)
    }
  }

  let mut mock_ctx = cluster::MockClusterLbContext::new();
  mock_ctx
    .expect_get_downstream_connection_sni()
    .returning(|| Some("backend.example.com".to_string()));
  mock_ctx
    .expect_get_downstream_header()
    .withf(|key, index| key == "host" && *index == 0)
    .returning(|_, _| Some(("backend.example.com".to_string(), 1)));
  mock_ctx.expect_compute_hash_key().returning(|| Some(99999));
  mock_ctx
    .expect_should_select_another_host()
    .returning(|_, _| false);

  let mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  let mut lb = SniBasedLb;
  let result = lb.choose_host(Some(&mock_ctx), Box::new(mock_completion));
  match result {
    cluster::HostSelectionResult::Selected(host) => assert_eq!(host, 0xABCD as *mut _),
    _ => panic!("Expected Selected"),
  }
}

// =================================================================================================
// Async Host Selection Tests
// =================================================================================================

#[test]
fn test_async_host_selection_complete_with_host() {
  let mut mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  mock_completion
    .expect_async_host_selection_complete()
    .withf(|host, details| host.is_some() && details == "resolved")
    .times(1)
    .returning(|_, _| ());

  mock_completion.async_host_selection_complete(Some(0x1234 as *mut _), "resolved");
}

#[test]
fn test_async_host_selection_complete_no_host() {
  let mut mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  mock_completion
    .expect_async_host_selection_complete()
    .withf(|host, details| host.is_none() && details == "dns_failure")
    .times(1)
    .returning(|_, _| ());

  mock_completion.async_host_selection_complete(None, "dns_failure");
}

#[test]
fn test_async_host_selection_complete_empty_details() {
  let mut mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  mock_completion
    .expect_async_host_selection_complete()
    .withf(|host, details| host.is_none() && details.is_empty())
    .times(1)
    .returning(|_, _| ());

  mock_completion.async_host_selection_complete(None, "");
}

#[test]
fn test_async_host_selection_with_stored_completion() {
  struct DnsResolvingLb {
    pending_completion: Option<Box<dyn cluster::EnvoyAsyncHostSelectionComplete>>,
  }
  impl cluster::ClusterLb for DnsResolvingLb {
    fn choose_host(
      &mut self,
      _context: Option<&dyn cluster::ClusterLbContext>,
      async_completion: Box<dyn cluster::EnvoyAsyncHostSelectionComplete>,
    ) -> cluster::HostSelectionResult {
      self.pending_completion = Some(async_completion);
      struct NoOpHandle;
      impl cluster::AsyncHostSelectionHandle for NoOpHandle {
        fn cancel(&mut self) {}
      }
      cluster::HostSelectionResult::AsyncPending(Box::new(NoOpHandle))
    }
  }

  let mut mock_completion = cluster::MockEnvoyAsyncHostSelectionComplete::new();
  mock_completion
    .expect_async_host_selection_complete()
    .withf(|host, details| host == &Some(0xBEEF as *mut _) && details == "dns_resolved")
    .times(1)
    .returning(|_, _| ());

  let mut lb = DnsResolvingLb {
    pending_completion: None,
  };
  let result = lb.choose_host(None, Box::new(mock_completion));
  assert!(matches!(
    result,
    cluster::HostSelectionResult::AsyncPending(_)
  ));

  // Simulate async DNS resolution completing.
  let completion = lb.pending_completion.take().unwrap();
  completion.async_host_selection_complete(Some(0xBEEF as *mut _), "dns_resolved");
}

#[test]
fn test_bootstrap_extension_cluster_add_or_update() {
  use std::sync::atomic::{AtomicBool, Ordering};
  static CLUSTER_ADDED: AtomicBool = AtomicBool::new(false);
  static CLUSTER_NAME_RECEIVED: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_cluster_add_or_update(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      cluster_name: &str,
    ) {
      CLUSTER_ADDED.store(true, Ordering::SeqCst);
      *CLUSTER_NAME_RECEIVED.lock().unwrap() = cluster_name.to_string();
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let cluster_name = "test_cluster";
  let cluster_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: cluster_name.as_ptr() as *const _,
    length: cluster_name.len(),
  };

  CLUSTER_ADDED.store(false, Ordering::SeqCst);
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
      std::ptr::null_mut(),
      config_ptr,
      cluster_name_buf,
    );
  }

  assert!(CLUSTER_ADDED.load(Ordering::SeqCst));
  assert_eq!(*CLUSTER_NAME_RECEIVED.lock().unwrap(), "test_cluster");

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_cluster_removal() {
  use std::sync::atomic::{AtomicBool, Ordering};
  static CLUSTER_REMOVED: AtomicBool = AtomicBool::new(false);
  static REMOVED_CLUSTER_NAME: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_cluster_removal(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      cluster_name: &str,
    ) {
      CLUSTER_REMOVED.store(true, Ordering::SeqCst);
      *REMOVED_CLUSTER_NAME.lock().unwrap() = cluster_name.to_string();
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let cluster_name = "removed_cluster";
  let cluster_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: cluster_name.as_ptr() as *const _,
    length: cluster_name.len(),
  };

  CLUSTER_REMOVED.store(false, Ordering::SeqCst);
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
      std::ptr::null_mut(),
      config_ptr,
      cluster_name_buf,
    );
  }

  assert!(CLUSTER_REMOVED.load(Ordering::SeqCst));
  assert_eq!(*REMOVED_CLUSTER_NAME.lock().unwrap(), "removed_cluster");

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_cluster_lifecycle_default_noop() {
  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let cluster_name = "test_cluster";
  let cluster_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: cluster_name.as_ptr() as *const _,
    length: cluster_name.len(),
  };

  // Calling cluster lifecycle hooks with default implementations should not panic.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
      std::ptr::null_mut(),
      config_ptr,
      cluster_name_buf,
    );
    envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
      std::ptr::null_mut(),
      config_ptr,
      cluster_name_buf,
    );
  }

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_listener_add_or_update() {
  use std::sync::atomic::{AtomicBool, Ordering};
  static LISTENER_ADDED: AtomicBool = AtomicBool::new(false);
  static LISTENER_NAME_RECEIVED: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_listener_add_or_update(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      listener_name: &str,
    ) {
      LISTENER_ADDED.store(true, Ordering::SeqCst);
      *LISTENER_NAME_RECEIVED.lock().unwrap() = listener_name.to_string();
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let listener_name = "test_listener";
  let listener_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: listener_name.as_ptr() as *const _,
    length: listener_name.len(),
  };

  LISTENER_ADDED.store(false, Ordering::SeqCst);
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
      std::ptr::null_mut(),
      config_ptr,
      listener_name_buf,
    );
  }

  assert!(LISTENER_ADDED.load(Ordering::SeqCst));
  assert_eq!(*LISTENER_NAME_RECEIVED.lock().unwrap(), "test_listener");

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_listener_removal() {
  use std::sync::atomic::{AtomicBool, Ordering};
  static LISTENER_REMOVED: AtomicBool = AtomicBool::new(false);
  static REMOVED_LISTENER_NAME: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }

    fn on_listener_removal(
      &self,
      _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
      listener_name: &str,
    ) {
      LISTENER_REMOVED.store(true, Ordering::SeqCst);
      *REMOVED_LISTENER_NAME.lock().unwrap() = listener_name.to_string();
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let listener_name = "removed_listener";
  let listener_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: listener_name.as_ptr() as *const _,
    length: listener_name.len(),
  };

  LISTENER_REMOVED.store(false, Ordering::SeqCst);
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_listener_removal(
      std::ptr::null_mut(),
      config_ptr,
      listener_name_buf,
    );
  }

  assert!(LISTENER_REMOVED.load(Ordering::SeqCst));
  assert_eq!(*REMOVED_LISTENER_NAME.lock().unwrap(), "removed_listener");

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}

#[test]
fn test_bootstrap_extension_listener_lifecycle_default_noop() {
  struct TestBootstrapExtensionConfig;
  impl BootstrapExtensionConfig for TestBootstrapExtensionConfig {
    fn new_bootstrap_extension(
      &self,
      _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    ) -> Box<dyn BootstrapExtension> {
      Box::new(TestBootstrapExtension)
    }
  }

  struct TestBootstrapExtension;
  impl BootstrapExtension for TestBootstrapExtension {}

  fn new_config(
    _envoy_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _name: &str,
    _config: &[u8],
  ) -> Option<Box<dyn BootstrapExtensionConfig>> {
    Some(Box::new(TestBootstrapExtensionConfig))
  }

  let mut envoy_config = bootstrap::EnvoyBootstrapExtensionConfigImpl::new(std::ptr::null_mut());
  let config_ptr = bootstrap::init_bootstrap_extension_config(
    &mut envoy_config,
    "test",
    b"config",
    &(new_config as NewBootstrapExtensionConfigFunction),
  );
  assert!(!config_ptr.is_null());

  let listener_name = "test_listener";
  let listener_name_buf = abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: listener_name.as_ptr() as *const _,
    length: listener_name.len(),
  };

  // Calling listener lifecycle hooks with default implementations should not panic.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
      std::ptr::null_mut(),
      config_ptr,
      listener_name_buf,
    );
    envoy_dynamic_module_on_bootstrap_extension_listener_removal(
      std::ptr::null_mut(),
      config_ptr,
      listener_name_buf,
    );
  }

  // Clean up.
  unsafe {
    envoy_dynamic_module_on_bootstrap_extension_config_destroy(config_ptr);
  }
}
