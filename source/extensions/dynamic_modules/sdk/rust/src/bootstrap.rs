use crate::abi::envoy_dynamic_module_type_metrics_result;
use crate::buffer::EnvoyBuffer;
use crate::{
  abi, drop_wrapped_c_void_ptr, str_to_module_buffer, wrap_into_c_void_ptr, EnvoyCounterId,
  EnvoyCounterVecId, EnvoyGaugeId, EnvoyGaugeVecId, EnvoyHistogramId, EnvoyHistogramVecId,
  NewBootstrapExtensionConfigFunction, NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION,
};
use mockall::*;

/// EnvoyBootstrapExtensionConfig is the Envoy-side bootstrap extension configuration.
/// This is a handle to the Envoy configuration object.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyBootstrapExtensionConfig {
  /// Create a new implementation of the [`EnvoyBootstrapExtensionConfigScheduler`] trait.
  ///
  /// This can be used to schedule an event to the main thread where the config is running.
  fn new_scheduler(&self) -> Box<dyn EnvoyBootstrapExtensionConfigScheduler>;

  /// Send an HTTP callout to the given cluster with the given headers and optional body.
  ///
  /// Headers must contain the `:method`, `:path`, and `host` headers.
  ///
  /// This returns a tuple of (status, callout_id):
  ///   * Success + valid callout_id: The callout was started successfully. The callout ID can be
  ///     used to correlate the response in [`BootstrapExtensionConfig::on_http_callout_done`].
  ///   * ClusterNotFound: The cluster does not exist.
  ///   * MissingRequiredHeaders: The headers are missing required headers.
  ///   * CannotCreateRequest: The request could not be created, e.g., there's no healthy upstream
  ///     host in the cluster.
  ///
  /// The callout result will be delivered to the [`BootstrapExtensionConfig::on_http_callout_done`]
  /// method.
  ///
  /// This must be called on the main thread. To call from other threads, use the scheduler
  /// mechanism to post an event to the main thread first.
  fn send_http_callout<'a>(
    &mut self,
    _cluster_name: &'a str,
    _headers: Vec<(&'a str, &'a [u8])>,
    _body: Option<&'a [u8]>,
    _timeout_milliseconds: u64,
  ) -> (
    abi::envoy_dynamic_module_type_http_callout_init_result,
    u64, // callout id
  );

  /// Signal that the module's initialization is complete. Envoy automatically registers an init
  /// target for every bootstrap extension, blocking traffic until this is called.
  ///
  /// The module must call this exactly once during or after `new_bootstrap_extension_config` to
  /// unblock Envoy. If the module does not require asynchronous initialization, it should call
  /// this immediately during config creation.
  ///
  /// This must be called on the main thread. To call from other threads, use the scheduler
  /// mechanism to post an event to the main thread first.
  fn signal_init_complete(&self);

  /// Define a new counter scoped to this bootstrap extension config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec scoped to this bootstrap extension config with the given name.
  fn define_counter_vec<'a>(
    &mut self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this bootstrap extension config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec scoped to this bootstrap extension config with the given name.
  fn define_gauge_vec<'a>(
    &mut self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this bootstrap extension config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec scoped to this bootstrap extension config with the given name.
  fn define_histogram_vec<'a>(
    &mut self,
    name: &str,
    labels: &[&'a str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result>;

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increment the counter vec with the given id.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge vec with the given id.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge vec with the given id.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge vec with the given id.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram vec with the given id.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Create a new timer on the main thread dispatcher.
  ///
  /// The timer is not armed upon creation. Call [`EnvoyBootstrapExtensionTimer::enable`] to arm it.
  /// When the timer fires, [`BootstrapExtensionConfig::on_timer_fired`] is called on the main
  /// thread.
  ///
  /// The returned timer handle owns the underlying Envoy timer and will destroy it when dropped.
  ///
  /// This must be called on the main thread.
  fn new_timer(&self) -> Box<dyn EnvoyBootstrapExtensionTimer>;

  /// Watch a file or directory for changes. Each call creates a new watcher for the given path.
  /// The watcher lifetime is managed by Envoy and tied to the config — all watchers are
  /// automatically destroyed when the config is destroyed.
  ///
  /// When a change is detected, [`BootstrapExtensionConfig::on_file_changed`] is called on the
  /// main thread.
  ///
  /// Returns `true` if the watch was successfully added, `false` otherwise (e.g. file does not
  /// exist).
  ///
  /// This must be called on the main thread.
  fn add_file_watch(&self, path: &str, events: u32) -> bool;

  /// Register a custom admin HTTP endpoint.
  ///
  /// When the endpoint is requested, [`BootstrapExtensionConfig::on_admin_request`] is called.
  ///
  /// * `path_prefix` is the URL prefix to handle (e.g. "/my_module/status").
  /// * `help_text` is the help text displayed in the admin console.
  /// * `removable` if true, allows the handler to be removed later via
  ///   [`EnvoyBootstrapExtensionConfig::remove_admin_handler`].
  /// * `mutates_server_state` if true, indicates the handler mutates server state.
  ///
  /// Returns `true` if the handler was successfully registered, `false` otherwise.
  ///
  /// This must be called on the main thread.
  fn register_admin_handler(
    &self,
    path_prefix: &str,
    help_text: &str,
    removable: bool,
    mutates_server_state: bool,
  ) -> bool;

  /// Remove a previously registered admin HTTP endpoint.
  ///
  /// * `path_prefix` is the URL prefix of the handler to remove.
  ///
  /// Returns `true` if the handler was successfully removed, `false` otherwise.
  ///
  /// This must be called on the main thread.
  fn remove_admin_handler(&self, path_prefix: &str) -> bool;

  /// Enable cluster lifecycle event notifications. When enabled, the module will receive
  /// [`BootstrapExtensionConfig::on_cluster_add_or_update`] and
  /// [`BootstrapExtensionConfig::on_cluster_removal`] callbacks when clusters are added, updated,
  /// or removed from the ClusterManager.
  ///
  /// This must be called on the main thread, typically during or after `on_server_initialized`,
  /// since the ClusterManager is not available until that point.
  ///
  /// This should be called at most once. Subsequent calls are no-ops and return `false`.
  fn enable_cluster_lifecycle(&self) -> bool;

  /// Enable listener lifecycle event notifications. When enabled, the module will receive
  /// [`BootstrapExtensionConfig::on_listener_add_or_update`] and
  /// [`BootstrapExtensionConfig::on_listener_removal`] callbacks when listeners are added, updated,
  /// or removed from the ListenerManager.
  ///
  /// This must be called on the main thread, typically during or after `on_server_initialized`,
  /// since the ListenerManager is not available until that point.
  ///
  /// This should be called at most once. Subsequent calls are no-ops and return `false`.
  fn enable_listener_lifecycle(&self) -> bool;
}

