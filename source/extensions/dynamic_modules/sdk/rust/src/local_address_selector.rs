//! Local address selector support for dynamic modules.
//!
//! A local address selector picks the source address of each upstream connection of a cluster. The
//! entry point is the `local_address_selector:` arm of [`crate::declare_all_init_functions!`],
//! which registers a factory through [`crate::NEW_LOCAL_ADDRESS_SELECTOR_CONFIG_FUNCTION`] and lets
//! a single module dispatch by `selector_name`.

use crate::{abi, envoy_log_warn, ffi_export};
use std::ffi::c_void;
use std::net::{IpAddr, SocketAddr};
use std::ptr;

/// The source address of an upstream connection, as selected by
/// [`LocalAddressSelectorConfig::select`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LocalAddressSelection {
  /// Keep the address that the default local address selector picks from the cluster's bind
  /// config, as if no local address selector were configured.
  Default,
  /// Bind the connection to this source address and port. A port of 0 lets the kernel pick the
  /// port at connect time. The address must have the same IP family as the endpoint, or the
  /// connection fails.
  Address(IpAddr, u16),
  /// Fail the connection. Envoy closes it before it connects, and the connection pool handles it
  /// as a local origin connect failure of the host. It fails the requests waiting for a
  /// connection to the host, and outlier detection counts the failure against the host, as a 503
  /// response unless the cluster's outlier detection sets `split_external_local_origin_errors`.
  /// A module that fails the connections of a healthy host can therefore get the host ejected.
  Fail,
}

/// Configuration of a local address selector.
///
/// Envoy creates one configuration per cluster on the main thread, and every connection of the
/// cluster shares it. [`LocalAddressSelectorConfig::select`] runs concurrently on worker threads
/// and on the main thread, so the configuration must be `Send + Sync` and read-only during
/// selection. It is dropped with the last reference to the cluster, which can happen on any thread.
pub trait LocalAddressSelectorConfig: Send + Sync {
  /// Selects the source address of an upstream connection to `endpoint`.
  ///
  /// Envoy calls this once per connection attempt, including each happy eyeballs attempt, and only
  /// for IP endpoints.
  fn select(&self, endpoint: SocketAddr) -> LocalAddressSelection;
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_local_address_selector_config_new(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_local_address_selector_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
    cluster_name: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> abi::envoy_dynamic_module_type_local_address_selector_config_module_ptr {
    // SAFETY: `name` and `cluster_name` are protobuf strings (UTF-8 by contract) and `config` is
    // opaque bytes. The helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for
    // malformed UTF-8 rather than triggering UB.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    let cluster_name_str = unsafe {
      crate::ffi_helpers::str_lossy_from_raw(cluster_name.ptr as *const u8, cluster_name.length)
    };

    envoy_dynamic_module_on_local_address_selector_config_new_impl(
      name_str.as_ref(),
      config_bytes,
      cluster_name_str.as_ref(),
      crate::NEW_LOCAL_ADDRESS_SELECTOR_CONFIG_FUNCTION
        .get()
        .expect("NEW_LOCAL_ADDRESS_SELECTOR_CONFIG_FUNCTION must be set"),
    )
  }
  // Envoy rejects the cluster when the configuration cannot be created.
  on_panic = ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_local_address_selector_config_new`].
///
/// The FFI entry point decodes the buffers and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_local_address_selector_config_new_impl(
  name: &str,
  config: &[u8],
  cluster_name: &str,
  new_fn: &crate::NewLocalAddressSelectorConfigFunction,
) -> *const c_void {
  match new_fn(name, config, cluster_name) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_local_address_selector_config_destroy(
    config_module_ptr: abi::envoy_dynamic_module_type_local_address_selector_config_module_ptr,
  ) {
    crate::drop_wrapped_c_void_ptr!(config_module_ptr, LocalAddressSelectorConfig);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_local_address_selector_select(
    config_module_ptr: abi::envoy_dynamic_module_type_local_address_selector_config_module_ptr,
    selector_envoy_ptr: abi::envoy_dynamic_module_type_local_address_selector_envoy_ptr,
    endpoint_address: abi::envoy_dynamic_module_type_envoy_buffer,
    endpoint_port: u32,
  ) -> bool {
    let config = unsafe { &*(config_module_ptr as *const Box<dyn LocalAddressSelectorConfig>) };
    let endpoint_ip = unsafe {
      crate::ffi_helpers::str_lossy_from_raw(
        endpoint_address.ptr as *const u8,
        endpoint_address.length,
      )
    };
    let Some(endpoint) = parse_endpoint(endpoint_ip.as_ref(), endpoint_port) else {
      envoy_log_warn!(
        "local address selector got an invalid endpoint {}:{}, failing the connection",
        endpoint_ip,
        endpoint_port
      );
      return false;
    };
    apply_selection(selector_envoy_ptr, config.select(endpoint))
  }
  // A panic during selection must not look like a decision, so fail the connection.
  on_panic = false
}

fn parse_endpoint(ip: &str, port: u32) -> Option<SocketAddr> {
  let ip = ip.parse::<IpAddr>().ok()?;
  let port = u16::try_from(port).ok()?;
  Some(SocketAddr::new(ip, port))
}

/// Translates a selection into the return value of the select hook, recording the address with
/// Envoy when the module picked one.
fn apply_selection(
  selector_envoy_ptr: abi::envoy_dynamic_module_type_local_address_selector_envoy_ptr,
  selection: LocalAddressSelection,
) -> bool {
  match selection {
    LocalAddressSelection::Default => true,
    LocalAddressSelection::Fail => false,
    LocalAddressSelection::Address(ip, port) => {
      // `IpAddr` formats IPv6 without brackets, which is what Envoy expects.
      let ip = ip.to_string();
      unsafe {
        abi::envoy_dynamic_module_callback_local_address_selector_set_address(
          selector_envoy_ptr,
          abi::envoy_dynamic_module_type_module_buffer {
            ptr: ip.as_ptr() as *const _,
            length: ip.len(),
          },
          u32::from(port),
        )
      }
    },
  }
}
