use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

declare_cluster_init_functions!(my_program_init, new_cluster_config);

fn my_program_init() -> bool {
  true
}

/// Thread-safe wrapper for host pointers returned by [`EnvoyCluster::add_hosts`].
///
/// Raw pointers are `!Send` and `!Sync`, but the Envoy ABI guarantees that host pointers
/// remain valid across threads for the lifetime of the cluster.
struct HostList(Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>);
// SAFETY: Host pointers are stable addresses managed by Envoy across threads.
unsafe impl Send for HostList {}
unsafe impl Sync for HostList {}

type SharedHostList = Arc<Mutex<HostList>>;

fn new_cluster_config(
  name: &str,
  config: &[u8],
  envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics>,
) -> Option<Box<dyn ClusterConfig>> {
  let config_str = std::str::from_utf8(config).unwrap_or("");
  match name {
    "sync_host_selection" => Some(Box::new(SyncHostSelectionClusterConfig {
      upstream_address: config_str.to_string(),
      metrics: envoy_cluster_metrics,
    })),
    "async_host_selection" => Some(Box::new(AsyncHostSelectionClusterConfig {
      upstream_address: config_str.to_string(),
    })),
    "scheduler_host_update" => Some(Box::new(SchedulerHostUpdateClusterConfig {
      upstream_address: config_str.to_string(),
    })),
    "lifecycle_callbacks" => Some(Box::new(LifecycleCallbacksClusterConfig {
      upstream_address: config_str.to_string(),
    })),
    _ => None,
  }
}

// =============================================================================
// Synchronous host selection.
// =============================================================================

struct SyncHostSelectionClusterConfig {
  upstream_address: String,
  metrics: Arc<dyn EnvoyClusterMetrics>,
}

impl ClusterConfig for SyncHostSelectionClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    let counter_id = self.metrics.define_counter("requests_routed").ok();
    Box::new(SyncHostSelectionCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
      counter_id,
      metrics: self.metrics.clone(),
    })
  }
}

struct SyncHostSelectionCluster {
  upstream_address: String,
  hosts: SharedHostList,
  counter_id: Option<EnvoyCounterId>,
  metrics: Arc<dyn EnvoyClusterMetrics>,
}

impl Cluster for SyncHostSelectionCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(SyncHostSelectionLb {
      hosts: self.hosts.clone(),
      index: AtomicUsize::new(0),
      counter_id: self.counter_id,
      metrics: self.metrics.clone(),
    })
  }
}

struct SyncHostSelectionLb {
  hosts: SharedHostList,
  index: AtomicUsize,
  counter_id: Option<EnvoyCounterId>,
  metrics: Arc<dyn EnvoyClusterMetrics>,
}

impl ClusterLb for SyncHostSelectionLb {
  fn choose_host(
    &mut self,
    _context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    let idx = self.index.fetch_add(1, Ordering::Relaxed) % hosts.0.len();
    if let Some(counter_id) = self.counter_id {
      let _ = self.metrics.increment_counter(counter_id, 1);
    }
    HostSelectionResult::Selected(hosts.0[idx])
  }
}

// =============================================================================
// Asynchronous host selection via background thread.
// =============================================================================

struct AsyncHostSelectionClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for AsyncHostSelectionClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(AsyncHostSelectionCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct AsyncHostSelectionCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

impl Cluster for AsyncHostSelectionCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(AsyncHostSelectionLb {
      hosts: self.hosts.clone(),
    })
  }
}

/// Wraps async completion arguments so they can be sent to a background thread.
struct AsyncCompletionTask {
  completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  host: abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
}
// SAFETY: The host pointer is a stable Envoy address valid across threads, and the
// completion callback is Send (as required by the EnvoyAsyncHostSelectionComplete trait bound).
unsafe impl Send for AsyncCompletionTask {}

impl AsyncCompletionTask {
  fn run(self) {
    self
      .completion
      .async_host_selection_complete(Some(self.host), "async_resolved");
  }
}

struct AsyncHostSelectionLb {
  hosts: SharedHostList,
}

impl ClusterLb for AsyncHostSelectionLb {
  fn choose_host(
    &mut self,
    _context: Option<&dyn ClusterLbContext>,
    async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    let task = AsyncCompletionTask {
      completion: async_completion,
      host: hosts.0[0],
    };
    // Spawn a background thread to complete the host selection asynchronously.
    // The ABI implementation posts the completion to the correct worker thread.
    std::thread::spawn(move || task.run());
    HostSelectionResult::AsyncPending(Box::new(AsyncHandle {
      cancelled: Arc::new(AtomicBool::new(false)),
    }))
  }
}