/// EnvoyBootstrapExtension is the Envoy-side bootstrap extension.
/// This is a handle to the Envoy extension object.
pub trait EnvoyBootstrapExtension {
  /// Get the current value of a counter by name.
  ///
  /// Returns `Some(value)` if the counter exists, `None` otherwise.
  fn get_counter_value(&self, name: &str) -> Option<u64>;

  /// Get the current value of a gauge by name.
  ///
  /// Returns `Some(value)` if the gauge exists, `None` otherwise.
  fn get_gauge_value(&self, name: &str) -> Option<u64>;

  /// Get the summary statistics of a histogram by name.
  ///
  /// Returns `Some((sample_count, sample_sum))` if the histogram exists, `None` otherwise.
  /// These are cumulative statistics since the server started.
  fn get_histogram_summary(&self, name: &str) -> Option<(u64, f64)>;

  /// Iterate over all counters in the stats store.
  ///
  /// The callback receives the counter name and its current value.
  /// Return `true` to continue iteration, `false` to stop.
  fn iterate_counters(&self, callback: &mut dyn FnMut(&str, u64) -> bool);

  /// Iterate over all gauges in the stats store.
  ///
  /// The callback receives the gauge name and its current value.
  /// Return `true` to continue iteration, `false` to stop.
  fn iterate_gauges(&self, callback: &mut dyn FnMut(&str, u64) -> bool);
}

