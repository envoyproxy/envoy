use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicUsize, Ordering};

declare_cluster_init_functions!(init, new_cluster_config);

fn init() -> bool {
  true
}

fn new_cluster_config(
  _envoy_cluster_config: &mut EnvoyClusterConfig,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn ClusterConfig>> {
  match name {
    "round_robin" => Some(Box::new(RoundRobinClusterConfig::new(config))),
    "composite" => Some(Box::new(CompositeClusterConfig::new(config))),
    _ => None,
  }
}

// =============================================================================
// Round Robin Cluster Implementation
// =============================================================================

struct RoundRobinClusterConfig {
  initial_hosts: Vec<(String, u32)>,
}

impl RoundRobinClusterConfig {
  fn new(config: &[u8]) -> Self {
    // Parse config for initial hosts.
    // For simplicity, assume config is a comma-separated list: "host1:port1,host2:port2".
    let config_str = std::str::from_utf8(config).unwrap_or("");
    let mut initial_hosts = Vec::new();

    if !config_str.is_empty() {
      for host_str in config_str.split(',') {
        let parts: Vec<&str> = host_str.split(':').collect();
        if parts.len() == 2 {
          if let Ok(port) = parts[1].parse::<u32>() {
            initial_hosts.push((parts[0].to_string(), port));
          }
        }
      }
    }

    Self { initial_hosts }
  }
}

impl ClusterConfig for RoundRobinClusterConfig {
  fn new_cluster(&mut self, envoy: &mut dyn EnvoyCluster) -> Box<dyn Cluster> {
    let mut hosts = Vec::new();

    // Add initial hosts.
    for (address, port) in &self.initial_hosts {
      if let Ok(handle) = envoy.add_host(address, *port, 1) {
        hosts.push(handle);
      }
    }

    Box::new(RoundRobinCluster { hosts })
  }
}

struct RoundRobinCluster {
  hosts: Vec<HostHandle>,
}

impl Cluster for RoundRobinCluster {
  fn on_init(&mut self, envoy: &mut dyn EnvoyCluster) {
    // Signal initialization complete.
    envoy.pre_init_complete();
  }

  fn on_cleanup(&mut self, _envoy: &mut dyn EnvoyCluster) {
    // No cleanup needed for this simple implementation.
  }

  fn new_load_balancer(&mut self) -> Box<dyn LoadBalancer> {
    Box::new(RoundRobinLoadBalancer {
      hosts: self.hosts.clone(),
      index: AtomicUsize::new(0),
    })
  }
}

struct RoundRobinLoadBalancer {
  hosts: Vec<HostHandle>,
  index: AtomicUsize,
}

impl LoadBalancer for RoundRobinLoadBalancer {
  fn choose_host(
    &mut self,
    _envoy_lb: &mut EnvoyLoadBalancer,
    _context: &LoadBalancerContext,
  ) -> Option<HostHandle> {
    if self.hosts.is_empty() {
      return None;
    }

    let idx = self.index.fetch_add(1, Ordering::Relaxed) % self.hosts.len();
    Some(self.hosts[idx])
  }
}

// =============================================================================
// Composite Cluster Implementation - Demonstrates Retry Progression Pattern
// =============================================================================

/// Overflow behavior when retry attempts exceed the number of sub-clusters.
#[derive(Clone, Copy)]
enum OverflowOption {
  /// Further retry attempts fail with no host available.
  Fail,
  /// Continue using the last cluster for overflow attempts.
  UseLastCluster,
  /// Cycle through all clusters for overflow attempts.
  RoundRobin,
}

struct CompositeClusterConfig {
  /// Names of sub-clusters in retry progression order.
  sub_cluster_names: Vec<String>,
  overflow_option: OverflowOption,
}

impl CompositeClusterConfig {
  fn new(config: &[u8]) -> Self {
    // Parse config: "cluster1,cluster2,cluster3;overflow=round_robin"
    let config_str = std::str::from_utf8(config).unwrap_or("");
    let mut sub_cluster_names = Vec::new();
    let mut overflow_option = OverflowOption::Fail;

    for part in config_str.split(';') {
      if let Some(overflow_value) = part.strip_prefix("overflow=") {
        match overflow_value {
          "fail" => overflow_option = OverflowOption::Fail,
          "use_last" => overflow_option = OverflowOption::UseLastCluster,
          "round_robin" => overflow_option = OverflowOption::RoundRobin,
          _ => {},
        }
      } else {
        // Parse cluster names.
        for name in part.split(',') {
          if !name.is_empty() {
            sub_cluster_names.push(name.to_string());
          }
        }
      }
    }

    Self {
      sub_cluster_names,
      overflow_option,
    }
  }
}

impl ClusterConfig for CompositeClusterConfig {
  fn new_cluster(&mut self, _envoy: &mut dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(CompositeCluster {
      sub_cluster_names: self.sub_cluster_names.clone(),
      overflow_option: self.overflow_option,
    })
  }
}

struct CompositeCluster {
  sub_cluster_names: Vec<String>,
  overflow_option: OverflowOption,
}

impl Cluster for CompositeCluster {
  fn on_init(&mut self, envoy: &mut dyn EnvoyCluster) {
    // Composite cluster does not add hosts directly - it references other clusters.
    envoy.pre_init_complete();
  }

  fn on_cleanup(&mut self, _envoy: &mut dyn EnvoyCluster) {}

  fn new_load_balancer(&mut self) -> Box<dyn LoadBalancer> {
    Box::new(CompositeLoadBalancer {
      sub_cluster_names: self.sub_cluster_names.clone(),
      overflow_option: self.overflow_option,
    })
  }

  fn on_host_set_change(&mut self, hosts_added: &[HostHandle], hosts_removed: &[HostHandle]) {
    // Log host set changes for debugging.
    let _ = (hosts_added.len(), hosts_removed.len());
  }

  fn on_host_health_change(
    &mut self,
    _host: HostHandle,
    transition: HealthTransition,
    current_health: HostHealth,
  ) {
    // Log health changes for debugging.
    let _ = (transition, current_health);
  }
}

struct CompositeLoadBalancer {
  sub_cluster_names: Vec<String>,
  overflow_option: OverflowOption,
}

impl LoadBalancer for CompositeLoadBalancer {
  fn choose_host(
    &mut self,
    _envoy_lb: &mut EnvoyLoadBalancer,
    context: &LoadBalancerContext,
  ) -> Option<HostHandle> {
    if self.sub_cluster_names.is_empty() {
      return None;
    }

    // Get the current attempt count (1-based: 1 = initial, 2 = first retry, etc.).
    let attempt = context.attempt_count().unwrap_or(1) as usize;

    // Determine which cluster to use based on attempt count.
    let cluster_index = if attempt <= self.sub_cluster_names.len() {
      // Use the cluster corresponding to this attempt.
      attempt - 1
    } else {
      // Handle overflow.
      match self.overflow_option {
        OverflowOption::Fail => return None,
        OverflowOption::UseLastCluster => self.sub_cluster_names.len() - 1,
        OverflowOption::RoundRobin => (attempt - 1) % self.sub_cluster_names.len(),
      }
    };

    // Note: In a real implementation, we would use envoy.get_thread_local_cluster()
    // to get the target cluster and call choose_host on it. For this test module,
    // we demonstrate the pattern but cannot actually call across clusters without
    // a real cluster manager.
    //
    // Example of how this would work:
    // let cluster_name = &self.sub_cluster_names[cluster_index];
    // if let Some(tl_cluster) = envoy.get_thread_local_cluster(cluster_name) {
    //     return tl_cluster.choose_host(context);
    // }

    // For testing, we just return None to indicate no host selected.
    // The actual composite cluster would delegate to sub-clusters.
    let _ = cluster_index;
    None
  }
}
