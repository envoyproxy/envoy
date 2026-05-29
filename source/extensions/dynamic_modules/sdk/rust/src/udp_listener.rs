use crate::buffer::EnvoyBuffer;
use crate::{
  abi, bytes_to_module_buffer, drop_wrapped_c_void_ptr, str_to_module_buffer, wrap_into_c_void_ptr,
  EnvoyCounterId, EnvoyGaugeId, EnvoyHistogramId, NewUdpListenerFilterConfigFunction,
  NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION,
};
use mockall::*;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// The trait that represents the Envoy UDP listener filter configuration.
/// This is used in [`NewUdpListenerFilterConfigFunction`] to pass the Envoy filter configuration
/// to the dynamic module.
#[automock]
pub trait EnvoyUdpListenerFilterConfig {
  /// Define a new counter scoped to this filter config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this filter config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this filter config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;
}

/// The trait that represents the configuration for an Envoy UDP listener filter configuration.
/// This has one to one mapping with the [`EnvoyUdpListenerFilterConfig`] object.
///
/// The object is created when the corresponding Envoy UDP listener filter config is created, and it
/// is dropped when the corresponding Envoy UDP listener filter config is destroyed. Therefore, the
/// implementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
///
/// Implementations must also be `Sync` since they are accessed from worker threads.
pub trait UdpListenerFilterConfig<ELF: EnvoyUdpListenerFilter>: Sync {
  /// This is called from a worker thread when a new UDP session/filter is created.
  fn new_udp_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn UdpListenerFilter<ELF>>;
}

/// The trait that corresponds to an Envoy UDP listener filter for each accepted connection/session
/// created via the [`UdpListenerFilterConfig::new_udp_listener_filter`] method.
pub trait UdpListenerFilter<ELF: EnvoyUdpListenerFilter> {
  /// This is called when a UDP packet is received.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_udp_listener_filter_status`] to
  /// indicate the status of the data processing.
  fn on_data(
    &mut self,
    _envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_udp_listener_filter_status {
    abi::envoy_dynamic_module_type_on_udp_listener_filter_status::Continue
  }
}

/// The trait that represents the Envoy UDP listener filter.
/// This is used in [`UdpListenerFilter`] to interact with the underlying Envoy UDP listener filter
/// object.
#[automock]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyUdpListenerFilter {
  /// Get the current datagram data as chunks.
  /// Returns a tuple of (chunks, total_length).
  fn get_datagram_data<'a>(&'a self) -> (Vec<EnvoyBuffer<'a>>, usize);

  /// Set the current datagram data.
  /// Returns true if successful.
  fn set_datagram_data(&mut self, data: &[u8]) -> bool;

  /// Get the peer address and port.
  /// Returns None if the address is not available or not an IP address.
  fn get_peer_address(&self) -> Option<(String, u32)>;

  /// Get the local address and port.
  /// Returns None if the address is not available or not an IP address.
  fn get_local_address(&self) -> Option<(String, u32)>;

  /// Send a datagram.
  /// Returns true if successful.
  fn send_datagram(&mut self, data: &[u8], peer_address: &str, peer_port: u32) -> bool;

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Get the index of the current worker thread.
  fn get_worker_index(&self) -> u32;
}

/// The implementation of [`EnvoyUdpListenerFilterConfig`] for the Envoy UDP listener filter
/// configuration.
pub struct EnvoyUdpListenerFilterConfigImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
}

impl EnvoyUdpListenerFilterConfigImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyUdpListenerFilterConfig for EnvoyUdpListenerFilterConfigImpl {
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
        self.raw,
        str_to_module_buffer(name),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }
}

/// The implementation of [`EnvoyUdpListenerFilter`] for the Envoy UDP listener filter.
pub struct EnvoyUdpListenerFilterImpl {
  pub(crate) raw: abi::envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
}

impl EnvoyUdpListenerFilterImpl {
  pub fn new(raw: abi::envoy_dynamic_module_type_udp_listener_filter_envoy_ptr) -> Self {
    Self { raw }
  }
}

