use crate::{
  abi,
  drop_wrapped_c_void_ptr,
  str_to_module_buffer,
  strs_to_module_buffers,
  wrap_into_c_void_ptr,
  CompletionCallback,
  EnvoyCounterId,
  EnvoyCounterVecId,
  EnvoyGaugeId,
  EnvoyGaugeVecId,
  EnvoyHistogramId,
  EnvoyHistogramVecId,
  NEW_CLUSTER_CONFIG_FUNCTION,
};
use mockall::*;
use std::sync::Arc;

/// The module-side cluster configuration.
///
/// This trait must be implemented by the module to handle cluster configuration.
/// The object is created when the corresponding Envoy cluster configuration is loaded, and
/// it is dropped when the corresponding Envoy cluster configuration is destroyed.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait ClusterConfig: Send + Sync {
  /// Create a new cluster instance.
  ///
  /// This is called when a new cluster is created from this configuration.
  /// The `envoy_cluster` provides access to Envoy's cluster operations such as
  /// adding/removing hosts.
  fn new_cluster(&self, envoy_cluster: &dyn EnvoyCluster) -> Box<dyn Cluster>;
}

/// The module-side cluster instance.
///
/// This trait must be implemented by the module to handle cluster lifecycle events.
/// The object is created per cluster and is responsible for host discovery.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait Cluster: Send + Sync {
  /// Called when cluster initialization begins.
  ///
  /// The module should perform initial host discovery (e.g., add hosts via
  /// [`EnvoyCluster::add_hosts`]) and then call [`EnvoyCluster::pre_init_complete`]
  /// to signal that the initial set of hosts is ready.
  fn on_init(&mut self, envoy_cluster: &dyn EnvoyCluster);

  /// Create a new load balancer instance for a worker thread.
  ///
  /// Each worker thread gets its own load balancer instance. The `envoy_lb`
  /// provides thread-local access to the cluster's host set.
  fn new_load_balancer(&self, envoy_lb: &dyn EnvoyClusterLoadBalancer) -> Box<dyn ClusterLb>;

  /// Called on the main thread when a new event is scheduled via
  /// [`EnvoyClusterScheduler::commit`] for this [`Cluster`].
  ///
  /// * `envoy_cluster` can be used to interact with the underlying Envoy cluster object.
  /// * `event_id` is the ID of the event that was scheduled with [`EnvoyClusterScheduler::commit`]
  ///   to distinguish multiple scheduled events.
  fn on_scheduled(&self, _envoy_cluster: &dyn EnvoyCluster, _event_id: u64) {}

  /// Called when the server initialization is complete (PostInit lifecycle stage).
  ///
  /// This is called on the main thread after all clusters have finished initialization and
  /// before workers are started. This is the appropriate place to start background discovery
  /// tasks or establish connections that depend on the server being fully operational.
  fn on_server_initialized(&mut self, _envoy_cluster: &dyn EnvoyCluster) {}

  /// Called when Envoy begins draining.
  ///
  /// This is called on the main thread before workers are stopped. The module can still use
  /// cluster operations during drain. This is the appropriate place to stop accepting new hosts,
  /// close persistent connections, or de-register from service discovery.
  fn on_drain_started(&mut self, _envoy_cluster: &dyn EnvoyCluster) {}

  /// Called when Envoy is about to exit (ShutdownExit lifecycle stage).
  ///
  /// The module must invoke [`CompletionCallback::done`] exactly once when it has finished
  /// cleanup. Envoy will wait for the callback before terminating. This is the appropriate
  /// place to flush batched data, close gRPC connections, or signal external systems.
  fn on_shutdown(&mut self, _envoy_cluster: &dyn EnvoyCluster, completion: CompletionCallback) {
    completion.done();
  }
}

/// The module-side load balancer instance.
///
/// This trait must be implemented by the module to select hosts for requests.
/// One instance is created per worker thread.
pub trait ClusterLb: Send {
  /// Select a host for a request.
  ///
  /// Returns the raw host pointer obtained from [`EnvoyClusterLoadBalancer::get_healthy_host`],
  /// or `None` if no host is available.
  fn choose_host(
    &mut self,
    context: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;
}

/// Envoy-side cluster operations available to the module.
#[automock]
pub trait EnvoyCluster: Send + Sync {
  /// Add multiple hosts to the cluster in a single batch operation.
  ///
  /// Each address must be in `ip:port` format (e.g., `127.0.0.1:8080`).
  /// Each weight must be between 1 and 128. The `addresses` and `weights` slices must have the
  /// same length.
  ///
  /// This triggers only one priority set update regardless of how many hosts are added, avoiding
  /// the overhead of updating the priority set per host.
  ///
  /// Returns the host pointers if all hosts were added successfully, or `None` if any host failed
  /// (e.g., invalid address or weight). On failure, no hosts are added.
  fn add_hosts(
    &self,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>>;

  /// Remove multiple hosts from the cluster in a single batch operation.
  ///
  /// The host pointers must have been returned by a previous [`EnvoyCluster::add_hosts`] call.
  ///
  /// This triggers only one priority set update regardless of how many hosts are removed.
  ///
  /// Returns the number of hosts that were successfully removed. Hosts not found in the cluster
  /// are skipped.
  fn remove_hosts(&self, hosts: &[abi::envoy_dynamic_module_type_cluster_host_envoy_ptr]) -> usize;

  /// Signal that the cluster's initial host discovery is complete.
  ///
  /// This must be called during or after [`Cluster::on_init`] to allow Envoy to start
  /// routing traffic to this cluster.
  fn pre_init_complete(&self);

  /// Create a new implementation of the [`EnvoyClusterScheduler`] trait.
  ///
  /// This can be used to schedule an event to the main thread where the cluster is running.
  fn new_scheduler(&self) -> Box<dyn EnvoyClusterScheduler>;
}

/// Envoy-side load balancer operations available to the module.
#[automock]
pub trait EnvoyClusterLoadBalancer: Send {
  /// Get the number of healthy hosts at the given priority level.
  fn get_healthy_host_count(&self, priority: u32) -> usize;

