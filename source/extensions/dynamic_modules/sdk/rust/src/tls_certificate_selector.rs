//! TLS certificate selector support for dynamic modules.
//!
//! A certificate selector picks the certificate that Envoy uses in a TLS handshake. It can pick a
//! configured certificate, let Envoy select one, or pick a dynamic certificate that the module
//! provides at runtime. The entry point is the `tls_certificate_selector:` arm of
//! [`crate::declare_all_init_functions!`], which registers a factory through
//! [`crate::NEW_TLS_CERTIFICATE_SELECTOR_CONFIG_FUNCTION`].

use crate::{
  abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, ffi_export, str_to_module_buffer,
  strs_to_module_buffers, wrap_into_c_void_ptr, EnvoyBuffer, EnvoyCounterId, EnvoyCounterVecId,
  EnvoyGaugeId, EnvoyGaugeVecId, EnvoyHistogramId, EnvoyHistogramVecId,
};
#[cfg(any(test, feature = "mock"))]
use mockall::*;
use std::cell::RefCell;
use std::sync::Arc;

// The select hook returns the name of a dynamic certificate in a module-owned buffer. The buffer
// must stay valid until the next call into the module on the same thread, so it is held in
// thread-local storage.
thread_local! {
  static CERTIFICATE_NAME_BUFFER: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

/// # Safety
///
/// When `found` is true, `buffer` must point to Envoy memory that outlives `'a`.
unsafe fn found_buffer<'a>(
  found: bool,
  buffer: abi::envoy_dynamic_module_type_envoy_buffer,
) -> Option<EnvoyBuffer<'a>> {
  found.then(|| unsafe { EnvoyBuffer::new_from_raw(buffer.ptr as *const u8, buffer.length) })
}

fn empty_buffer() -> abi::envoy_dynamic_module_type_envoy_buffer {
  abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: std::ptr::null(),
    length: 0,
  }
}

/// Context of a single certificate selection.
///
/// It is valid only for the duration of the [`TlsCertificateSelector::select`] call that receives
/// it.
pub struct TlsCertificateSelectionContext {
  envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr,
}

impl TlsCertificateSelectionContext {
  /// Create a new TlsCertificateSelectionContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the selection context Envoy passed to
  /// [`envoy_dynamic_module_on_tls_certificate_selector_select`], and the returned value must not
  /// outlive that call.
  #[doc(hidden)]
  pub unsafe fn new(
    envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr,
  ) -> Self {
    Self { envoy_ptr }
  }

  /// Returns the requested server name (SNI) of the handshake, or `None` when the handshake has
  /// none.
  ///
  /// For a downstream TLS context this is the server name requested by the client, and for an
  /// upstream TLS context it is the server name sent by Envoy.
  pub fn get_requested_server_name(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = empty_buffer();
    let found = unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
        self.envoy_ptr,
        &mut result,
      )
    };
    unsafe { found_buffer(found, result) }
  }

  /// Returns the body of a ClientHello extension, such as the ALPN protocols offered by the client.
  ///
  /// This returns `None` when the ClientHello has no such extension, and always for an upstream TLS
  /// context.
  pub fn get_client_hello_extension(&self, extension_type: u16) -> Option<EnvoyBuffer<'_>> {
    let mut result = empty_buffer();
    let found = unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_get_client_hello_extension(
        self.envoy_ptr,
        extension_type,
        &mut result,
      )
    };
    unsafe { found_buffer(found, result) }
  }

  /// Returns a string value from the filter state of the connection running the handshake.
  ///
  /// For an upstream TLS context this includes the objects that the downstream connection shares
  /// with the upstream connection.
  pub fn get_filter_state(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    let mut result = empty_buffer();
    let found = unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_get_filter_state(
        self.envoy_ptr,
        str_to_module_buffer(key),
        &mut result,
      )
    };
    unsafe { found_buffer(found, result) }
  }
}

