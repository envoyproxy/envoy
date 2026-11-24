use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::memory::{MemoryResourceMonitor, TrackingAllocator};
use envoy_proxy_dynamic_modules_rust_sdk::resource_monitor::{
  EnvoyResourceMonitorConfig,
  ResourceMonitor,
};
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::num::NonZeroU64;
use std::sync::Mutex;

#[global_allocator]
static ALLOCATOR: TrackingAllocator = TrackingAllocator::new(std::alloc::System);

declare_all_init_functions!(
  init,
  http: new_http_filter_config_fn,
  resource_monitor: new_resource_monitor_fn,
);

fn init() -> bool {
  true
}

/// Creates a monitor that reports the memory of the module against the heap size in bytes that the
/// configuration holds.
fn new_resource_monitor_fn(
  name: &str,
  config: &[u8],
  _envoy_config: &mut EnvoyResourceMonitorConfig,
) -> Option<Box<dyn ResourceMonitor>> {
  if name != "memory" {
    return None;
  }
  let max_heap_size_bytes: NonZeroU64 = std::str::from_utf8(config).ok()?.parse().ok()?;
  Some(Box::new(MemoryResourceMonitor::new(
    &ALLOCATOR,
    max_heap_size_bytes,
  )))
}

/// The memory retained by the module until a request releases it.
static RETAINED: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  if name != "memory" {
    return None;
  }
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
      // Replacing the vector frees its buffer as well, which would otherwise outlive the module.
      *RETAINED.lock().unwrap() = Vec::new();
    }
    envoy_filter.send_response(200, &[], None, None);
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }
}
