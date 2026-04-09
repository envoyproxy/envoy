use crate::buffer::EnvoyBuffer;
use crate::{
  abi, drop_wrapped_c_void_ptr, str_to_module_buffer, strs_to_module_buffers, wrap_into_c_void_ptr,
  CompletionCallback, EnvoyCounterId, EnvoyCounterVecId, EnvoyGaugeId, EnvoyGaugeVecId,
  EnvoyHistogramId, EnvoyHistogramVecId, NEW_CLUSTER_CONFIG_FUNCTION,
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

  /// Called on the main thread when an HTTP callout initiated by
  /// [`EnvoyCluster::send_http_callout`] receives a response or fails.
  ///
  /// * `envoy_cluster` can be used to interact with the underlying Envoy cluster object.
  /// * `callout_id` is the ID of the callout returned by [`EnvoyCluster::send_http_callout`].
  /// * `result` is the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers. This is optional.
  /// * `response_body` is the response body chunks. This is optional.
  fn on_http_callout_done(
    &mut self,
    _envoy_cluster: &dyn EnvoyCluster,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
  }
}

/// The result of a host selection operation.
///
/// This enum represents the three possible outcomes of [`ClusterLb::choose_host`]:
/// synchronous success, synchronous failure, or async pending.
pub enum HostSelectionResult {
  /// A host was selected synchronously.
  Selected(abi::envoy_dynamic_module_type_cluster_host_envoy_ptr),
  /// No host is available and no async resolution will occur.
  NoHost,
  /// The module needs to perform async work (e.g., DNS resolution) before selecting a host.
  /// The module must eventually call
  /// [`EnvoyAsyncHostSelectionComplete::async_host_selection_complete`] to deliver the result,
  /// unless [`AsyncHostSelectionHandle::cancel`] is called first.
  AsyncPending(Box<dyn AsyncHostSelectionHandle>),
}

/// A handle for canceling an in-progress asynchronous host selection.
///
/// When the stream is destroyed before async host selection completes (e.g., due to a timeout),
/// Envoy calls [`AsyncHostSelectionHandle::cancel`]. After cancellation, the module must not
/// call the async completion callback for this operation.
pub trait AsyncHostSelectionHandle: Send {
  /// Cancel the async host selection. After this call, the module must not deliver a result
  /// for this operation.
  fn cancel(&mut self);
}

/// Envoy-side async host selection completion callback.
///
/// This is passed to [`ClusterLb::choose_host`] and must be stored by the module when returning
/// [`HostSelectionResult::AsyncPending`]. The module calls
/// [`EnvoyAsyncHostSelectionComplete::async_host_selection_complete`] to deliver the async result.
#[automock]
pub trait EnvoyAsyncHostSelectionComplete: Send {
  /// Deliver the result of an asynchronous host selection.
  ///
  /// `host` is the selected host pointer, or `None` if host selection failed.
  /// `details` is an optional description of the resolution outcome (e.g., error reason).
  fn async_host_selection_complete(
    &self,
    host: Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>,
    details: &str,
  );
}

/// The module-side load balancer instance.
///
/// This trait must be implemented by the module to select hosts for requests.
/// One instance is created per worker thread.
pub trait ClusterLb: Send {
  /// Select a host for a request.
  ///
  /// The `context` provides access to per-request information such as downstream headers,
  /// hash keys, override host, and retry state. It may be `None` if no context is available
  /// (e.g., health check requests).
  ///
  /// The `async_completion` callback must be used when returning
  /// [`HostSelectionResult::AsyncPending`]. The module stores it and later calls
  /// [`EnvoyAsyncHostSelectionComplete::async_host_selection_complete`] to deliver the result.
  /// For synchronous results, `async_completion` can be ignored.
  fn choose_host(
    &mut self,
    context: Option<&dyn ClusterLbContext>,
    async_completion: Box<dyn EnvoyAsyncHostSelectionComplete>,
  ) -> HostSelectionResult;