struct AsyncHandle {
  cancelled: Arc<AtomicBool>,
}

impl AsyncHostSelectionHandle for AsyncHandle {
  fn cancel(&mut self) {
    self.cancelled.store(true, Ordering::Relaxed);
  }
}

// =============================================================================
// Scheduler-based host updates.
// =============================================================================

struct SchedulerHostUpdateClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for SchedulerHostUpdateClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(SchedulerHostUpdateCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct SchedulerHostUpdateCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

const ADD_HOST_EVENT_ID: u64 = 100;

impl Cluster for SchedulerHostUpdateCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    envoy_cluster.pre_init_complete();
    let scheduler = envoy_cluster.new_scheduler();
    scheduler.commit(ADD_HOST_EVENT_ID);
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(SchedulerHostUpdateLb {
      hosts: self.hosts.clone(),
      membership_update_count: AtomicUsize::new(0),
    })
  }

  fn on_scheduled(&self, envoy_cluster: &dyn EnvoyCluster, event_id: u64) {
    if event_id == ADD_HOST_EVENT_ID {
      let addresses = vec![self.upstream_address.clone()];
      let weights = vec![1u32];
      if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
        self.hosts.lock().unwrap().0 = host_ptrs;
      }
    }
  }
}

struct SchedulerHostUpdateLb {
  hosts: SharedHostList,
  membership_update_count: AtomicUsize,
}

impl ClusterLb for SchedulerHostUpdateLb {
  fn choose_host(
    &mut self,
    _context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    HostSelectionResult::Selected(hosts.0[0])
  }

  fn on_host_membership_update(
    &mut self,
    _envoy_lb: &dyn EnvoyClusterLoadBalancer,
    _num_hosts_added: usize,
    _num_hosts_removed: usize,
  ) {
    self.membership_update_count.fetch_add(1, Ordering::Relaxed);
  }
}

// =============================================================================
// Lifecycle callbacks verification.
// =============================================================================

struct LifecycleCallbacksClusterConfig {
  upstream_address: String,
}

impl ClusterConfig for LifecycleCallbacksClusterConfig {
  fn new_cluster(&self, _envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster> {
    Box::new(LifecycleCallbacksCluster {
      upstream_address: self.upstream_address.clone(),
      hosts: Arc::new(Mutex::new(HostList(Vec::new()))),
    })
  }
}

struct LifecycleCallbacksCluster {
  upstream_address: String,
  hosts: SharedHostList,
}

impl Cluster for LifecycleCallbacksCluster {
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster) {
    envoy_log_info!("cluster lifecycle: on_init called");
    let addresses = vec![self.upstream_address.clone()];
    let weights = vec![1u32];
    if let Some(host_ptrs) = envoy_cluster.add_hosts(&addresses, &weights) {
      self.hosts.lock().unwrap().0 = host_ptrs;
    }
    envoy_cluster.pre_init_complete();
  }

  fn new_load_balancer(&self, _envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb> {
    Box::new(LifecycleCallbacksLb {
      hosts: self.hosts.clone(),
    })
  }

  fn on_server_initialized(&mut self, _envoy_cluster: &dyn EnvoyCluster) {
    envoy_log_info!("cluster lifecycle: on_server_initialized called");
  }

  fn on_drain_started(&mut self, _envoy_cluster: &dyn EnvoyCluster) {
    envoy_log_info!("cluster lifecycle: on_drain_started called");
  }

  fn on_shutdown(&mut self, _envoy_cluster: &dyn EnvoyCluster, completion: CompletionCallback) {
    envoy_log_info!("cluster lifecycle: on_shutdown called");
    completion.done();
  }
}

struct LifecycleCallbacksLb {
  hosts: SharedHostList,
}

impl ClusterLb for LifecycleCallbacksLb {
  fn choose_host(
    &mut self,
    _context: Option<&dyn ClusterLbContext>,
    _async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult {
    let hosts = self.hosts.lock().unwrap();
    if hosts.0.is_empty() {
      return HostSelectionResult::NoHost;
    }
    HostSelectionResult::Selected(hosts.0[0])
  }
}