  /// Get a healthy host by index at the given priority level.
  ///
  /// Returns the host pointer, or `None` if the index is out of bounds.
  fn get_healthy_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;
}

/// Envoy-side scheduler that dispatches events to the main thread.
///
/// The scheduler can be used from any thread. When [`EnvoyClusterScheduler::commit`] is called,
/// the event is posted to the main thread dispatcher and [`Cluster::on_scheduled`] will be
/// invoked on the main thread with the corresponding `event_id`.
#[automock]
pub trait EnvoyClusterScheduler: Send + Sync {
  /// Commit the scheduled event to the main thread.
  fn commit(&self, event_id: u64);
}

/// Envoy-side metrics interface for the cluster dynamic module.
///
/// This trait provides the ability to define and record custom metrics (counters, gauges,
/// histograms) scoped to the cluster configuration. Metrics should be defined during
/// config creation and can be recorded at any point during the cluster lifecycle.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
#[automock]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyClusterMetrics: Send + Sync {
  // -------------------------------------------------------------------------
  // Define metrics (call during config creation).
  // -------------------------------------------------------------------------

  /// Define a new counter with the given name and no labels.
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec with the given name and label names.
  fn define_counter_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge with the given name and no labels.
  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec with the given name and label names.
  fn define_gauge_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram with the given name and no labels.
  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec with the given name and label names.
  fn define_histogram_vec<'a>(
    &self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result>;

  // -------------------------------------------------------------------------
  // Record metrics (call at runtime, e.g., during cluster lifecycle).
  // -------------------------------------------------------------------------

  /// Increment a previously defined counter by the given value.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increment a previously defined counter vec by the given value with label values.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge vec with label values.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge by the given value.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge vec by the given value with label values.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge by the given value.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge vec by the given value with label values.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in a previously defined histogram.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in a previously defined histogram vec with label values.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;
}

struct EnvoyClusterSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_cluster_scheduler_module_ptr,
}

unsafe impl Send for EnvoyClusterSchedulerImpl {}
unsafe impl Sync for EnvoyClusterSchedulerImpl {}

impl Drop for EnvoyClusterSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyClusterScheduler for EnvoyClusterSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

impl EnvoyClusterScheduler for Box<dyn EnvoyClusterScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

// Implementations

struct EnvoyClusterImpl {
  raw: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
}

unsafe impl Send for EnvoyClusterImpl {}
unsafe impl Sync for EnvoyClusterImpl {}

impl EnvoyClusterImpl {
  fn new(raw: abi::envoy_dynamic_module_type_cluster_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyCluster for EnvoyClusterImpl {
  fn add_hosts(
    &self,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>> {
    let count = addresses.len();
    let address_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> =
      addresses.iter().map(|a| str_to_module_buffer(a)).collect();
    let mut result_ptrs: Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> =
      vec![std::ptr::null_mut(); count];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_cluster_add_hosts(
        self.raw,
        address_buffers.as_ptr(),
        weights.as_ptr(),
        count,
        result_ptrs.as_mut_ptr(),
      )
    };
    if success {
      Some(result_ptrs)
    } else {
      None
    }
  }

  fn remove_hosts(&self, hosts: &[abi::envoy_dynamic_module_type_cluster_host_envoy_ptr]) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_remove_hosts(self.raw, hosts.as_ptr(), hosts.len())
    }
  }

  fn pre_init_complete(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_pre_init_complete(self.raw);
    }
  }

  fn new_scheduler(&self) -> Box<dyn EnvoyClusterScheduler> {
    unsafe {
      let scheduler_ptr = abi::envoy_dynamic_module_callback_cluster_scheduler_new(self.raw);
      Box::new(EnvoyClusterSchedulerImpl {
        raw_ptr: scheduler_ptr,
      })
    }
  }
}

struct EnvoyClusterLoadBalancerImpl {
  raw: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
}

unsafe impl Send for EnvoyClusterLoadBalancerImpl {}

