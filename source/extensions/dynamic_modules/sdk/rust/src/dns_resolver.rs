//! DNS resolver support for dynamic modules.
//!
//! This module provides traits and types for implementing custom DNS resolvers as dynamic
//! modules. A DNS resolver resolves domain names to IP addresses and is used by Envoy
//! for upstream cluster endpoint resolution.

use crate::{
  abi, drop_wrapped_c_void_ptr, str_to_module_buffer, strs_to_module_buffers, wrap_into_c_void_ptr,
  EnvoyCounterId, EnvoyCounterVecId, EnvoyGaugeId, EnvoyGaugeVecId, EnvoyHistogramId,
  EnvoyHistogramVecId,
};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Arc;

/// The DNS lookup family specifying which address families to look up.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DnsLookupFamily {
  V4Only,
  V6Only,
  Auto,
  V4Preferred,
  All,
}

/// The final status of a DNS resolution.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DnsResolutionStatus {
  Completed,
  Failure,
}

/// A single resolved DNS address with its TTL.
#[derive(Debug, Clone)]
pub struct DnsAddress {
  /// The resolved address in "ip:port" format (e.g., "1.2.3.4:0"). The port must always be 0
  /// because DNS resolution only produces IP addresses; the actual port comes from the
  /// cluster/endpoint configuration.
  pub address: String,
  /// The time-to-live in seconds for this record.
  pub ttl_seconds: u32,
}

/// The module-side DNS resolver configuration.
///
/// This trait must be implemented by the module to handle DNS resolver configuration.
/// The object is created when the corresponding Envoy DNS resolver configuration is loaded,
/// and dropped when the configuration is destroyed.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait DnsResolverConfig: Send + Sync {
  /// Create a new DNS resolver instance from this configuration.
  ///
  /// The `envoy_callback` is used by the resolver to deliver resolution results back to Envoy.
  /// It is safe to call from any thread.
  fn new_resolver(
    &self,
    envoy_callback: Arc<dyn EnvoyDnsResolverCallback>,
  ) -> Box<dyn DnsResolverInstance>;
}

/// The module-side DNS resolver instance.
///
/// This trait must be implemented by the module to perform DNS resolution.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads.
pub trait DnsResolverInstance: Send + Sync {
  /// Start an asynchronous DNS resolution.
  ///
  /// The module should start the resolution and return a query handle. When the resolution
  /// completes, the module must call `envoy_callback.resolve_complete()` with the `query_id`.
  ///
  /// Returns `Some(handle)` if the resolution was started, or `None` if it could not be started.
  fn resolve(
    &self,
    dns_name: &str,
    lookup_family: DnsLookupFamily,
    query_id: u64,
  ) -> Option<Box<dyn DnsActiveQuery>>;

  /// Reset the resolver's networking state, typically in response to a network change.
  fn reset_networking(&self) {}
}

/// A handle to an active DNS query that can be cancelled.
pub trait DnsActiveQuery: Send {
  /// Cancel this in-flight query. After this call, the module must not deliver results
  /// for this query.
  fn cancel(&mut self);
}

/// Envoy-side callback for delivering DNS resolution results.
///
/// This is safe to call from any thread. The C++ shell handles posting to the correct
/// Envoy dispatcher thread.
pub trait EnvoyDnsResolverCallback: Send + Sync {
  /// Deliver the result of a DNS resolution.
  fn resolve_complete(
    &self,
    query_id: u64,
    status: DnsResolutionStatus,
    details: &str,
    addresses: &[DnsAddress],
  );
}

/// Envoy-side configuration interface for DNS resolver modules.
///
/// This provides access to metric-defining and metric-recording callbacks scoped to
/// the DNS resolver configuration. The caller receives an `Arc` so it can be stored
/// and used at runtime (e.g., during resolution) for recording metrics.
pub trait EnvoyDnsResolverConfig: Send + Sync {
  // -------------------------------------------------------------------------
  // Define metrics (call during config creation).
  // -------------------------------------------------------------------------