  /// Called when the set of hosts in the cluster changes.
  ///
  /// The `envoy_lb` provides access to the updated host set and to the addresses of hosts
  /// that were added or removed via
  /// [`EnvoyClusterLoadBalancer::get_member_update_host_address`].
  ///
  /// After this callback returns, the standard host query methods reflect the new state.
  ///
  /// Override this to rebuild internal data structures (e.g., hash rings, address-to-index
  /// maps) when the host set changes. The default implementation is a no-op.
  fn on_host_membership_update(
    &mut self,
    _envoy_lb: &dyn EnvoyClusterLoadBalancer,
    _num_hosts_added: usize,
    _num_hosts_removed: usize,
  ) {
  }
}

/// Per-request context available during [`ClusterLb::choose_host`].
///
/// This provides access to downstream request information for making load balancing decisions
/// such as header-based routing, consistent hashing, and retry-aware host selection.
#[automock]
pub trait ClusterLbContext {
  /// Compute a hash key from the request context for consistent hashing.
  ///
  /// Returns `Some(hash)` if a hash key was computed, `None` otherwise.
  fn compute_hash_key(&self) -> Option<u64>;

  /// Returns the number of downstream request headers.
  fn get_downstream_headers_size(&self) -> usize;

  /// Returns all downstream request headers as a vector of (key, value) pairs.
  ///
  /// Returns `None` if no headers are available.
  fn get_downstream_headers(&self) -> Option<Vec<(String, String)>>;

  /// Returns a downstream request header value by key and index.
  ///
  /// Since a header key can have multiple values, the `index` parameter selects a specific value.
  /// Returns `Some((value, total_count))` where `total_count` is the number of values for the key,
  /// or `None` if the header was not found at the given index.
  fn get_downstream_header(&self, key: &str, index: usize) -> Option<(String, usize)>;

  /// Returns the maximum number of times host selection should be retried if the chosen host
  /// is rejected by [`ClusterLbContext::should_select_another_host`].
  fn get_host_selection_retry_count(&self) -> u32;

  /// Checks whether the load balancer should reject the given host and retry selection.
  ///
  /// This is used during retries to avoid selecting hosts that were already attempted.
  /// The host is identified by priority and index within the healthy host list at that priority.
  fn should_select_another_host(&self, priority: u32, index: usize) -> bool;

  /// Returns the override host address and strict mode flag from the context.
  ///
  /// Override host allows upstream filters to direct the load balancer to prefer a specific host
  /// by address. Returns `Some((address, strict))` if an override host is set, `None` otherwise.
  /// When `strict` is true, the load balancer should return no host if the override is not valid.
  fn get_override_host(&self) -> Option<(String, bool)>;

  /// Returns the requested server name (SNI) from the downstream connection.
  ///
  /// Returns `None` if the downstream connection or SNI is not available.
  fn get_downstream_connection_sni(&self) -> Option<String>;
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

