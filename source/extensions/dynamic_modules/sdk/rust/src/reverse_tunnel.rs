/// Trait implemented by any type that handles reverse tunnel lifecycle events.
///
/// All methods are invoked on Envoy's main thread.
pub trait ReverseTunnelReporter: Sized + 'static {
  /// Called by Envoy to construct the reporter from opaque config bytes.
  fn new(config: &[u8]) -> Self;

  /// Called once after Envoy completes server initialization.
  fn on_server_initialized(&mut self);

  /// Called when a reverse tunnel connection is established.
  fn on_connected(&mut self, node_id: &str, cluster_id: &str, tenant_id: &str);

  /// Called when a reverse tunnel connection is torn down.
  fn on_disconnected(&mut self, node_id: &str, cluster_id: &str);
}

/// Wires a type implementing [`ReverseTunnelReporter`] to the five reverse tunnel ABI hooks.
///
/// # Example
/// ```ignore
/// struct MyReporter;
/// impl envoy_proxy_dynamic_modules_rust_sdk::ReverseTunnelReporter for MyReporter {
///   fn new(_config: &[u8]) -> Self { MyReporter }
///   fn on_server_initialized(&mut self) {}
///   fn on_connected(&mut self, _n: &str, _c: &str, _t: &str) {}
///   fn on_disconnected(&mut self, _n: &str, _c: &str) {}
/// }
/// declare_reverse_tunnel_reporter!(MyReporter);
/// ```
#[macro_export]
macro_rules! declare_reverse_tunnel_reporter {
  ($ty:ty) => {
    #[no_mangle]
    pub unsafe extern "C" fn envoy_dynamic_module_on_reverse_tunnel_reporter_new(
      reporter_config: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> *mut ::std::ffi::c_void {
      let config =
        ::std::slice::from_raw_parts(reporter_config.ptr as *const u8, reporter_config.length);
      let reporter = <$ty as $crate::reverse_tunnel::ReverseTunnelReporter>::new(config);
      Box::into_raw(Box::new(reporter)) as *mut ::std::ffi::c_void
    }

    #[no_mangle]
    pub unsafe extern "C" fn envoy_dynamic_module_on_reverse_tunnel_reporter_destroy(
      reporter_ptr: *mut ::std::ffi::c_void,
    ) {
      if !reporter_ptr.is_null() {
        drop(Box::from_raw(reporter_ptr as *mut $ty));
      }
    }

    #[no_mangle]
    pub unsafe extern "C" fn envoy_dynamic_module_on_reverse_tunnel_server_initialized(
      reporter_ptr: *mut ::std::ffi::c_void,
    ) {
      let reporter = &mut *(reporter_ptr as *mut $ty);
      <$ty as $crate::reverse_tunnel::ReverseTunnelReporter>::on_server_initialized(reporter);
    }

    #[no_mangle]
    pub unsafe extern "C" fn envoy_dynamic_module_on_reverse_tunnel_connected(
      reporter_ptr: *mut ::std::ffi::c_void,
      node_id: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      cluster_id: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      tenant_id: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) {
      let reporter = &mut *(reporter_ptr as *mut $ty);
      let node_str = ::std::str::from_utf8(::std::slice::from_raw_parts(
        node_id.ptr as *const u8,
        node_id.length,
      ))
      .unwrap_or("");
      let cluster_str = ::std::str::from_utf8(::std::slice::from_raw_parts(
        cluster_id.ptr as *const u8,
        cluster_id.length,
      ))
      .unwrap_or("");
      let tenant_str = ::std::str::from_utf8(::std::slice::from_raw_parts(
        tenant_id.ptr as *const u8,
        tenant_id.length,
      ))
      .unwrap_or("");
      <$ty as $crate::reverse_tunnel::ReverseTunnelReporter>::on_connected(
        reporter,
        node_str,
        cluster_str,
        tenant_str,
      );
    }

    #[no_mangle]
    pub unsafe extern "C" fn envoy_dynamic_module_on_reverse_tunnel_disconnected(
      reporter_ptr: *mut ::std::ffi::c_void,
      node_id: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
      cluster_id: $crate::abi::envoy_dynamic_module_type_envoy_buffer,
    ) {
      let reporter = &mut *(reporter_ptr as *mut $ty);
      let node_str = ::std::str::from_utf8(::std::slice::from_raw_parts(
        node_id.ptr as *const u8,
        node_id.length,
      ))
      .unwrap_or("");
      let cluster_str = ::std::str::from_utf8(::std::slice::from_raw_parts(
        cluster_id.ptr as *const u8,
        cluster_id.length,
      ))
      .unwrap_or("");
      <$ty as $crate::reverse_tunnel::ReverseTunnelReporter>::on_disconnected(
        reporter,
        node_str,
        cluster_str,
      );
    }
  };
}
