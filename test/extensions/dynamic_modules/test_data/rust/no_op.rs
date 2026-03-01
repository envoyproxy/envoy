use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};

declare_init_functions!(init, new_nop_http_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  LOAD_ID.fetch_add(1, Ordering::SeqCst);
  true
}

static LOAD_ID: AtomicI32 = AtomicI32::new(0);
static SEEN_LOAD_ID: AtomicI32 = AtomicI32::new(-1);
static SOME_VARIABLE: AtomicI32 = AtomicI32::new(0);

#[no_mangle]
pub extern "C" fn getSomeVariable() -> i32 {
  let current_load = LOAD_ID.load(Ordering::SeqCst);
  let mut seen = SEEN_LOAD_ID.load(Ordering::SeqCst);
  loop {
    if seen != current_load {
      match SEEN_LOAD_ID.compare_exchange(seen, current_load, Ordering::SeqCst, Ordering::SeqCst) {
        Ok(_) => {
          SOME_VARIABLE.store(1, Ordering::SeqCst);
          return 1;
        },
        Err(actual) => {
          seen = actual;
          continue;
        },
      }
    }
    let current = SOME_VARIABLE.load(Ordering::SeqCst);
    match SOME_VARIABLE.compare_exchange(current, current + 1, Ordering::SeqCst, Ordering::SeqCst) {
      Ok(_) => return current + 1,
      Err(_) => continue,
    }
  }
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewHttpFilterConfigFunction`]
/// signature.
fn new_nop_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  let name = name.to_string();
  let config = String::from_utf8(config.to_owned()).unwrap_or_default();
  Some(Box::new(NopHttpFilterConfig { name, config }))
}

/// A no-op HTTP filter configuration that implements
/// [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilterConfig`] as well as the [`Drop`] to test the
/// cleanup of the configuration.
struct NopHttpFilterConfig {
  name: String,
  config: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for NopHttpFilterConfig {
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(NopHttpFilter {
      on_request_headers_called: false,
      on_request_body_called: false,
      on_request_trailers_called: false,
      on_response_headers_called: false,
      on_response_body_called: false,
      on_response_trailers_called: false,
      on_stream_complete_called: false,
    })
  }
}

impl Drop for NopHttpFilterConfig {
  fn drop(&mut self) {
    assert_eq!(self.name, "foo");
    assert_eq!(self.config, "bar");
  }
}

/// A no-op HTTP filter that implements [`envoy_proxy_dynamic_modules_rust_sdk::HttpFilter`]
/// as well as the [`Drop`] to test the cleanup of the filter.
struct NopHttpFilter {
  on_request_headers_called: bool,
  on_request_body_called: bool,
  on_request_trailers_called: bool,
  on_response_headers_called: bool,
  on_response_body_called: bool,
  on_response_trailers_called: bool,
  on_stream_complete_called: bool,
}

impl Drop for NopHttpFilter {
  fn drop(&mut self) {
    assert!(self.on_request_headers_called);
    assert!(self.on_request_body_called);
    assert!(self.on_request_trailers_called);
    assert!(self.on_response_headers_called);
    assert!(self.on_response_body_called);
    assert!(self.on_response_trailers_called);
    assert!(self.on_stream_complete_called);
  }
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for NopHttpFilter {
  fn on_request_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    self.on_request_headers_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  fn on_request_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    self.on_request_body_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  fn on_request_trailers(
    &mut self,
    _envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    self.on_request_trailers_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  fn on_response_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    self.on_response_headers_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  fn on_response_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    self.on_response_body_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  fn on_response_trailers(
    &mut self,
    _envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    self.on_response_trailers_called = true;
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }

  fn on_stream_complete(&mut self, _envoy_filter: &mut EHF) {
    self.on_stream_complete_called = true;
  }
}