  /// Add multiple hosts to the cluster with per-host locality and metadata.
  ///
  /// Each address must be in `ip:port` format (e.g., `127.0.0.1:8080`).
  /// Each weight must be between 1 and 128. All per-host slices must have the same length.
  ///
  /// `localities` specifies (region, zone, sub_zone) for each host. An empty string indicates
  /// no value for that field.
  ///
  /// `metadata` specifies endpoint metadata as (filter_name, key, value) triples per host.
  /// All values are stored as strings. Pass an empty inner slice for hosts with no metadata.
  /// The number of triples per host must be the same for all hosts (pad with empty triples
  /// if needed) or the outer slice can be empty to skip metadata entirely.
  ///
  /// Returns the host pointers on success, or `None` if any host failed.
  fn add_hosts_with_locality(
    &self,
    addresses: &[String],
    weights: &[u32],
    localities: &[(String, String, String)],
    metadata: &[Vec<(String, String, String)>],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>>;

  /// Add multiple hosts to the cluster at the specified priority level in a single batch operation.
  ///
  /// This is the priority-aware version of [`EnvoyCluster::add_hosts`]. Only modules that manage
  /// hosts across multiple priority levels need to use this.
  ///
  /// Each address must be in `ip:port` format (e.g., `127.0.0.1:8080`).
  /// Each weight must be between 1 and 128.
  ///
  /// Returns the host pointers if all hosts were added successfully, or `None` if any host failed.
  fn add_hosts_to_priority(
    &self,
    priority: u32,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>>;

  /// Add multiple hosts to the cluster at the specified priority level with per-host locality and
  /// metadata in a single batch operation.
  ///
  /// This is the priority-aware version of [`EnvoyCluster::add_hosts_with_locality`]. Only modules
  /// that manage hosts across multiple priority levels need to use this.
  ///
  /// Each address must be in `ip:port` format. Each weight must be between 1 and 128.
  ///
  /// Returns the host pointers on success, or `None` if any host failed.
  fn add_hosts_with_locality_to_priority(
    &self,
    priority: u32,
    addresses: &[String],
    weights: &[u32],
    localities: &[(String, String, String)],
    metadata: &[Vec<(String, String, String)>],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>>;

  /// Update the health status of a host.
  ///
  /// This allows the module to mark hosts as unhealthy, degraded, or healthy based on
  /// external health information (e.g., from a custom service discovery system).
  ///
  /// Returns true if the host was found and updated, false otherwise.
  fn update_host_health(
    &self,
    host: abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
    health_status: abi::envoy_dynamic_module_type_host_health,
  ) -> bool;

  /// Look up a host by its address string across all priorities and return the host pointer.
  ///
  /// This provides O(1) lookup by address using the cross-priority host map.
  /// The address must match the format "ip:port" (e.g., "10.0.0.1:8080").
  ///
  /// Returns the host pointer if found, or `None` if the address is not in the cluster.
  fn find_host_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;

  /// Create a new implementation of the [`EnvoyClusterScheduler`] trait.
  ///
  /// This can be used to schedule an event to the main thread where the cluster is running.
  fn new_scheduler(&self) -> Box<dyn EnvoyClusterScheduler>;

  /// Sends an HTTP request to the specified cluster and asynchronously delivers the response
  /// via [`Cluster::on_http_callout_done`].
  ///
  /// This must be called on the main thread. The request requires `:method`, `:path`, and `host`
  /// headers to be present. To call from other threads, use the scheduler mechanism to post an
  /// event to the main thread first.
  ///
  /// Returns a tuple of the callout initialization result and the callout ID. The callout ID is
  /// only valid if the result is `Success`.
  fn send_http_callout<'a>(
    &self,
    cluster_name: &'a str,
    headers: &[(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64);
}

/// Envoy-side load balancer operations available to the module.
///
/// This trait provides access to the cluster's host set for load balancing decisions.
/// It mirrors the standalone load balancer's [`EnvoyLoadBalancer`] trait, operating on the
/// cluster's priority set.
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

  /// Get a host by index within all hosts at the given priority level, regardless of health status.
  ///
  /// Unlike [`EnvoyClusterLoadBalancer::get_healthy_host`] which only returns healthy hosts, this
  /// returns any host at the given index in the full host list.
  ///
  /// Returns the host pointer, or `None` if the index is out of bounds.
  fn get_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;

  /// Look up a host by its address string across all priorities in the cluster's priority set.
  ///
  /// This uses the cross-priority host map internally, providing O(1) lookup by address. The
  /// address must match the format "ip:port" (e.g., "10.0.0.1:8080").
  ///
  /// Unlike [`EnvoyCluster::find_host_by_address`] which operates on the main thread, this is
  /// safe to call from worker threads during load balancing decisions.
  ///
  /// Returns the host pointer if found, or `None` if the address is not in the cluster.
  fn find_host_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>;

  /// Returns the cluster name.
  fn get_cluster_name(&self) -> String;

  /// Returns the number of all hosts at a given priority, regardless of health status.
  fn get_hosts_count(&self, priority: u32) -> usize;

  /// Returns the number of degraded hosts at a given priority.
  fn get_degraded_hosts_count(&self, priority: u32) -> usize;

  /// Returns the number of priority levels in the cluster.
  fn get_priority_set_size(&self) -> usize;

  /// Returns the address of a healthy host by index at a given priority.
  fn get_healthy_host_address(&self, priority: u32, index: usize) -> Option<String>;

  /// Returns the weight of a healthy host by index at a given priority.
  fn get_healthy_host_weight(&self, priority: u32, index: usize) -> u32;

  /// Returns the health status of a host by index within all hosts at a given priority.
  fn get_host_health(
    &self,
    priority: u32,
    index: usize,
  ) -> abi::envoy_dynamic_module_type_host_health;

  /// Looks up a host by its address string across all priorities and returns its health status.
  /// This provides O(1) lookup by address using the cross-priority host map.
  ///
  /// The address must match the format "ip:port" (e.g., "10.0.0.1:8080").
  fn get_host_health_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_host_health>;

  /// Returns the address of a host by index within all hosts at a given priority.
  fn get_host_address(&self, priority: u32, index: usize) -> Option<String>;

  /// Returns the weight of a host by index within all hosts at a given priority.
  fn get_host_weight(&self, priority: u32, index: usize) -> u32;

  /// Returns the value of a per-host stat. This provides access to host-level counters and gauges
  /// such as total connections, request errors, active requests, and active connections.
  fn get_host_stat(
    &self,
    priority: u32,
    index: usize,
    stat: abi::envoy_dynamic_module_type_host_stat,
  ) -> u64;

  /// Returns the locality information (region, zone, sub_zone) for a host by index within all
  /// hosts at a given priority. This enables zone-aware and locality-aware load balancing.
  fn get_host_locality(&self, priority: u32, index: usize) -> Option<(String, String, String)>;

  /// Stores an opaque value on a host identified by priority and index. This data is stored per
  /// load balancer instance (per worker thread) and can be used for per-host state such as moving
  /// averages or request tracking. Use 0 to clear the data.
  fn set_host_data(&self, priority: u32, index: usize, data: usize) -> bool;

  /// Retrieves a previously stored opaque value for a host. Returns `None` if the host was not
  /// found. Returns `Some(0)` if the host exists but no data was stored.
  fn get_host_data(&self, priority: u32, index: usize) -> Option<usize>;

  /// Returns the string metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// string.
  fn get_host_metadata_string(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<String>;

  /// Returns the number metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// number.
  fn get_host_metadata_number(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<f64>;

  /// Returns the bool metadata value for a host by looking up the given filter name and key in
  /// the host's endpoint metadata. Returns `None` if the key was not found or the value is not a
  /// bool.
  fn get_host_metadata_bool(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<bool>;

  /// Returns the number of locality buckets for the healthy hosts at a given priority.
  fn get_locality_count(&self, priority: u32) -> usize;

  /// Returns the number of healthy hosts in a specific locality bucket at a given priority.
  fn get_locality_host_count(&self, priority: u32, locality_index: usize) -> usize;

  /// Returns the address of a host within a specific locality bucket at a given priority.
  fn get_locality_host_address(
    &self,
    priority: u32,
    locality_index: usize,
    host_index: usize,
  ) -> Option<String>;

  /// Returns the weight of a locality bucket at a given priority.
  fn get_locality_weight(&self, priority: u32, locality_index: usize) -> u32;

  /// Returns the address of an added or removed host during the
  /// [`ClusterLb::on_host_membership_update`] callback.
  ///
  /// This is only valid during the `on_host_membership_update` callback.
  ///
  /// Set `is_added` to `true` to get an added host address, `false` for a removed host address.
  fn get_member_update_host_address(&self, index: usize, is_added: bool) -> Option<String>;
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
    let empty_localities: Vec<(String, String, String)> = addresses
      .iter()
      .map(|_| (String::new(), String::new(), String::new()))
      .collect();
    self.add_hosts_with_locality_to_priority(0, addresses, weights, &empty_localities, &[])
  }

  fn add_hosts_with_locality(
    &self,
    addresses: &[String],
    weights: &[u32],
    localities: &[(String, String, String)],
    metadata: &[Vec<(String, String, String)>],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>> {
    self.add_hosts_with_locality_to_priority(0, addresses, weights, localities, metadata)
  }

  fn add_hosts_to_priority(
    &self,
    priority: u32,
    addresses: &[String],
    weights: &[u32],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>> {
    let empty_localities: Vec<(String, String, String)> = addresses
      .iter()
      .map(|_| (String::new(), String::new(), String::new()))
      .collect();
    self.add_hosts_with_locality_to_priority(priority, addresses, weights, &empty_localities, &[])
  }

  fn add_hosts_with_locality_to_priority(
    &self,
    priority: u32,
    addresses: &[String],
    weights: &[u32],
    localities: &[(String, String, String)],
    metadata: &[Vec<(String, String, String)>],
  ) -> Option<Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>> {
    let count = addresses.len();
    let address_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> =
      addresses.iter().map(|a| str_to_module_buffer(a)).collect();
    let region_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> = localities
      .iter()
      .map(|(r, ..)| str_to_module_buffer(r))
      .collect();
    let zone_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> = localities
      .iter()
      .map(|(_, z, _)| str_to_module_buffer(z))
      .collect();
    let sub_zone_buffers: Vec<abi::envoy_dynamic_module_type_module_buffer> = localities
      .iter()
      .map(|(_, _, s)| str_to_module_buffer(s))
      .collect();

    let metadata_pairs_per_host = if metadata.is_empty() {
      0
    } else {
      metadata[0].len()
    };
    let mut metadata_flat: Vec<abi::envoy_dynamic_module_type_module_buffer> = Vec::new();
    if !metadata.is_empty() {
      for host_meta in metadata {
        for (filter_name, key, value) in host_meta {
          metadata_flat.push(str_to_module_buffer(filter_name));
          metadata_flat.push(str_to_module_buffer(key));
          metadata_flat.push(str_to_module_buffer(value));
        }
      }
    }
    let metadata_ptr = if metadata_flat.is_empty() {
      std::ptr::null()
    } else {
      metadata_flat.as_ptr()
    };

    let mut result_ptrs: Vec<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> =
      vec![std::ptr::null_mut(); count];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_cluster_add_hosts(
        self.raw,
        priority,
        address_buffers.as_ptr(),
        weights.as_ptr(),
        region_buffers.as_ptr(),
        zone_buffers.as_ptr(),
        sub_zone_buffers.as_ptr(),
        metadata_ptr,
        metadata_pairs_per_host,
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

  fn update_host_health(
    &self,
    host: abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
    health_status: abi::envoy_dynamic_module_type_host_health,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_update_host_health(self.raw, host, health_status)
    }
  }

  fn find_host_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> {
    let address_buffer = str_to_module_buffer(address);
    let host = unsafe {
      abi::envoy_dynamic_module_callback_cluster_find_host_by_address(self.raw, address_buffer)
    };
    if host.is_null() {
      None
    } else {
      Some(host)
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

  fn send_http_callout<'a>(
    &self,
    cluster_name: &'a str,
    headers: &[(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);

    let module_headers: Vec<abi::envoy_dynamic_module_type_module_http_header> = headers
      .iter()
      .map(|(k, v)| abi::envoy_dynamic_module_type_module_http_header {
        key_ptr: k.as_ptr() as *const _,
        key_length: k.len(),
        value_ptr: v.as_ptr() as *const _,
        value_length: v.len(),
      })
      .collect();

    let mut callout_id: u64 = 0;

    let result = unsafe {
      abi::envoy_dynamic_module_callback_cluster_http_callout(
        self.raw,
        &mut callout_id as *mut _ as *mut _,
        str_to_module_buffer(cluster_name),
        module_headers.as_ptr() as *mut _,
        module_headers.len(),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        timeout_milliseconds,
      )
    };
    (result, callout_id)
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

  fn get_host(
    &self,
    priority: u32,
    index: usize,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> {
    let host =
      unsafe { abi::envoy_dynamic_module_callback_cluster_lb_get_host(self.raw, priority, index) };
    if host.is_null() {
      None
    } else {
      Some(host)
    }
  }

  fn find_host_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr> {
    let address_buffer = str_to_module_buffer(address);
    let host = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_find_host_by_address(self.raw, address_buffer)
    };
    if host.is_null() {
      None
    } else {
      Some(host)
    }
  }

  fn get_cluster_name(&self) -> String {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_cluster_name(self.raw, &mut result);
    }
    if result.ptr.is_null() || result.length == 0 {
      String::new()
    } else {
      unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      }
    }
  }

  fn get_hosts_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_cluster_lb_get_hosts_count(self.raw, priority) }
  }

  fn get_degraded_hosts_count(&self, priority: u32) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(self.raw, priority)
    }
  }

  fn get_priority_set_size(&self) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(self.raw) }
  }

  fn get_healthy_host_address(&self, priority: u32, index: usize) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(
        self.raw,
        priority,
        index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
    }
  }

  fn get_healthy_host_weight(&self, priority: u32, index: usize) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(
        self.raw, priority, index,
      )
    }
  }