/// The certificate that Envoy uses for a handshake, as selected by
/// [`TlsCertificateSelector::select`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TlsCertificateSelection {
  /// Let Envoy select the certificate as it does without a custom selector.
  PassThrough,
  /// Use the configured certificate at this index of the slice passed to
  /// [`TlsCertificateSelectorConfig::new_selector`]. An index out of range fails the handshake.
  Configured(usize),
  /// Use the dynamic certificate of this name. When the module has not provided it yet, the
  /// handshake waits for the answer to [`TlsCertificateSelectorConfig::on_certificate_requested`].
  Dynamic(String),
  /// Fail the handshake.
  Failed,
}

/// Configuration of a TLS certificate selector.
///
/// It is created once per configuration on the main thread and creates a
/// [`TlsCertificateSelector`] for each TLS context that uses the configuration. It is dropped once
/// every selector is gone, which can be on a worker thread or on a thread that calls
/// [`EnvoyTlsCertificateSelectorConfigScheduler::commit`].
pub trait TlsCertificateSelectorConfig: Send + Sync {
  /// Creates the selector of a TLS context.
  ///
  /// `certificates` holds the DER-encoded leaf certificates of the TLS context in configuration
  /// order. An entry is empty when the TLS context has no certificate. The slices are valid only
  /// for the duration of this call, so the selector must copy what it keeps.
  ///
  /// Returning `None` fails every handshake that uses the TLS context.
  fn new_selector(&self, certificates: &[&[u8]]) -> Option<Box<dyn TlsCertificateSelector>>;

  /// Called on the main thread when a handshake waits for a dynamic certificate that the module
  /// has not provided.
  ///
  /// It is called once for each name until the certificate is provided or removed. The module
  /// answers with [`EnvoyTlsCertificateSelectorConfig::set_certificate`] or
  /// [`EnvoyTlsCertificateSelectorConfig::remove_certificate`], either during this call or later,
  /// and the handshakes that wait for the certificate stay paused until then. The default
  /// implementation removes the certificate, which fails them.
  fn on_certificate_requested(
    &self,
    envoy_config: &dyn EnvoyTlsCertificateSelectorConfig,
    name: &str,
  ) {
    envoy_config.remove_certificate(name);
  }

  /// Called on the main thread when an event committed via
  /// [`EnvoyTlsCertificateSelectorConfigScheduler::commit`] is dispatched.
  ///
  /// This is where a module that fetches dynamic certificates off the main thread provides them.
  /// `event_id` is the value passed to [`EnvoyTlsCertificateSelectorConfigScheduler::commit`]. The
  /// default implementation does nothing.
  fn on_scheduled(&self, _envoy_config: &dyn EnvoyTlsCertificateSelectorConfig, _event_id: u64) {}
}

/// Selector of a TLS context.
///
/// It is shared by every handshake that uses the TLS context and may be called concurrently on
/// multiple threads, so it must be `Send + Sync` and read-only during selection.
pub trait TlsCertificateSelector: Send + Sync {
  /// Selects the certificate that Envoy uses for a handshake.
  fn select(&self, context: &TlsCertificateSelectionContext) -> TlsCertificateSelection;
}