/// BootstrapExtensionConfig is the module-side bootstrap extension configuration.
///
/// This trait must be implemented by the module to handle bootstrap extension configuration.
/// The object is created when the corresponding Envoy bootstrap extension config is created, and
/// it is dropped when the corresponding Envoy bootstrap extension config is destroyed. Therefore,
/// the implementation is recommended to implement the [`Drop`] trait to handle the necessary
/// cleanup.
///
/// Implementations must also be `Send + Sync` since they may be accessed from multiple threads.
pub trait BootstrapExtensionConfig: Send + Sync {
  /// Create a new bootstrap extension instance.
  ///
  /// This is called when a new bootstrap extension is created.
  fn new_bootstrap_extension(
    &self,
    envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension>;

  /// This is called when a new event is scheduled via the
  /// [`EnvoyBootstrapExtensionConfigScheduler::commit`] for this [`BootstrapExtensionConfig`].
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyBootstrapExtensionConfigScheduler::commit`] to distinguish multiple scheduled events.
  fn on_scheduled(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _event_id: u64,
  ) {
  }

  /// This is called when an HTTP callout response is received.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `callout_id` is the ID of the callout returned by
  ///   [`EnvoyBootstrapExtensionConfig::send_http_callout`].
  /// * `result` is the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers. This is optional.
  /// * `response_body` is the response body. This is optional.
  fn on_http_callout_done(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
  }

  /// This is called when a timer created via [`EnvoyBootstrapExtensionConfig::new_timer`] fires.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `timer` is a non-owning reference to the timer that fired. The module can re-arm the timer
  ///   by calling [`EnvoyBootstrapExtensionTimer::enable`] on it.
  fn on_timer_fired(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _timer: &dyn EnvoyBootstrapExtensionTimer,
  ) {
  }

  /// This is called when a file watched via
  /// [`EnvoyBootstrapExtensionConfig::add_file_watch`] changes.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `path` is the path that was registered via `add_file_watch` that triggered the change.
  /// * `events` is the bitmask of events that occurred ([`FILE_WATCHER_EVENT_MOVED_TO`],
  ///   [`FILE_WATCHER_EVENT_MODIFIED`]).
  fn on_file_changed(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _path: &str,
    _events: u32,
  ) {
  }

  /// This is called when an admin endpoint registered via
  /// [`EnvoyBootstrapExtensionConfig::register_admin_handler`] is requested.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `method` is the HTTP method of the request (e.g. "GET", "POST").
  /// * `path` is the full path and query string of the request.
  /// * `body` is the request body. May be empty.
  ///
  /// Returns a tuple of (HTTP status code, response body string).
  fn on_admin_request(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    _path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    (404, String::new())
  }

  /// This is called when a cluster is added to or updated in the ClusterManager.
  ///
  /// This is only called if the module has opted in via
  /// [`EnvoyBootstrapExtensionConfig::enable_cluster_lifecycle`]. The callback is invoked on the
  /// main thread.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `cluster_name` is the name of the cluster that was added or updated.
  fn on_cluster_add_or_update(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _cluster_name: &str,
  ) {
  }

  /// This is called when a cluster is removed from the ClusterManager.
  ///
  /// This is only called if the module has opted in via
  /// [`EnvoyBootstrapExtensionConfig::enable_cluster_lifecycle`]. The callback is invoked on the
  /// main thread.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `cluster_name` is the name of the cluster that was removed.
  fn on_cluster_removal(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _cluster_name: &str,
  ) {
  }

  /// This is called when a listener is added to or updated in the ListenerManager.
  ///
  /// This is only called if the module has opted in via
  /// [`EnvoyBootstrapExtensionConfig::enable_listener_lifecycle`]. The callback is invoked on the
  /// main thread.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `listener_name` is the name of the listener that was added or updated.
  fn on_listener_add_or_update(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _listener_name: &str,
  ) {
  }

  /// This is called when a listener is removed from the ListenerManager.
  ///
  /// This is only called if the module has opted in via
  /// [`EnvoyBootstrapExtensionConfig::enable_listener_lifecycle`]. The callback is invoked on the
  /// main thread.
  ///
  /// * `envoy_extension_config` can be used to interact with the underlying Envoy config object.
  /// * `listener_name` is the name of the listener that was removed.
  fn on_listener_removal(
    &self,
    _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _listener_name: &str,
  ) {
  }
}

/// A completion callback that must be invoked exactly once to signal that an asynchronous
/// operation has finished. Envoy will wait for this callback before proceeding.
///
/// The callback is invoked by calling [`CompletionCallback::done`].
pub struct CompletionCallback {
  callback: abi::envoy_dynamic_module_type_event_cb,
  context: *mut std::os::raw::c_void,
}

// Safety: The completion callback is provided by Envoy and is safe to send across threads.
unsafe impl Send for CompletionCallback {}
// Safety: The completion callback function pointer is thread-safe when invoked exactly once.
unsafe impl Sync for CompletionCallback {}

impl CompletionCallback {
  pub(crate) fn new(
    callback: abi::envoy_dynamic_module_type_event_cb,
    context: *mut std::os::raw::c_void,
  ) -> Self {
    Self { callback, context }
  }

  /// Signal that the asynchronous operation is complete. This must be called exactly once.
  pub fn done(self) {
    unsafe {
      if let Some(cb) = self.callback {
        cb(self.context);
      }
    }
    // Prevent Drop from running since we've consumed the callback.
    std::mem::forget(self);
  }
}

impl Drop for CompletionCallback {
  fn drop(&mut self) {
    // If the callback is dropped without being called, invoke it to prevent Envoy from hanging.
    unsafe {
      if let Some(cb) = self.callback {
        cb(self.context);
      }
    }
  }
}

/// BootstrapExtension is the module-side bootstrap extension.
///
/// This trait must be implemented by the module to handle bootstrap extension lifecycle events.
///
/// All the event hooks are called on the main thread unless otherwise noted.
pub trait BootstrapExtension: Send + Sync {
  /// Called when the server is initialized.
  ///
  /// This is called on the main thread after the ServerFactoryContext is fully initialized.
  /// This is where you can perform initialization tasks like fetching configuration from
  /// external services, initializing global state, or registering singleton resources.
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}

  /// Called when a worker thread is initialized.
  ///
  /// This is called once per worker thread when it starts. You can use this to perform
  /// per-worker-thread initialization like setting up thread-local storage.
  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}

  /// Called when Envoy begins draining.
  ///
  /// This is called on the main thread before workers are stopped. The module can still make HTTP
  /// callouts and use timers during drain. This is the appropriate place to close persistent
  /// connections, stop background tasks, or de-register from service discovery.
  fn on_drain_started(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}

  /// Called when Envoy is about to exit.
  ///
  /// This is called on the main thread during the ShutdownExit lifecycle stage. The module MUST
  /// signal completion by calling [`CompletionCallback::done`] when it has finished cleanup. Envoy
  /// will wait for the callback before terminating.
  ///
  /// If the [`CompletionCallback`] is dropped without calling `done`, it will automatically signal
  /// completion to prevent Envoy from hanging.
  fn on_shutdown(
    &mut self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
    completion: CompletionCallback,
  ) {
    completion.done();
  }
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy bootstrap extension config on the main thread.
#[automock]
pub trait EnvoyBootstrapExtensionConfigScheduler: Send + Sync {
  /// Commit the scheduled event to the main thread.
  fn commit(&self, event_id: u64);
}

struct EnvoyBootstrapExtensionConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr,
}

unsafe impl Send for EnvoyBootstrapExtensionConfigSchedulerImpl {}
unsafe impl Sync for EnvoyBootstrapExtensionConfigSchedulerImpl {}

impl Drop for EnvoyBootstrapExtensionConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyBootstrapExtensionConfigScheduler for EnvoyBootstrapExtensionConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
        self.raw_ptr,
        event_id,
      );
    }
  }
}

impl EnvoyBootstrapExtensionConfigScheduler for Box<dyn EnvoyBootstrapExtensionConfigScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// A timer handle for bootstrap extensions on the main thread event loop.
///
/// The timer is created via [`EnvoyBootstrapExtensionConfig::new_timer`] and fires by calling
/// [`BootstrapExtensionConfig::on_timer_fired`]. All methods must be called on the main thread.
///
/// The owning handle (returned by `new_timer`) will automatically destroy the underlying Envoy
/// timer when dropped.
///
/// Each timer has a unique [`id`](EnvoyBootstrapExtensionTimer::id) that is stable for its
/// lifetime. This allows modules with multiple timers to identify which timer fired in the
/// [`BootstrapExtensionConfig::on_timer_fired`] callback by comparing the id of the fired timer
/// reference against the ids of their stored timer handles.
#[automock]
pub trait EnvoyBootstrapExtensionTimer: Send + Sync {
  /// Returns a unique opaque identifier for this timer. The identifier is stable for the
  /// lifetime of the timer and can be used to distinguish between multiple timers in the
  /// [`BootstrapExtensionConfig::on_timer_fired`] callback.
  fn id(&self) -> usize;

  /// Enable the timer with the given delay. If the timer is already enabled, it is reset.
  fn enable(&self, delay: std::time::Duration);

  /// Disable the timer without destroying it. The timer can be re-enabled later.
  fn disable(&self);

  /// Check whether the timer is currently armed.
  fn enabled(&self) -> bool;
}

/// Owning implementation of [`EnvoyBootstrapExtensionTimer`]. Calls `timer_delete` on drop.
struct EnvoyBootstrapExtensionTimerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
}

unsafe impl Send for EnvoyBootstrapExtensionTimerImpl {}
unsafe impl Sync for EnvoyBootstrapExtensionTimerImpl {}

impl Drop for EnvoyBootstrapExtensionTimerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_timer_delete(self.raw_ptr);
    }
  }
}

impl EnvoyBootstrapExtensionTimer for EnvoyBootstrapExtensionTimerImpl {
  fn id(&self) -> usize {
    self.raw_ptr as usize
  }

  fn enable(&self, delay: std::time::Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
        self.raw_ptr,
        delay.as_millis() as u64,
      );
    }
  }

  fn disable(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_timer_disable(self.raw_ptr);
    }
  }

  fn enabled(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(self.raw_ptr) }
  }
}

/// Non-owning reference to a timer, used in the [`BootstrapExtensionConfig::on_timer_fired`]
/// callback. Does NOT call `timer_delete` on drop.
struct EnvoyBootstrapExtensionTimerRef {
  raw_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
}

// SAFETY: The raw pointer is only used on the main thread, matching Envoy's threading model.
unsafe impl Send for EnvoyBootstrapExtensionTimerRef {}
unsafe impl Sync for EnvoyBootstrapExtensionTimerRef {}

impl EnvoyBootstrapExtensionTimer for EnvoyBootstrapExtensionTimerRef {
  fn id(&self) -> usize {
    self.raw_ptr as usize
  }

  fn enable(&self, delay: std::time::Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
        self.raw_ptr,
        delay.as_millis() as u64,
      );
    }
  }

  fn disable(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_timer_disable(self.raw_ptr);
    }
  }

  fn enabled(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(self.raw_ptr) }
  }
}

impl EnvoyBootstrapExtensionTimer for Box<dyn EnvoyBootstrapExtensionTimer> {
  fn id(&self) -> usize {
    (**self).id()
  }

  fn enable(&self, delay: std::time::Duration) {
    (**self).enable(delay);
  }

  fn disable(&self) {
    (**self).disable();
  }

  fn enabled(&self) -> bool {
    (**self).enabled()
  }
}

/// File watcher event: file was moved to the watched path (e.g. atomic rename).
pub const FILE_WATCHER_EVENT_MOVED_TO: u32 = 0x1;
/// File watcher event: file content was modified.
pub const FILE_WATCHER_EVENT_MODIFIED: u32 = 0x2;

// Implementation of EnvoyBootstrapExtensionConfig

pub(crate) struct EnvoyBootstrapExtensionConfigImpl {
  raw: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
}

impl EnvoyBootstrapExtensionConfigImpl {
  pub(crate) fn new(
    raw: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  ) -> Self {
    Self { raw }
  }
}

impl EnvoyBootstrapExtensionConfig for EnvoyBootstrapExtensionConfigImpl {
  fn new_scheduler(&self) -> Box<dyn EnvoyBootstrapExtensionConfigScheduler> {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(self.raw);
      Box::new(EnvoyBootstrapExtensionConfigSchedulerImpl {
        raw_ptr: scheduler_ptr,
      })
    }
  }

  fn send_http_callout<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: Vec<(&'a str, &'a [u8])>,
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);

    // Convert headers to module HTTP headers.
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
      abi::envoy_dynamic_module_callback_bootstrap_extension_http_callout(
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

  fn signal_init_complete(&self) {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(self.raw);
    }
  }

  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
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
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
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
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
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
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
        self.raw,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
        self.raw,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
        self.raw,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
        self.raw,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
        self.raw,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
        self.raw,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn new_timer(&self) -> Box<dyn EnvoyBootstrapExtensionTimer> {
    unsafe {
      let timer_ptr = abi::envoy_dynamic_module_callback_bootstrap_extension_timer_new(self.raw);
      Box::new(EnvoyBootstrapExtensionTimerImpl { raw_ptr: timer_ptr })
    }
  }

  fn add_file_watch(&self, path: &str, events: u32) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch(
        self.raw,
        str_to_module_buffer(path),
        events,
      )
    }
  }

  fn register_admin_handler(
    &self,
    path_prefix: &str,
    help_text: &str,
    removable: bool,
    mutates_server_state: bool,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
        self.raw,
        str_to_module_buffer(path_prefix),
        str_to_module_buffer(help_text),
        removable,
        mutates_server_state,
      )
    }
  }

  fn remove_admin_handler(&self, path_prefix: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
        self.raw,
        str_to_module_buffer(path_prefix),
      )
    }
  }

  fn enable_cluster_lifecycle(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle(self.raw)
    }
  }

  fn enable_listener_lifecycle(&self) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle(self.raw)
    }
  }
}

// Implementation of EnvoyBootstrapExtension

pub(crate) struct EnvoyBootstrapExtensionImpl {
  raw: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
}

impl EnvoyBootstrapExtensionImpl {
  pub(crate) fn new(raw: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyBootstrapExtension for EnvoyBootstrapExtensionImpl {
  fn get_counter_value(&self, name: &str) -> Option<u64> {
    let mut value: u64 = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
        self.raw,
        str_to_module_buffer(name),
        &mut value,
      )
    };
    if found {
      Some(value)
    } else {
      None
    }
  }

  fn get_gauge_value(&self, name: &str) -> Option<u64> {
    let mut value: u64 = 0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
        self.raw,
        str_to_module_buffer(name),
        &mut value,
      )
    };
    if found {
      Some(value)
    } else {
      None
    }
  }

  fn get_histogram_summary(&self, name: &str) -> Option<(u64, f64)> {
    let mut sample_count: u64 = 0;
    let mut sample_sum: f64 = 0.0;
    let found = unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
        self.raw,
        str_to_module_buffer(name),
        &mut sample_count,
        &mut sample_sum,
      )
    };
    if found {
      Some((sample_count, sample_sum))
    } else {
      None
    }
  }

  fn iterate_counters(&self, callback: &mut dyn FnMut(&str, u64) -> bool) {
    // We use a wrapper struct to pass the closure through the C callback.
    struct CallbackWrapper<'a> {
      callback: &'a mut dyn FnMut(&str, u64) -> bool,
      stopped: bool,
    }

    extern "C" fn counter_iterator_trampoline(
      name: abi::envoy_dynamic_module_type_envoy_buffer,
      value: u64,
      user_data: *mut std::ffi::c_void,
    ) -> abi::envoy_dynamic_module_type_stats_iteration_action {
      let wrapper = unsafe { &mut *(user_data as *mut CallbackWrapper) };
      if wrapper.stopped {
        return abi::envoy_dynamic_module_type_stats_iteration_action::Stop;
      }
      let name_slice = unsafe { std::slice::from_raw_parts(name.ptr as *const u8, name.length) };
      let name_str = std::str::from_utf8(name_slice).unwrap_or("");
      if (wrapper.callback)(name_str, value) {
        abi::envoy_dynamic_module_type_stats_iteration_action::Continue
      } else {
        wrapper.stopped = true;
        abi::envoy_dynamic_module_type_stats_iteration_action::Stop
      }
    }

    let mut wrapper = CallbackWrapper {
      callback,
      stopped: false,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
        self.raw,
        Some(counter_iterator_trampoline),
        &mut wrapper as *mut _ as *mut std::ffi::c_void,
      );
    }
  }

  fn iterate_gauges(&self, callback: &mut dyn FnMut(&str, u64) -> bool) {
    // We use a wrapper struct to pass the closure through the C callback.
    struct CallbackWrapper<'a> {
      callback: &'a mut dyn FnMut(&str, u64) -> bool,
      stopped: bool,
    }

    extern "C" fn gauge_iterator_trampoline(
      name: abi::envoy_dynamic_module_type_envoy_buffer,
      value: u64,
      user_data: *mut std::ffi::c_void,
    ) -> abi::envoy_dynamic_module_type_stats_iteration_action {
      let wrapper = unsafe { &mut *(user_data as *mut CallbackWrapper) };
      if wrapper.stopped {
        return abi::envoy_dynamic_module_type_stats_iteration_action::Stop;
      }
      let name_slice = unsafe { std::slice::from_raw_parts(name.ptr as *const u8, name.length) };
      let name_str = std::str::from_utf8(name_slice).unwrap_or("");
      if (wrapper.callback)(name_str, value) {
        abi::envoy_dynamic_module_type_stats_iteration_action::Continue
      } else {
        wrapper.stopped = true;
        abi::envoy_dynamic_module_type_stats_iteration_action::Stop
      }
    }

    let mut wrapper = CallbackWrapper {
      callback,
      stopped: false,
    };
    unsafe {
      abi::envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
        self.raw,
        Some(gauge_iterator_trampoline),
        &mut wrapper as *mut _ as *mut std::ffi::c_void,
      );
    }
  }
}

// Bootstrap Extension Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_config_new(
  envoy_extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr {
  let mut envoy_extension_config =
    EnvoyBootstrapExtensionConfigImpl::new(envoy_extension_config_ptr);
  let name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      name.ptr as *const _,
      name.length,
    ))
  };
  let config_slice = unsafe { std::slice::from_raw_parts(config.ptr as *const _, config.length) };
  init_bootstrap_extension_config(
    &mut envoy_extension_config,
    name_str,
    config_slice,
    NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION
      .get()
      .expect("NEW_BOOTSTRAP_EXTENSION_CONFIG_FUNCTION must be set"),
  )
}

pub(crate) fn init_bootstrap_extension_config(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  name: &str,
  config: &[u8],
  new_extension_config_fn: &NewBootstrapExtensionConfigFunction,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr {
  let extension_config = new_extension_config_fn(envoy_extension_config, name, config);
  match extension_config {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_config_destroy(
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(extension_config_ptr, BootstrapExtensionConfig);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_new(
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  envoy_extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr {
  let mut envoy_extension = EnvoyBootstrapExtensionImpl::new(envoy_extension_ptr);
  let extension_config = {
    let raw = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
    &**raw
  };
  envoy_dynamic_module_on_bootstrap_extension_new_impl(&mut envoy_extension, extension_config)
}

pub(crate) fn envoy_dynamic_module_on_bootstrap_extension_new_impl(
  envoy_extension: &mut EnvoyBootstrapExtensionImpl,
  extension_config: &dyn BootstrapExtensionConfig,
) -> abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr {
  let extension = extension_config.new_bootstrap_extension(envoy_extension);
  wrap_into_c_void_ptr!(extension)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_server_initialized(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
  let extension = extension_ptr as *mut Box<dyn BootstrapExtension>;
  let extension = unsafe { &mut *extension };
  extension.on_server_initialized(&mut EnvoyBootstrapExtensionImpl::new(envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
  let extension = extension_ptr as *mut Box<dyn BootstrapExtension>;
  let extension = unsafe { &mut *extension };
  extension.on_worker_thread_initialized(&mut EnvoyBootstrapExtensionImpl::new(envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_drain_started(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
  let extension = extension_ptr as *mut Box<dyn BootstrapExtension>;
  let extension = unsafe { &mut *extension };
  extension.on_drain_started(&mut EnvoyBootstrapExtensionImpl::new(envoy_ptr));
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_shutdown(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
  extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr,
  completion_callback: abi::envoy_dynamic_module_type_event_cb,
  completion_context: *mut std::os::raw::c_void,
) {
  let extension = extension_ptr as *mut Box<dyn BootstrapExtension>;
  let extension = unsafe { &mut *extension };
  let completion = CompletionCallback::new(completion_callback, completion_context);
  extension.on_shutdown(&mut EnvoyBootstrapExtensionImpl::new(envoy_ptr), completion);
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_destroy(
  extension_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_module_ptr,
) {
  let _ = unsafe { Box::from_raw(extension_ptr as *mut Box<dyn BootstrapExtension>) };
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  event_id: u64,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };
  extension_config.on_scheduled(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    event_id,
  );
}

/// Event hook called by Envoy when an HTTP callout initiated by a bootstrap extension completes.
///
/// # Safety
/// This function is unsafe because it dereferences raw pointers passed from Envoy. The caller
/// must ensure that all pointers are valid and that the memory they point to remains valid for
/// the duration of the function call.
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let headers = if headers_size > 0 {
    Some(unsafe {
      std::slice::from_raw_parts(headers as *const (EnvoyBuffer, EnvoyBuffer), headers_size)
    })
  } else {
    None
  };
  let body = if body_chunks_size > 0 {
    Some(unsafe { std::slice::from_raw_parts(body_chunks as *const EnvoyBuffer, body_chunks_size) })
  } else {
    None
  };

  extension_config.on_http_callout_done(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    callout_id,
    result,
    headers,
    body,
  );
}

/// Event hook called by Envoy when a timer created by a bootstrap extension fires.
#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_timer_fired(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  timer_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  // Create a non-owning reference to the timer so the module can re-enable it.
  let timer_ref = EnvoyBootstrapExtensionTimerRef { raw_ptr: timer_ptr };

  extension_config.on_timer_fired(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    &timer_ref,
  );
}

/// Event hook called by Envoy when a watched file changes for a bootstrap extension.
#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_bootstrap_extension_file_changed(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  path: abi::envoy_dynamic_module_type_envoy_buffer,
  events: u32,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let path_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      path.ptr as *const u8,
      path.length,
    ))
  };

  extension_config.on_file_changed(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    path_str,
    events,
  );
}

/// Event hook called by Envoy when an admin endpoint registered by a bootstrap extension is
/// requested.
///
/// # Safety
/// This function is unsafe because it dereferences raw pointers passed from Envoy. The caller
/// must ensure that all pointers are valid and that the memory they point to remains valid for
/// the duration of the function call.
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_admin_request(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  method: abi::envoy_dynamic_module_type_envoy_buffer,
  path: abi::envoy_dynamic_module_type_envoy_buffer,
  body: abi::envoy_dynamic_module_type_envoy_buffer,
) -> u32 {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let method_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      method.ptr as *const u8,
      method.length,
    ))
  };
  let path_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      path.ptr as *const u8,
      path.length,
    ))
  };
  let body_slice = unsafe { std::slice::from_raw_parts(body.ptr as *const u8, body.length) };

  let (status_code, response_str) = extension_config.on_admin_request(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    method_str,
    path_str,
    body_slice,
  );

  // Pass the response body to Envoy via the callback. Envoy copies the buffer immediately,
  // so the string only needs to live until the call returns.
  if !response_str.is_empty() {
    let response_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: response_str.as_ptr() as *const _,
      length: response_str.len(),
    };
    abi::envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
      envoy_ptr,
      response_buf,
    );
  }

  status_code
}

/// Event hook called by Envoy when a cluster is added to or updated in the ClusterManager.
///
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  cluster_name: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let cluster_name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      cluster_name.ptr as *const u8,
      cluster_name.length,
    ))
  };

  extension_config.on_cluster_add_or_update(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    cluster_name_str,
  );
}

/// Event hook called by Envoy when a cluster is removed from the ClusterManager.
///
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  cluster_name: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let cluster_name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      cluster_name.ptr as *const u8,
      cluster_name.length,
    ))
  };

  extension_config.on_cluster_removal(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    cluster_name_str,
  );
}

/// Event hook called by Envoy when a listener is added to or updated in the ListenerManager.
///
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  listener_name: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let listener_name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      listener_name.ptr as *const u8,
      listener_name.length,
    ))
  };

  extension_config.on_listener_add_or_update(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    listener_name_str,
  );
}

/// Event hook called by Envoy when a listener is removed from the ListenerManager.
///
/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_bootstrap_extension_listener_removal(
  envoy_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
  extension_config_ptr: abi::envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
  listener_name: abi::envoy_dynamic_module_type_envoy_buffer,
) {
  let extension_config = extension_config_ptr as *const *const dyn BootstrapExtensionConfig;
  let extension_config = unsafe { &**extension_config };

  let listener_name_str = unsafe {
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(
      listener_name.ptr as *const u8,
      listener_name.length,
    ))
  };

  extension_config.on_listener_removal(
    &mut EnvoyBootstrapExtensionConfigImpl::new(envoy_ptr),
    listener_name_str,
  );
}
