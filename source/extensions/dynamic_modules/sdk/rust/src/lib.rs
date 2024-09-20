#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

/// This module contains the generated bindings for the envoy dynamic modules ABI.
///
/// This is not meant to be used directly.
pub mod abi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

/// Declare the init function for the dynamic module. This function is called when the dynamic module is loaded.
/// The function must return true on success, and false on failure. When it returns false,
/// the dynamic module will not be loaded.
///
/// This is useful to perform any process-wide initialization that the dynamic module needs.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::declare_program_init;
///
/// declare_program_init!(my_program_init);
///
/// fn my_program_init() -> bool {
///    true
/// }
/// ```
#[macro_export]
macro_rules! declare_program_init {
    ($f:ident) => {
        #[no_mangle]
        pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
            if ($f()) {
                envoy_proxy_dynamic_modules_rust_sdk::abi::kAbiVersion.as_ptr()
                    as *const ::std::os::raw::c_char
            } else {
                ::std::ptr::null()
            }
        }
    };
}
