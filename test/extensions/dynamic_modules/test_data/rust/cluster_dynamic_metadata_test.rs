//! Integration test for the cluster dynamic-metadata set ABI.
//!
//! Exposes a cluster whose load balancer, during ``choose_host``, annotates the current request
//! under the ``dynamic_modules.test`` namespace with a number and a string through the single-key
//! setters, two more strings through the batch setter, one more through the struct setter and typed
//! metadata through the typed setter, then returns its pre-registered upstream. It selects no host
//! if Envoy rejects a valid buffer or accepts a malformed one. The C++ harness reads the untyped
//! values back from the access log via ``%DYNAMIC_METADATA(dynamic_modules.test:...)%``.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::{Arc, Mutex};

const METADATA_NAMESPACE: &str = "dynamic_modules.test";
const NUMBER_KEY: &str = "number_key";
const STRING_KEY: &str = "string_key";
const NUMBER_VALUE: f64 = 1234.0;
const STRING_VALUE: &str = "test_value";
const BATCH_ENTRIES: [(&str, &str); 2] = [("l1_decision", "resolved"), ("l2_selector", "dicer")];
// A serialized google.protobuf.Struct with "k" set to "v".
const SERIALIZED_STRUCT: &[u8] = &[0x0a, 0x08, 0x0a, 0x01, 0x6b, 0x12, 0x03, 0x1a, 0x01, 0x76];
// A serialized google.protobuf.Any with type URL "t/x" and value 0x01 0x02.
const SERIALIZED_ANY: &[u8] = &[0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02];

declare_cluster_init_functions!(my_program_init, new_cluster_config_fn);

fn my_program_init() -> bool {
  true
}

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
    "dynamic_metadata_writer" => Some(Box::new(DynamicMetadataWriterClusterConfig {
      upstream_address,
    })),
    _ => None,
  }
}

struct DynamicMetadataWriterClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for DynamicMetadataWriterClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(DynamicMetadataWriterCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct DynamicMetadataWriterCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

impl Cluster for DynamicMetadataWriterCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(
    &self,
    _envoy_lb: &dyn EnvoyClusterLoadBalancer,
  ) -> Option<Box<dyn ClusterLb>> {
    Some(Box::new(DynamicMetadataWriterLb {
      hosts: self.hosts.clone(),
    }))
  }
}

struct DynamicMetadataWriterLb {
  hosts: SharedHostList,
}

impl ClusterLb for DynamicMetadataWriterLb {
  fn choose_host(
    &mut self,
    context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let Some(ctx) = context else {
      return HostSelectionResult::NoHost;
    };
    ctx.set_dynamic_metadata_number(METADATA_NAMESPACE, NUMBER_KEY, NUMBER_VALUE);
    ctx.set_dynamic_metadata_string(METADATA_NAMESPACE, STRING_KEY, STRING_VALUE);
    ctx.set_dynamic_metadata_string_batch(METADATA_NAMESPACE, &BATCH_ENTRIES);
    if !ctx.set_dynamic_metadata_struct(METADATA_NAMESPACE, SERIALIZED_STRUCT)
      || !ctx.set_dynamic_typed_metadata(METADATA_NAMESPACE, SERIALIZED_ANY)
    {
      return HostSelectionResult::NoHost;
    }
    // A struct that does not parse and an Any without a type URL are rejected.
    if ctx.set_dynamic_metadata_struct(METADATA_NAMESPACE, &[0x0f])
      || ctx.set_dynamic_typed_metadata(METADATA_NAMESPACE, &[])
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