  fn get_host_health(
    &self,
    priority: u32,
    index: usize,
  ) -> abi::envoy_dynamic_module_type_host_health {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_health(self.raw, priority, index)
    }
  }

  fn get_host_health_by_address(
    &self,
    address: &str,
  ) -> Option<abi::envoy_dynamic_module_type_host_health> {
    let address_buf = str_to_module_buffer(address);
    let mut result = abi::envoy_dynamic_module_type_host_health::Unhealthy;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
        self.raw,
        address_buf,
        &mut result,
      )
    };
    if found {
      Some(result)
    } else {
      None
    }
  }

  fn get_host_address(&self, priority: u32, index: usize) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_address(
        self.raw,
        priority,
        index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
    }
  }

  fn get_host_weight(&self, priority: u32, index: usize) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_weight(self.raw, priority, index)
    }
  }

  fn get_host_stat(
    &self,
    priority: u32,
    index: usize,
    stat: abi::envoy_dynamic_module_type_host_stat,
  ) -> u64 {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_stat(self.raw, priority, index, stat)
    }
  }

  fn get_host_locality(&self, priority: u32, index: usize) -> Option<(String, String, String)> {
    let mut region = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut zone = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut sub_zone = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_locality(
        self.raw,
        priority,
        index,
        &mut region,
        &mut zone,
        &mut sub_zone,
      )
    };
    if found {
      unsafe {
        let region_str = if region.ptr.is_null() || region.length == 0 {
          String::new()
        } else {
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            region.ptr as *const u8,
            region.length,
          ))
          .to_string()
        };
        let zone_str = if zone.ptr.is_null() || zone.length == 0 {
          String::new()
        } else {
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            zone.ptr as *const u8,
            zone.length,
          ))
          .to_string()
        };
        let sub_zone_str = if sub_zone.ptr.is_null() || sub_zone.length == 0 {
          String::new()
        } else {
          std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            sub_zone.ptr as *const u8,
            sub_zone.length,
          ))
          .to_string()
        };
        Some((region_str, zone_str, sub_zone_str))
      }
    } else {
      None
    }
  }

  fn set_host_data(&self, priority: u32, index: usize, data: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_set_host_data(self.raw, priority, index, data)
    }
  }

  fn get_host_data(&self, priority: u32, index: usize) -> Option<usize> {
    let mut data: usize = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_data(
        self.raw, priority, index, &mut data,
      )
    };
    if found {
      Some(data)
    } else {
      None
    }
  }

  fn get_host_metadata_string(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<String> {
    let filter_buf = str_to_module_buffer(filter_name);
    let key_buf = str_to_module_buffer(key);
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
        self.raw,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
    }
  }

  fn get_host_metadata_number(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<f64> {
    let filter_buf = str_to_module_buffer(filter_name);
    let key_buf = str_to_module_buffer(key);
    let mut result: f64 = 0.0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
        self.raw,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found {
      Some(result)
    } else {
      None
    }
  }

  fn get_host_metadata_bool(
    &self,
    priority: u32,
    index: usize,
    filter_name: &str,
    key: &str,
  ) -> Option<bool> {
    let filter_buf = str_to_module_buffer(filter_name);
    let key_buf = str_to_module_buffer(key);
    let mut result: bool = false;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
        self.raw,
        priority,
        index,
        filter_buf,
        key_buf,
        &mut result,
      )
    };
    if found {
      Some(result)
    } else {
      None
    }
  }

  fn get_locality_count(&self, priority: u32) -> usize {
    unsafe { abi::envoy_dynamic_module_callback_cluster_lb_get_locality_count(self.raw, priority) }
  }

  fn get_locality_host_count(&self, priority: u32, locality_index: usize) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
        self.raw,
        priority,
        locality_index,
      )
    }
  }

  fn get_locality_host_address(
    &self,
    priority: u32,
    locality_index: usize,
    host_index: usize,
  ) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
        self.raw,
        priority,
        locality_index,
        host_index,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
    }
  }

  fn get_locality_weight(&self, priority: u32, locality_index: usize) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
        self.raw,
        priority,
        locality_index,
      )
    }
  }

  fn get_member_update_host_address(&self, index: usize, is_added: bool) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let found = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
        self.raw,
        index,
        is_added,
        &mut result,
      )
    };
    if found && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe {
        std::str::from_utf8_unchecked(std::slice::from_raw_parts(
          result.ptr as *const u8,
          result.length,
        ))
        .to_string()
      })
    } else {
      None
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

struct EnvoyAsyncHostSelectionCompleteImpl {
  raw_lb: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  raw_context: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
}

unsafe impl Send for EnvoyAsyncHostSelectionCompleteImpl {}

impl EnvoyAsyncHostSelectionComplete for EnvoyAsyncHostSelectionCompleteImpl {
  fn async_host_selection_complete(
    &self,
    host: Option<abi::envoy_dynamic_module_type_cluster_host_envoy_ptr>,
    details: &str,
  ) {
    let host_ptr = host.unwrap_or(std::ptr::null_mut());
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
        self.raw_lb,
        self.raw_context,
        host_ptr,
        str_to_module_buffer(details),
      );
    }
  }
}