  /// Define a new counter with the given name and no labels.
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec with the given name and label names.
  fn define_counter_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge with the given name and no labels.
  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec with the given name and label names.
  fn define_gauge_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram with the given name and no labels.
  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec with the given name and label names.
  fn define_histogram_vec(
    &self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result>;

  // -------------------------------------------------------------------------
  // Record metrics (call during runtime).
  // -------------------------------------------------------------------------

  /// Increment a previously defined counter by the given value.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increment a previously defined counter vec by the given value with label values.
  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of a previously defined gauge vec with label values.
  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge by the given value.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a previously defined gauge vec by the given value with label values.
  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge by the given value.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a previously defined gauge vec by the given value with label values.
  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value for a previously defined histogram.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value for a previously defined histogram vec with label values.
  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;
}

// -- Internal Implementation Types --

struct EnvoyDnsResolverCallbackImpl {
  resolver_envoy_ptr: abi::envoy_dynamic_module_type_dns_resolver_envoy_ptr,
}

// SAFETY: The raw pointer is an opaque handle to an Envoy-side object that is thread-safe to
// invoke callbacks on. The ABI guarantees thread-safe access.
unsafe impl Send for EnvoyDnsResolverCallbackImpl {}
unsafe impl Sync for EnvoyDnsResolverCallbackImpl {}

impl EnvoyDnsResolverCallback for EnvoyDnsResolverCallbackImpl {
  fn resolve_complete(
    &self,
    query_id: u64,
    status: DnsResolutionStatus,
    details: &str,
    addresses: &[DnsAddress],
  ) {
    let abi_status = match status {
      DnsResolutionStatus::Completed => {
        abi::envoy_dynamic_module_type_dns_resolution_status::Completed
      },
      DnsResolutionStatus::Failure => abi::envoy_dynamic_module_type_dns_resolution_status::Failure,
    };

    let abi_addresses: Vec<abi::envoy_dynamic_module_type_dns_address> = addresses
      .iter()
      .map(|a| abi::envoy_dynamic_module_type_dns_address {
        address_ptr: a.address.as_ptr() as *const _,
        address_length: a.address.len(),
        ttl_seconds: a.ttl_seconds,
      })
      .collect();

    let details_buf = str_to_module_buffer(details);
    let addresses_ptr = if abi_addresses.is_empty() {
      std::ptr::null()
    } else {
      abi_addresses.as_ptr()
    };

    unsafe {
      abi::envoy_dynamic_module_callback_dns_resolve_complete(
        self.resolver_envoy_ptr,
        query_id,
        abi_status,
        details_buf,
        addresses_ptr,
        abi_addresses.len(),
      );
    }
  }
}

struct EnvoyDnsResolverConfigImpl {
  raw: abi::envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
}

// SAFETY: The raw pointer is an opaque handle to the Envoy-side DNS resolver configuration which
// provides thread-safe metric operations. The ABI guarantees thread-safe access.
unsafe impl Send for EnvoyDnsResolverConfigImpl {}
unsafe impl Sync for EnvoyDnsResolverConfigImpl {}

fn dns_metric_result_to_rust(
  res: abi::envoy_dynamic_module_type_metrics_result,
) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
  if res == abi::envoy_dynamic_module_type_metrics_result::Success {
    Ok(())
  } else {
    Err(res)
  }
}