/// The Envoy side of a certificate selector configuration.
///
/// It is passed to the factory registered via the `tls_certificate_selector:` arm of
/// [`crate::declare_all_init_functions!`] and to the hooks of [`TlsCertificateSelectorConfig`]. The
/// metrics must be defined while the configuration is being created, and can be recorded on any
/// thread. The dynamic certificates must be provided and removed on the main thread, which other
/// threads reach through a scheduler. The handle borrows the Envoy side configuration, so a module
/// must drop every clone of it no later than the [`TlsCertificateSelectorConfig`] it was created
/// for, which outlives its selectors.
#[cfg_attr(any(test, feature = "mock"), automock)]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyTlsCertificateSelectorConfig: Send + Sync {
  /// Define a new counter with the given name and no labels.
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec with the given name and label names.
  fn define_counter_vec<'a>(
    &self,
    name: &str,
    label_names: &[&'a str],
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
    label_names: &[&'a str],
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
    label_names: &[&'a str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result>;

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
    label_values: &[&'a str],
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
    label_values: &[&'a str],
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
    label_values: &[&'a str],
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
    label_values: &[&'a str],
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
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Provides or replaces the dynamic certificate of the given name, which resumes the handshakes
  /// that wait for it.
  ///
  /// `certificate_chain` and `private_key` are PEM-encoded, and `ocsp_staple` is a DER-encoded
  /// OCSP response that can be empty. This returns false if the certificate is invalid, dynamic
  /// certificates are not allowed, or the TLS context is removed. A rejected certificate keeps the
  /// current one, and without a current one the handshakes that wait for it fail. When the TLS
  /// context waits for its SDS validation context, Envoy builds the certificate once the validation
  /// context arrives, and removes it if it is invalid. Envoy also rebuilds the certificate when the
  /// TLS context is updated, and removes it if the rebuild fails. This must be called on the main
  /// thread.
  fn set_certificate(
    &self,
    name: &str,
    certificate_chain: &[u8],
    private_key: &[u8],
    ocsp_staple: &[u8],
  ) -> bool;

  /// Removes the dynamic certificate of the given name, or declines one that is requested, which
  /// fails the handshakes that wait for it. This must be called on the main thread.
  fn remove_certificate(&self, name: &str);

  /// Creates a scheduler that posts events to this configuration on the main thread.
  fn new_scheduler(&self) -> Box<dyn EnvoyTlsCertificateSelectorConfigScheduler>;
}

/// This represents a thread-safe object that can be used to post an event to a certificate
/// selector configuration on the main thread.
///
/// The module can hold it on any thread, for example a Tokio worker, and call
/// [`EnvoyTlsCertificateSelectorConfigScheduler::commit`] to run
/// [`TlsCertificateSelectorConfig::on_scheduled`] on the main thread. It is created by
/// [`EnvoyTlsCertificateSelectorConfig::new_scheduler`] and releases the Envoy-side resource when
/// dropped.
#[cfg_attr(any(test, feature = "mock"), automock)]
pub trait EnvoyTlsCertificateSelectorConfigScheduler: Send + Sync {
  /// Schedules [`TlsCertificateSelectorConfig::on_scheduled`] to run on the main thread with the
  /// given `event_id`.
  ///
  /// This can be called multiple times to schedule multiple events. The event is dropped when the
  /// configuration is destroyed before it runs.
  fn commit(&self, event_id: u64);
}

/// Implementation of [`EnvoyTlsCertificateSelectorConfig`] that calls into the Envoy ABI.
pub(crate) struct EnvoyTlsCertificateSelectorConfigImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
}

// SAFETY: the configuration wraps an opaque handle to the Envoy-side configuration. The metrics and
// scheduler callbacks are safe to call from any thread, and the dynamic certificate callbacks
// reject calls off the main thread, by ABI contract.
unsafe impl Send for EnvoyTlsCertificateSelectorConfigImpl {}
unsafe impl Sync for EnvoyTlsCertificateSelectorConfigImpl {}

impl EnvoyTlsCertificateSelectorConfigImpl {
  fn define(
    &self,
    define_fn: unsafe extern "C" fn(
      abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
      abi::envoy_dynamic_module_type_module_buffer,
      *mut abi::envoy_dynamic_module_type_module_buffer,
      usize,
      *mut usize,
    ) -> abi::envoy_dynamic_module_type_metrics_result,
    name: &str,
    label_names: &[&str],
  ) -> Result<usize, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_names = strs_to_module_buffers(label_names);
    let mut id: usize = 0;
    Result::from(unsafe {
      define_fn(
        self.raw,
        str_to_module_buffer(name),
        label_names.as_mut_ptr(),
        label_names.len(),
        &mut id,
      )
    })?;
    Ok(id)
  }

  fn record(
    &self,
    record_fn: unsafe extern "C" fn(
      abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
      usize,
      *mut abi::envoy_dynamic_module_type_module_buffer,
      usize,
      u64,
    ) -> abi::envoy_dynamic_module_type_metrics_result,
    id: usize,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = strs_to_module_buffers(label_values);
    Result::from(unsafe {
      record_fn(
        self.raw,
        id,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }
}

impl EnvoyTlsCertificateSelectorConfig for EnvoyTlsCertificateSelectorConfigImpl {
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter,
        name,
        &[],
      )
      .map(EnvoyCounterId)
  }

  fn define_counter_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter,
        name,
        label_names,
      )
      .map(EnvoyCounterVecId)
  }

  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge,
        name,
        &[],
      )
      .map(EnvoyGaugeId)
  }

  fn define_gauge_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge,
        name,
        label_names,
      )
      .map(EnvoyGaugeVecId)
  }

  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram,
        name,
        &[],
      )
      .map(EnvoyHistogramId)
  }

  fn define_histogram_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result> {
    self
      .define(
        abi::envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram,
        name,
        label_names,
      )
      .map(EnvoyHistogramVecId)
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter,
      id.0,
      &[],
      value,
    )
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter,
      id.0,
      label_values,
      value,
    )
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge,
      id.0,
      &[],
      value,
    )
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge,
      id.0,
      label_values,
      value,
    )
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_increment_gauge,
      id.0,
      &[],
      value,
    )
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_increment_gauge,
      id.0,
      label_values,
      value,
    )
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_decrement_gauge,
      id.0,
      &[],
      value,
    )
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_decrement_gauge,
      id.0,
      label_values,
      value,
    )
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value,
      id.0,
      &[],
      value,
    )
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    self.record(
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value,
      id.0,
      label_values,
      value,
    )
  }

  fn set_certificate(
    &self,
    name: &str,
    certificate_chain: &[u8],
    private_key: &[u8],
    ocsp_staple: &[u8],
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate(
        self.raw,
        str_to_module_buffer(name),
        bytes_to_module_buffer(certificate_chain),
        bytes_to_module_buffer(private_key),
        bytes_to_module_buffer(ocsp_staple),
      )
    }
  }

  fn remove_certificate(&self, name: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate(
        self.raw,
        str_to_module_buffer(name),
      )
    }
  }

  fn new_scheduler(&self) -> Box<dyn EnvoyTlsCertificateSelectorConfigScheduler> {
    let raw = unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_new(self.raw)
    };
    Box::new(EnvoyTlsCertificateSelectorConfigSchedulerImpl { raw })
  }
}

