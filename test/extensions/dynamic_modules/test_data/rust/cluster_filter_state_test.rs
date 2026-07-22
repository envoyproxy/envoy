//! Integration test for the cluster filter-state ABI (read and write).
//!
//! Exposes both an HTTP filter and clusters from the same shared library:
//! - The HTTP filter writes two filter state entries (bytes + typed) during ``on_request_headers``.
//! - The ``filter_state_reader`` cluster reads those entries in ``choose_host`` and returns its
//!   pre-registered upstream only when both values match the expected payload, otherwise it
//!   returns ``NoHost`` so the request fails with a 503.
//! - The ``filter_state_writer`` cluster writes two filter state entries (bytes + typed) in
//!   ``choose_host`` before selecting its upstream, so the access log can read them back on the
//!   same request.
//!
//! The C++ test harness registers ``ObjectFactory``s under ``envoy.test.cluster_typed_object`` and
//! ``envoy.test.cluster_written_typed_object`` so the typed read and write paths are exercised.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::{Arc, Mutex};

const BYTES_KEY: &[u8] = b"test.cluster_filter_state.bytes_key";
const BYTES_VALUE: &[u8] = b"bytes_value";
const TYPED_KEY: &[u8] = b"envoy.test.cluster_typed_object";
const TYPED_VALUE: &[u8] = b"typed_value";

const WRITTEN_BYTES_KEY: &[u8] = b"test.cluster_filter_state.written_bytes_key";
const WRITTEN_BYTES_VALUE: &[u8] = b"written_bytes_value";
const WRITTEN_TYPED_KEY: &[u8] = b"envoy.test.cluster_written_typed_object";
const WRITTEN_TYPED_VALUE: &[u8] = b"written_typed_value";

declare_all_init_functions!(
  my_program_init,
  http: new_http_filter_config_fn,
  cluster: new_cluster_config_fn,
);

fn my_program_init() -> bool {
  true
}

// -------------------------------------------------------------------------------------
// HTTP filter that produces filter state.
// -------------------------------------------------------------------------------------

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  match name {
    "filter_state_producer" => Some(Box::new(FilterStateProducerConfig {})),
    _ => None,
  }
}

struct FilterStateProducerConfig {}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for FilterStateProducerConfig {
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(FilterStateProducerFilter {})
  }
}

struct FilterStateProducerFilter {}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for FilterStateProducerFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    assert!(envoy_filter.set_filter_state_bytes(BYTES_KEY, BYTES_VALUE));
    assert!(envoy_filter.set_filter_state_typed(TYPED_KEY, TYPED_VALUE));
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }
}

// -------------------------------------------------------------------------------------
// Cluster that reads filter state during host selection.
// -------------------------------------------------------------------------------------

struct HostList(Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>);
// SAFETY: Host pointers are stable addresses managed by Envoy across threads.
unsafe impl Send for HostList {}
unsafe impl Sync for HostList {}

type SharedHostList = Arc<Mutex<HostList>>;

fn new_cluster_config_fn(
  name: &str,
  config: &[u8],
  _envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics>,
) -> Option<Box<dyn ClusterConfig>> {
  let upstream_address = std::str::from_utf8(config).unwrap_or("").to_string();
  match name {
    "filter_state_reader" => Some(Box::new(FilterStateReaderClusterConfig {
      upstream_address,
    })),
    "filter_state_writer" => Some(Box::new(FilterStateWriterClusterConfig {
      upstream_address,
    })),
    _ => None,
  }
}

struct FilterStateReaderClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for FilterStateReaderClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(FilterStateReaderCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct FilterStateReaderCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

impl Cluster for FilterStateReaderCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(FilterStateReaderLb {
      hosts: self.hosts.clone(),
    })
  }
}

struct FilterStateReaderLb {
  hosts: SharedHostList,
}

impl ClusterLb for FilterStateReaderLb {
  fn choose_host(
    &mut self,
    context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let Some(ctx) = context else {
      return HostSelectionResult::NoHost;
    };
    let bytes = ctx.get_filter_state_bytes(BYTES_KEY);
    let typed = ctx.get_filter_state_typed(TYPED_KEY);
    let bytes_match = bytes.as_ref().map(|b| b.as_slice()) == Some(BYTES_VALUE);
    let typed_match = typed.as_ref().map(|b| b.as_slice()) == Some(TYPED_VALUE);
    if !bytes_match || !typed_match {
      envoy_log_info!(
        "filter_state_reader: mismatch — bytes_match={} typed_match={}",
        bytes_match,
        typed_match
      );
      return HostSelectionResult::NoHost;
    }
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    HostSelectionResult::Selected(hosts.0[0])
  }
}

// -------------------------------------------------------------------------------------
// Cluster that writes filter state during host selection.
// -------------------------------------------------------------------------------------

struct FilterStateWriterClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for FilterStateWriterClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(FilterStateWriterCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct FilterStateWriterCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

impl Cluster for FilterStateWriterCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(FilterStateWriterLb {
      hosts: self.hosts.clone(),
    })
  }
}

struct FilterStateWriterLb {
  hosts: SharedHostList,
}

impl ClusterLb for FilterStateWriterLb {
  fn choose_host(
    &mut self,
    context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let Some(ctx) = context else {
      return HostSelectionResult::NoHost;
    };
    if !ctx.set_filter_state_bytes(WRITTEN_BYTES_KEY, WRITTEN_BYTES_VALUE)
      || !ctx.set_filter_state_typed(WRITTEN_TYPED_KEY, WRITTEN_TYPED_VALUE)
    {
      return HostSelectionResult::NoHost;
    }
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    HostSelectionResult::Selected(hosts.0[0])
  }
}
