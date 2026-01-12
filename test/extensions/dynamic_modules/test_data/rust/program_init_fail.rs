use envoy_proxy_dynamic_modules_rust_sdk::*;

#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_program_init(
  _server_factory_context: abi::envoy_dynamic_module_type_server_factory_context_envoy_ptr,
) -> *const ::std::os::raw::c_char {
  std::ptr::null()
}
