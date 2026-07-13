//! Integration test for the cluster dynamic-metadata set ABI.
//!
//! Exposes a cluster whose load balancer, during ``choose_host``, annotates the current request
//! with a number and a string under the ``dynamic_modules.test`` namespace, then returns its
//! pre-registered upstream. The C++ harness reads the two values back from the access log via
//! ``%DYNAMIC_METADATA(dynamic_modules.test:...)%``.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::{Arc, Mutex};

const METADATA_NAMESPACE: &str = "dynamic_modules.test";
const NUMBER_KEY: &str = "number_key";
const STRING_KEY: &str = "string_key";
const NUMBER_VALUE: f64 = 1234.0;
const STRING_VALUE: &str = "test_value";

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

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(DynamicMetadataWriterLb {
      hosts: self.hosts.clone(),
    })
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
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    HostSelectionResult::Selected(hosts.0[0])
  }
}
