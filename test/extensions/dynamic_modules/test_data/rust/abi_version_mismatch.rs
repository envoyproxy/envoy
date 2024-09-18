#[no_mangle]
pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
    b"invalid-version-hash\0".as_ptr() as *const ::std::os::raw::c_char
}
