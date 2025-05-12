#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
  c"invalid-version-hash".as_ptr() as *const ::std::os::raw::c_char
}