impl EnvoyUdpListenerFilter for EnvoyUdpListenerFilterImpl {
  fn get_datagram_data(&self) -> (Vec<EnvoyBuffer<'_>>, usize) {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(self.raw)
    };
    if size == 0 {
      return (Vec::new(), 0);
    }
    let mut buffers = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      size
    ];
    let ok = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
        self.raw,
        buffers.as_mut_ptr(),
      )
    };
    if !ok {
      return (Vec::new(), 0);
    }

    let total_length = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(self.raw)
    };
    if total_length == 0 {
      // This shouldn't happen if chunks were retrieved, but for safety:
      return (Vec::new(), 0);
    }

    let envoy_buffers: Vec<EnvoyBuffer> = buffers
      .into_iter()
      .map(|b| unsafe { EnvoyBuffer::new_from_raw(b.ptr as *const _, b.length) })
      .collect();
    (envoy_buffers, total_length)
  }

  fn set_datagram_data(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
        self.raw,
        bytes_to_module_buffer(data),
      )
    }
  }

  fn get_peer_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn get_local_address(&self) -> Option<(String, u32)> {
    let mut address = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let mut port: u32 = 0;
    let result = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
        self.raw,
        &mut address as *mut _ as *mut _,
        &mut port,
      )
    };
    if !result || address.length == 0 || address.ptr.is_null() {
      return None;
    }
    let address_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(address.ptr as *const u8, address.length) };
    Some((address_str.into_owned(), port))
  }

  fn send_datagram(&mut self, data: &[u8], peer_address: &str, peer_port: u32) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
        self.raw,
        bytes_to_module_buffer(data),
        str_to_module_buffer(peer_address),
        peer_port,
      )
    }
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_increment_counter(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_set_gauge(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(self.raw, id, value)
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
        self.raw, id, value,
      )
    };
    if res == abi::envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn get_worker_index(&self) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(self.raw) }
  }
}

// UDP Listener Filter Event Hook Implementations

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_udp_listener_filter_config_new(
  envoy_filter_config_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_udp_listener_filter_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter_config = EnvoyUdpListenerFilterConfigImpl::new(envoy_filter_config_ptr);
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    init_udp_listener_filter_config(
      &mut envoy_filter_config,
      name_str.as_ref(),
      config_slice,
      NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_UDP_LISTENER_FILTER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_udp_listener_filter_config_new",
      panic,
    );
    std::ptr::null()
  })
}

pub(crate) fn init_udp_listener_filter_config<
  EC: EnvoyUdpListenerFilterConfig,
  ELF: EnvoyUdpListenerFilter,
>(
  envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
  new_listener_filter_config_fn: &NewUdpListenerFilterConfigFunction<EC, ELF>,
) -> abi::envoy_dynamic_module_type_udp_listener_filter_config_module_ptr {
  let listener_filter_config = new_listener_filter_config_fn(envoy_filter_config, name, config);
  match listener_filter_config {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_udp_listener_filter_config_destroy(
  filter_config_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(
      filter_config_ptr,
      UdpListenerFilterConfig<EnvoyUdpListenerFilterImpl>
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_udp_listener_filter_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_udp_listener_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
  envoy_filter_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_udp_listener_filter_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter = EnvoyUdpListenerFilterImpl::new(envoy_filter_ptr);
    let filter_config = {
      let raw =
        filter_config_ptr as *const *const dyn UdpListenerFilterConfig<EnvoyUdpListenerFilterImpl>;
      &**raw
    };
    envoy_dynamic_module_on_udp_listener_filter_new_impl(&mut envoy_filter, filter_config)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_udp_listener_filter_new", panic);
    std::ptr::null()
  })
}

pub(crate) fn envoy_dynamic_module_on_udp_listener_filter_new_impl(
  envoy_filter: &mut EnvoyUdpListenerFilterImpl,
  filter_config: &dyn UdpListenerFilterConfig<EnvoyUdpListenerFilterImpl>,
) -> abi::envoy_dynamic_module_type_udp_listener_filter_module_ptr {
  let filter = filter_config.new_udp_listener_filter(envoy_filter);
  wrap_into_c_void_ptr!(filter)
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_udp_listener_filter_on_data(
  envoy_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_udp_listener_filter_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut Box<dyn UdpListenerFilter<EnvoyUdpListenerFilterImpl>>;
    let filter = unsafe { &mut *filter };
    filter.on_data(&mut EnvoyUdpListenerFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_udp_listener_filter_on_data", panic);
    abi::envoy_dynamic_module_type_on_udp_listener_filter_status::StopIteration
  })
}

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_udp_listener_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_udp_listener_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let _ = unsafe {
      Box::from_raw(filter_ptr as *mut Box<dyn UdpListenerFilter<EnvoyUdpListenerFilterImpl>>)
    };
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_udp_listener_filter_destroy", panic);
  });
}