struct ClusterLbContextImpl {
  raw_context: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  raw_lb: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
}

impl ClusterLbContextImpl {
  fn new(
    raw_context: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
    raw_lb: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  ) -> Self {
    Self {
      raw_context,
      raw_lb,
    }
  }
}

impl ClusterLbContext for ClusterLbContextImpl {
  fn compute_hash_key(&self) -> Option<u64> {
    let mut hash: u64 = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(
        self.raw_context,
        &mut hash,
      )
    };
    if ok {
      Some(hash)
    } else {
      None
    }
  }

  fn get_downstream_headers_size(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(
        self.raw_context,
      )
    }
  }

  fn get_downstream_headers(&self) -> Option<Vec<(String, String)>> {
    let size = self.get_downstream_headers_size();
    if size == 0 {
      return None;
    }
    let mut raw_headers = vec![
      abi::envoy_dynamic_module_type_envoy_http_header {
        key_ptr: std::ptr::null_mut(),
        key_length: 0,
        value_ptr: std::ptr::null_mut(),
        value_length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
        self.raw_context,
        raw_headers.as_mut_ptr(),
      )
    };
    if !ok {
      return None;
    }
    Some(
      raw_headers
        .iter()
        .map(|h| unsafe {
          let key = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            h.key_ptr as *const u8,
            h.key_length,
          ));
          let value = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
            h.value_ptr as *const u8,
            h.value_length,
          ));
          (key.to_string(), value.to_string())
        })
        .collect(),
    )
  }

  fn get_downstream_header(&self, key: &str, index: usize) -> Option<(String, usize)> {
    let key_buf = str_to_module_buffer(key);
    let mut result_buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut total_size: usize = 0;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
        self.raw_context,
        key_buf,
        &mut result_buffer,
        index,
        &mut total_size,
      )
    };
    if !ok {
      return None;
    }
    let value = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        result_buffer.ptr as *const u8,
        result_buffer.length,
      ))
    };
    Some((value.to_string(), total_size))
  }

  fn get_host_selection_retry_count(&self) -> u32 {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
        self.raw_context,
      )
    }
  }

  fn should_select_another_host(&self, priority: u32, index: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
        self.raw_lb,
        self.raw_context,
        priority,
        index,
      )
    }
  }

  fn get_override_host(&self) -> Option<(String, bool)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let mut strict = false;
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
        self.raw_context,
        &mut address,
        &mut strict,
      )
    };
    if !ok {
      return None;
    }
    let addr_str = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        address.ptr as *const u8,
        address.length,
      ))
    };
    Some((addr_str.to_string(), strict))
  }

  fn get_downstream_connection_sni(&self) -> Option<String> {
    let mut result_buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null_mut(),
      length: 0,
    };
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
        self.raw_context,
        &mut result_buffer,
      )
    };
    if !ok {
      return None;
    }
    let sni = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        result_buffer.ptr as *const u8,
        result_buffer.length,
      ))
    };
    Some(sni.to_string())
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

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_init(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = &mut *cluster;
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

/// Wrapper that pairs a module-side load balancer with the Envoy-side LB pointer.
/// The `lb_envoy_ptr` is needed by [`ClusterLbContextImpl::should_select_another_host`] to
/// resolve host pointers from the priority set.
struct ClusterLbWrapper {
  lb: Box<dyn ClusterLb>,
  lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
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
  let wrapper = Box::new(ClusterLbWrapper { lb, lb_envoy_ptr });
  Box::into_raw(wrapper) as abi::envoy_dynamic_module_type_cluster_lb_module_ptr
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_destroy(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
) {
  let wrapper = lb_module_ptr as *mut ClusterLbWrapper;
  let _ = Box::from_raw(wrapper);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_choose_host(
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
  context_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
  host_out: *mut abi::envoy_dynamic_module_type_cluster_host_envoy_ptr,
  async_handle_out: *mut abi::envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
  let wrapper = &mut *(lb_module_ptr as *mut ClusterLbWrapper);
  let context = if context_envoy_ptr.is_null() {
    None
  } else {
    Some(ClusterLbContextImpl::new(
      context_envoy_ptr,
      wrapper.lb_envoy_ptr,
    ))
  };

  let async_completion = Box::new(EnvoyAsyncHostSelectionCompleteImpl {
    raw_lb: wrapper.lb_envoy_ptr,
    raw_context: context_envoy_ptr,
  });

  let result = wrapper.lb.choose_host(
    context.as_ref().map(|c| c as &dyn ClusterLbContext),
    async_completion,
  );

  match result {
    HostSelectionResult::Selected(host) => {
      *host_out = host;
      *async_handle_out = std::ptr::null_mut();
    },
    HostSelectionResult::NoHost => {
      *host_out = std::ptr::null_mut();
      *async_handle_out = std::ptr::null_mut();
    },
    HostSelectionResult::AsyncPending(handle) => {
      *host_out = std::ptr::null_mut();
      *async_handle_out = Box::into_raw(Box::new(handle))
        as abi::envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr;
    },
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_cancel_host_selection(
  _lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
  async_handle_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
  let handle = async_handle_module_ptr as *mut Box<dyn AsyncHostSelectionHandle>;
  let mut handle = Box::from_raw(handle);
  handle.cancel();
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_lb_on_host_membership_update(
  lb_envoy_ptr: abi::envoy_dynamic_module_type_cluster_lb_envoy_ptr,
  lb_module_ptr: abi::envoy_dynamic_module_type_cluster_lb_module_ptr,
  num_hosts_added: usize,
  num_hosts_removed: usize,
) {
  let wrapper = &mut *(lb_module_ptr as *mut ClusterLbWrapper);
  let envoy_lb = EnvoyClusterLoadBalancerImpl::new(lb_envoy_ptr);
  wrapper
    .lb
    .on_host_membership_update(&envoy_lb, num_hosts_added, num_hosts_removed);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_scheduled(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  event_id: u64,
) {
  let cluster = cluster_module_ptr as *const *const dyn Cluster;
  let cluster = &**cluster;
  cluster.on_scheduled(&EnvoyClusterImpl::new(cluster_envoy_ptr), event_id);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_server_initialized(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = &mut *cluster;
  cluster.on_server_initialized(&EnvoyClusterImpl::new(cluster_envoy_ptr));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_drain_started(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = &mut *cluster;
  cluster.on_drain_started(&EnvoyClusterImpl::new(cluster_envoy_ptr));
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_shutdown(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  completion_callback: abi::envoy_dynamic_module_type_event_cb,
  completion_context: *mut std::os::raw::c_void,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = &mut *cluster;
  let completion = CompletionCallback::new(completion_callback, completion_context);
  cluster.on_shutdown(&EnvoyClusterImpl::new(cluster_envoy_ptr), completion);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cluster_http_callout_done(
  cluster_envoy_ptr: abi::envoy_dynamic_module_type_cluster_envoy_ptr,
  cluster_module_ptr: abi::envoy_dynamic_module_type_cluster_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let cluster = cluster_module_ptr as *mut Box<dyn Cluster>;
  let cluster = &mut *cluster;

  let headers = if headers_size > 0 {
    Some(std::slice::from_raw_parts(
      headers as *const (EnvoyBuffer, EnvoyBuffer),
      headers_size,
    ))
  } else {
    None
  };
  let body = if body_chunks_size > 0 {
    Some(std::slice::from_raw_parts(
      body_chunks as *const EnvoyBuffer,
      body_chunks_size,
    ))
  } else {
    None
  };

  cluster.on_http_callout_done(
    &EnvoyClusterImpl::new(cluster_envoy_ptr),
    callout_id,
    result,
    headers,
    body,
  );
}