impl EnvoyDnsResolverConfig for EnvoyDnsResolverConfigImpl {
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_counter(
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
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_counter(
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
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_gauge(
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
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_gauge(
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
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_histogram(
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
      abi::envoy_dynamic_module_callback_dns_resolver_config_define_histogram(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_increment_counter(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_increment_counter(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_set_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_set_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(
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
    dns_metric_result_to_rust(unsafe {
      abi::envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(
        self.raw,
        id,
        label_bufs.as_mut_ptr(),
        labels.len(),
        value,
      )
    })
  }
}

/// Wraps a module-side resolver with the Envoy callback for delivering results.
struct DnsResolverWrapper {
  resolver: Box<dyn DnsResolverInstance>,
}

// -- FFI Event Hook Implementations --

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolver_config_new(
  config_envoy_ptr: abi::envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_dns_resolver_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    // SAFETY: Envoy guarantees name and config are valid UTF-8 per the ABI contract.
    let name_str = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        name.ptr as *const u8,
        name.length,
      ))
    };
    let config_slice =
      unsafe { std::slice::from_raw_parts(config.ptr as *const u8, config.length) };
    let new_config_fn = crate::NEW_DNS_RESOLVER_CONFIG_FUNCTION
      .get()
      .expect("NEW_DNS_RESOLVER_CONFIG_FUNCTION must be set");
    let envoy_dns_resolver_config: Arc<dyn EnvoyDnsResolverConfig> =
      Arc::new(EnvoyDnsResolverConfigImpl {
        raw: config_envoy_ptr,
      });
    match new_config_fn(name_str, config_slice, envoy_dns_resolver_config) {
      Some(config) => wrap_into_c_void_ptr!(config),
      None => std::ptr::null(),
    }
  }))
  .unwrap_or_else(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolver_config_new", panic);
    std::ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolver_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(config_module_ptr, DnsResolverConfig);
  }))
  .map_err(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolver_config_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolver_new(
  config_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_config_module_ptr,
  resolver_envoy_ptr: abi::envoy_dynamic_module_type_dns_resolver_envoy_ptr,
) -> abi::envoy_dynamic_module_type_dns_resolver_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let config = config_module_ptr as *const *const dyn DnsResolverConfig;
    let config = unsafe { &**config };
    let envoy_callback: Arc<dyn EnvoyDnsResolverCallback> =
      Arc::new(EnvoyDnsResolverCallbackImpl { resolver_envoy_ptr });
    let resolver = config.new_resolver(envoy_callback);
    let wrapper = Box::new(DnsResolverWrapper { resolver });
    Box::into_raw(wrapper) as abi::envoy_dynamic_module_type_dns_resolver_module_ptr
  }))
  .unwrap_or_else(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolver_new", panic);
    std::ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolver_destroy(
  resolver_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = resolver_module_ptr as *mut DnsResolverWrapper;
    let _ = unsafe { Box::from_raw(wrapper) };
  }))
  .map_err(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolver_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolve(
  resolver_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_module_ptr,
  dns_name: abi::envoy_dynamic_module_type_envoy_buffer,
  lookup_family: abi::envoy_dynamic_module_type_dns_lookup_family,
  query_id: u64,
) -> abi::envoy_dynamic_module_type_dns_query_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(resolver_module_ptr as *const DnsResolverWrapper) };
    let name_str = unsafe {
      std::str::from_utf8_unchecked(std::slice::from_raw_parts(
        dns_name.ptr as *const u8,
        dns_name.length,
      ))
    };

    let family = match lookup_family {
      abi::envoy_dynamic_module_type_dns_lookup_family::V4Only => DnsLookupFamily::V4Only,
      abi::envoy_dynamic_module_type_dns_lookup_family::V6Only => DnsLookupFamily::V6Only,
      abi::envoy_dynamic_module_type_dns_lookup_family::Auto => DnsLookupFamily::Auto,
      abi::envoy_dynamic_module_type_dns_lookup_family::V4Preferred => DnsLookupFamily::V4Preferred,
      abi::envoy_dynamic_module_type_dns_lookup_family::All => DnsLookupFamily::All,
    };

    match wrapper.resolver.resolve(name_str, family, query_id) {
      Some(query) => {
        let boxed = Box::new(query);
        Box::into_raw(boxed) as abi::envoy_dynamic_module_type_dns_query_module_ptr
      },
      None => std::ptr::null_mut(),
    }
  }))
  .unwrap_or_else(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolve", panic);
    std::ptr::null_mut()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolve_cancel(
  _resolver_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_module_ptr,
  query_module_ptr: abi::envoy_dynamic_module_type_dns_query_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let query = query_module_ptr as *mut Box<dyn DnsActiveQuery>;
    let mut query = unsafe { Box::from_raw(query) };
    query.cancel();
  }))
  .map_err(|panic| {
    log_panic("envoy_dynamic_module_on_dns_resolve_cancel", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_dns_resolver_reset_networking(
  resolver_module_ptr: abi::envoy_dynamic_module_type_dns_resolver_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let wrapper = unsafe { &*(resolver_module_ptr as *const DnsResolverWrapper) };
    wrapper.resolver.reset_networking();
  }))
  .map_err(|panic| {
    log_panic(
      "envoy_dynamic_module_on_dns_resolver_reset_networking",
      panic,
    );
  });
}

/// Log a panic caught at an FFI boundary.
fn log_panic(function_name: &str, panic: Box<dyn std::any::Any + Send>) {
  crate::envoy_log_error!(
    "{}: caught panic: {}",
    function_name,
    crate::panic_payload_to_string(panic)
  );
}