/// This implements the [`EnvoyTlsCertificateSelectorConfigScheduler`] trait with the given raw
/// pointer to the Envoy certificate selector configuration scheduler object.
struct EnvoyTlsCertificateSelectorConfigSchedulerImpl {
  raw: abi::envoy_dynamic_module_type_tls_certificate_selector_config_scheduler_module_ptr,
}

// SAFETY: the scheduler wraps an opaque handle to the Envoy-side scheduler. Its commit() and
// delete() are safe to call from any thread, and concurrent commits are data-race-free, by ABI
// contract.
unsafe impl Send for EnvoyTlsCertificateSelectorConfigSchedulerImpl {}
unsafe impl Sync for EnvoyTlsCertificateSelectorConfigSchedulerImpl {}

impl Drop for EnvoyTlsCertificateSelectorConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_delete(self.raw);
    }
  }
}

impl EnvoyTlsCertificateSelectorConfigScheduler
  for Box<dyn EnvoyTlsCertificateSelectorConfigScheduler>
{
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

impl EnvoyTlsCertificateSelectorConfigScheduler for EnvoyTlsCertificateSelectorConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(
        self.raw, event_id,
      );
    }
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_config_new(
    config_envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
    is_upstream: bool,
  ) -> abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr {
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    init_tls_certificate_selector_config(
      name_str.as_ref(),
      config_slice,
      is_upstream,
      Arc::new(EnvoyTlsCertificateSelectorConfigImpl {
        raw: config_envoy_ptr,
      }),
      crate::NEW_TLS_CERTIFICATE_SELECTOR_CONFIG_FUNCTION
        .get()
        .expect("NEW_TLS_CERTIFICATE_SELECTOR_CONFIG_FUNCTION must be set"),
    )
  }
  on_panic = std::ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_tls_certificate_selector_config_new`].
pub(crate) fn init_tls_certificate_selector_config(
  name: &str,
  config: &[u8],
  is_upstream: bool,
  envoy_config: Arc<dyn EnvoyTlsCertificateSelectorConfig>,
  new_config_fn: &crate::NewTlsCertificateSelectorConfigFunction,
) -> abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr {
  match new_config_fn(name, config, is_upstream, envoy_config) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_config_destroy(
    config_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr,
  ) {
    drop_wrapped_c_void_ptr!(config_module_ptr, TlsCertificateSelectorConfig);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_new(
    config_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr,
    certificates: *const abi::envoy_dynamic_module_type_envoy_buffer,
    certificates_count: usize,
  ) -> abi::envoy_dynamic_module_type_tls_certificate_selector_module_ptr {
    let config = unsafe { &*(config_module_ptr as *const Box<dyn TlsCertificateSelectorConfig>) };
    let certificate_buffers =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(certificates, certificates_count) };
    let certificates: Vec<&[u8]> = certificate_buffers
      .iter()
      .map(|buffer| unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(buffer.ptr as *const u8, buffer.length)
      })
      .collect();
    match config.new_selector(&certificates) {
      Some(selector) => wrap_into_c_void_ptr!(selector),
      None => std::ptr::null(),
    }
  }
  on_panic = std::ptr::null()
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_destroy(
    selector_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_module_ptr,
  ) {
    drop_wrapped_c_void_ptr!(selector_module_ptr, TlsCertificateSelector);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_select(
    selector_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_module_ptr,
    context_envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr,
    certificate_index: *mut usize,
    certificate_name: *mut abi::envoy_dynamic_module_type_module_buffer,
  ) -> abi::envoy_dynamic_module_type_tls_certificate_selector_status {
    let selector = unsafe { &*(selector_module_ptr as *const Box<dyn TlsCertificateSelector>) };
    let context = unsafe { TlsCertificateSelectionContext::new(context_envoy_ptr) };
    match selector.select(&context) {
      TlsCertificateSelection::PassThrough => {
        abi::envoy_dynamic_module_type_tls_certificate_selector_status::PassThrough
      },
      TlsCertificateSelection::Configured(index) => {
        unsafe { *certificate_index = index };
        abi::envoy_dynamic_module_type_tls_certificate_selector_status::Configured
      },
      TlsCertificateSelection::Dynamic(name) => {
        CERTIFICATE_NAME_BUFFER.with(|cell| {
          let mut buffer = cell.borrow_mut();
          *buffer = name.into_bytes();
          unsafe { *certificate_name = bytes_to_module_buffer(buffer.as_slice()) };
        });
        abi::envoy_dynamic_module_type_tls_certificate_selector_status::Dynamic
      },
      TlsCertificateSelection::Failed => {
        abi::envoy_dynamic_module_type_tls_certificate_selector_status::Failed
      },
    }
  }
  // A panic during selection must not look like a selection, so fail closed.
  on_panic = abi::envoy_dynamic_module_type_tls_certificate_selector_status::Failed
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  #[allow(clippy::unused_unit)]
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_config_certificate_requested(
    config_envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
    config_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> () {
    let config = unsafe { &*(config_module_ptr as *const Box<dyn TlsCertificateSelectorConfig>) };
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let envoy_config = EnvoyTlsCertificateSelectorConfigImpl {
      raw: config_envoy_ptr,
    };
    config.on_certificate_requested(&envoy_config, name_str.as_ref());
  }
  // A panic must not leave the handshakes waiting for the certificate, so decline it even if the
  // module provided it before panicking.
  on_panic = unsafe {
    abi::envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate(
      config_envoy_ptr,
      abi::envoy_dynamic_module_type_module_buffer {
        ptr: name.ptr,
        length: name.length,
      },
    )
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_tls_certificate_selector_config_scheduled(
    config_envoy_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr,
    config_module_ptr: abi::envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr,
    event_id: u64,
  ) {
    let config = unsafe { &*(config_module_ptr as *const Box<dyn TlsCertificateSelectorConfig>) };
    let envoy_config = EnvoyTlsCertificateSelectorConfigImpl {
      raw: config_envoy_ptr,
    };
    config.on_scheduled(&envoy_config, event_id);
  }
}
