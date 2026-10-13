use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Mutex;

declare_init_functions!(init, new_http_filter_config_fn);

fn init() -> bool {
  true
}

/// The memory retained by the module until a request releases it.
static RETAINED: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  Some(Box::new(MemoryFilterConfig {}))
}

struct MemoryFilterConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for MemoryFilterConfig {
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(MemoryFilter {})
  }
}

/// Retains the number of bytes in the `x-retain-bytes` request header, releases all the retained
/// memory when the `x-release` request header is present, and replies without an upstream.
struct MemoryFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for MemoryFilter {
  fn on_request_headers(
    &self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    let retain_bytes = envoy_filter
      .get_request_header_value("x-retain-bytes")
      .and_then(|value| {
        std::str::from_utf8(value.as_slice())
          .ok()?
          .parse::<usize>()
          .ok()
      });
    if let Some(bytes) = retain_bytes {
      RETAINED.lock().unwrap().push(vec![1; bytes]);
    }
    if envoy_filter.get_request_header_value("x-release").is_some() {
      RETAINED.lock().unwrap().clear();
    }
    envoy_filter.send_response(200, &[], None, None);
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }
}
