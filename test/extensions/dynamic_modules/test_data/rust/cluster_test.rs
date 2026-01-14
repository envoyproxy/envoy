// A test cluster dynamic module for integration testing.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

declare_cluster_init_functions!(program_init, new_cluster_config);

fn program_init() -> bool {
  true
}

fn new_cluster_config(
  _envoy: &mut EnvoyClusterImpl,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn ClusterConfig<EnvoyClusterImpl>>> {
  match name {
    "round_robin" => Some(Box::new(RoundRobinClusterConfig {})),
    "composite" => Some(Box::new(CompositeClusterConfig {})),
    "lb_returns_host" => Some(Box::new(LbReturnsHostClusterConfig {})),
    _ => Some(Box::new(NoOpClusterConfig {})),
  }
}

// No-op cluster configuration for basic testing.
struct NoOpClusterConfig {}

impl ClusterConfig<EnvoyClusterImpl> for NoOpClusterConfig {
  fn new_cluster(&self, _envoy: &mut EnvoyClusterImpl) -> Box<dyn Cluster<EnvoyClusterImpl>> {
    Box::new(NoOpCluster {})
  }
}

struct NoOpCluster {}

impl Cluster<EnvoyClusterImpl> for NoOpCluster {
  fn on_init(&mut self, envoy: &mut EnvoyClusterImpl) {
    envoy.pre_init_complete();
  }

  fn new_load_balancer(&self) -> Box<dyn LoadBalancer> {
    Box::new(NoOpLoadBalancer {})
  }
}

struct NoOpLoadBalancer {}

impl LoadBalancer for NoOpLoadBalancer {
  fn choose_host(&mut self, _context: LoadBalancerContextHandle) -> Option<HostHandle> {
    None
  }
}

// Round-robin cluster configuration.
struct RoundRobinClusterConfig {}

impl ClusterConfig<EnvoyClusterImpl> for RoundRobinClusterConfig {
  fn new_cluster(&self, _envoy: &mut EnvoyClusterImpl) -> Box<dyn Cluster<EnvoyClusterImpl>> {
    Box::new(RoundRobinCluster {
      hosts: Arc::new(std::sync::RwLock::new(Vec::new())),
    })
  }
}

struct RoundRobinCluster {
  hosts: Arc<std::sync::RwLock<Vec<HostHandle>>>,
}

impl Cluster<EnvoyClusterImpl> for RoundRobinCluster {
  fn on_init(&mut self, envoy: &mut EnvoyClusterImpl) {
    // Just call pre_init_complete without any logging.
    envoy.pre_init_complete();
  }

  fn on_host_set_change(
    &mut self,
    _envoy: &mut EnvoyClusterImpl,
    hosts_added: &[HostInfo],
    hosts_removed: &[HostInfo],
  ) {
    let mut hosts = self.hosts.write().unwrap();
    // Remove hosts.
    for removed in hosts_removed {
      hosts.retain(|h| h.0 != removed.host);
    }
    // Add hosts.
    for added in hosts_added {
      hosts.push(HostHandle(added.host));
    }
  }

  fn new_load_balancer(&self) -> Box<dyn LoadBalancer> {
    Box::new(RoundRobinLoadBalancer {
      hosts: self.hosts.clone(),
      index: AtomicUsize::new(0),
    })
  }
}

struct RoundRobinLoadBalancer {
  hosts: Arc<std::sync::RwLock<Vec<HostHandle>>>,
  index: AtomicUsize,
}

impl LoadBalancer for RoundRobinLoadBalancer {
  fn choose_host(&mut self, _context: LoadBalancerContextHandle) -> Option<HostHandle> {
    let hosts = self.hosts.read().unwrap();
    if hosts.is_empty() {
      return None;
    }
    let idx = self.index.fetch_add(1, Ordering::Relaxed) % hosts.len();
    Some(hosts[idx])
  }
}

// Composite cluster configuration for retry progression.
struct CompositeClusterConfig {}

impl ClusterConfig<EnvoyClusterImpl> for CompositeClusterConfig {
  fn new_cluster(&self, _envoy: &mut EnvoyClusterImpl) -> Box<dyn Cluster<EnvoyClusterImpl>> {
    Box::new(CompositeCluster {
      sub_clusters: vec!["primary".to_string(), "secondary".to_string()],
    })
  }
}

struct CompositeCluster {
  sub_clusters: Vec<String>,
}

impl Cluster<EnvoyClusterImpl> for CompositeCluster {
  fn on_init(&mut self, envoy: &mut EnvoyClusterImpl) {
    envoy.pre_init_complete();
  }

  fn new_load_balancer(&self) -> Box<dyn LoadBalancer> {
    Box::new(CompositeLoadBalancer {
      sub_clusters: self.sub_clusters.clone(),
    })
  }
}

struct CompositeLoadBalancer {
  sub_clusters: Vec<String>,
}

impl LoadBalancer for CompositeLoadBalancer {
  fn choose_host(&mut self, context: LoadBalancerContextHandle) -> Option<HostHandle> {
    // Select sub-cluster based on attempt count for retry progression.
    let attempt = context.attempt_count() as usize;
    let _cluster_idx = attempt % self.sub_clusters.len();

    // In a real implementation, we would use the cluster manager to get the
    // thread-local cluster and choose a host from it.
    // For now, we just return None since we don't have access to the cluster manager.
    None
  }
}

// Cluster config where the load balancer returns an actual host.
struct LbReturnsHostClusterConfig {}

impl ClusterConfig<EnvoyClusterImpl> for LbReturnsHostClusterConfig {
  fn new_cluster(&self, _envoy: &mut EnvoyClusterImpl) -> Box<dyn Cluster<EnvoyClusterImpl>> {
    Box::new(LbReturnsHostCluster {
      hosts: Arc::new(std::sync::RwLock::new(Vec::new())),
    })
  }
}

struct LbReturnsHostCluster {
  hosts: Arc<std::sync::RwLock<Vec<HostHandle>>>,
}

impl Cluster<EnvoyClusterImpl> for LbReturnsHostCluster {
  fn on_init(&mut self, envoy: &mut EnvoyClusterImpl) {
    envoy.pre_init_complete();
  }

  fn on_host_set_change(
    &mut self,
    _envoy: &mut EnvoyClusterImpl,
    hosts_added: &[HostInfo],
    hosts_removed: &[HostInfo],
  ) {
    let mut hosts = self.hosts.write().unwrap();
    for removed in hosts_removed {
      hosts.retain(|h| h.0 != removed.host);
    }
    for added in hosts_added {
      hosts.push(HostHandle(added.host));
    }
  }

  fn new_load_balancer(&self) -> Box<dyn LoadBalancer> {
    Box::new(LbReturnsHostLoadBalancer {
      hosts: self.hosts.clone(),
    })
  }
}

struct LbReturnsHostLoadBalancer {
  hosts: Arc<std::sync::RwLock<Vec<HostHandle>>>,
}

impl LoadBalancer for LbReturnsHostLoadBalancer {
  fn choose_host(&mut self, _context: LoadBalancerContextHandle) -> Option<HostHandle> {
    let hosts = self.hosts.read().unwrap();
    // Return the first host if available.
    hosts.first().copied()
  }
}