impl EnvoyClusterLoadBalancerImpl {
  fn new(raw: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyClusterLoadBalancer for EnvoyClusterLoadBalancerImpl {
  fn get_healthy_host_count(&self, priority: u32) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(self.raw, priority)
    }
  }

  fn get_healthy_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> {
    let host = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host(self.raw, priority, index)
    };
    if host.is_null() {
      None
    } else {
      Some(host)
    }
  }
}

/// Implementation of [`EnvoyClusterMetrics`] that calls into the Envoy ABI.
pub struct EnvoyClusterMetricsImpl {
  raw: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
}

// The raw pointer references C++ DynamicModuleClusterConfig which is safe for metric operations
// from any thread.
unsafe impl Send for EnvoyClusterMetricsImpl {}
unsafe impl Sync for EnvoyClusterMetricsImpl {}

fn cluster_metric_result_to_rust(
  res: abi::envoy_dynamic_module_type_metrics_result,
) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
  if res == abi::envoy_dynamic_module_type_metrics_result::Success {
    Ok(())
  } else {
    Err(res)
  }
}

impl EnvoyClusterMetrics for EnvoyClusterMetricsImpl {
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_counter_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_gauge_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn define_histogram_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_bufs = strs_to_module_buffers(labels);
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        label_bufs.as_mut_ptr(),
        labels.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_increment_counter(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_increment_counter(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_set_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_set_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_increment_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_increment_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_decrement_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_decrement_gauge(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_record_histogram_value(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramVecId(id) = id;
    let mut label_bufs = strs_to_module_buffers(labels);
    cluster_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_cluster_config_record_histogram_value(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }
}

// Cluster Event Hook Implementations

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_config_new(
  config_envoy_ptr: abi::envoy_dynamic_module_type_cluster_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_cluster_config_module_ptr {
  // SAFETY: Envoy guarantees name and config are valid UTF-8 per the ABI contract.
  let name_str = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
    name.ptr as *const _,
    name.length,
  ));
  let config_slice = std::slice::from_raw_parts(config.ptr as *const _, config.length);
  let new_config_fn = NEW_CLUSTER_CONFIG_FUNCTION
    .get()
    .expect("NEW_CLUSTER_CONFIG_FUNCTION must be set");
  let envoy_cluster_metrics: Arc<dyn EnvoyClusterMetrics> = Arc::new(EnvoyClusterMetricsImpl {
    raw: config_envoy_ptr,
  });
  match new_config_fn(name_str, config_slice, envoy_cluster_metrics) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_cluster_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_module_ptr, ClusterConfig);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_new(
  config_module_ptr: abi::envoy_dynamic_module_type_cluster_config_module_ptr,
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_module_ptr {
  let config = config_module_ptr as *const *const dyn ClusterConfig;
  let config = &**config;
  let envoy_cluster = EnvoyClusterImpl::new(cluster_envoy_ptr);
  let cluster = config.new_cluster(&envoy_cluster);
  wrap_into_c_void_ptr!(cluster)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_init(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = unsafe { &mut *cluster };
  let envoy_cluster = EnvoyClusterImpl::new(cluster_envoy_ptr);
  cluster.on_init(&envoy_cluster);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_destroy(
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  drop_wrapped_c_void_ptr!(cluster_module_ptr, Cluster);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_new(
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_lb_module_ptr {
  let cluster = cluster_module_ptr as *const *const dyn Cluster;
  let cluster = &**cluster;
  let envoy_lb = EnvoyClusterLoadBalancerImpl::new(lb_envoy_ptr);
  let lb = cluster.new_load_balancer(&envoy_lb);
  wrap_into_c_void_ptr!(lb)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_destroy(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
) {
  drop_wrapped_c_void_ptr!(lb_module_ptr, ClusterLb);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_lb_choose_host(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
) -> abi::envoy_dynamic_module_type_cluster_host_envoy_ptr {
  let lb = lb_module_ptr as *mut Box<dyn ClusterLb>;
  let lb = unsafe { &mut *lb };
  match lb.choose_host(context_envoy_ptr) {
    Some(host) => host,
    None => std::ptr::null_mut(),
  }
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_scheduled(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  event_id: u64,
) {
  let cluster = cluster_module_ptr as *const *const dyn Cluster;
  let cluster = unsafe { &**cluster };
  cluster.on_scheduled(&EnvoyClusterImpl::new(cluster_envoy_ptr), event_id);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_server_initialized(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = unsafe { &mut *cluster };
  cluster.on_server_initialized(&EnvoyClusterImpl::new(cluster_envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_drain_started(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = unsafe { &mut *cluster };
  cluster.on_drain_started(&EnvoyClusterImpl::new(cluster_envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_cluster_shutdown(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  completion_callback: abi::envoy_dynamic_module_type_event_cb,
  completion_context: *mut std::os::raw::c_void,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = unsafe { &mut *cluster };
  let completion = CompletionCallback::new(completion_callback, completion_context);
  cluster.on_shutdown(&EnvoyClusterImpl::new(cluster_envoy_ptr), completion);
}
