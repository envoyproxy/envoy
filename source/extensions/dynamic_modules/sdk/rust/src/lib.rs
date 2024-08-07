#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

mod abi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

/// Declare the entry point for the dynamic module. This function is called when the dynamic module is loaded.
/// The function must return 0 on success, and any other value on failure. When the function returns a non-zero value,
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
/// fn my_program_init() -> i32 {
///    0
/// }
/// ```
#[macro_export]
macro_rules! declare_program_init {
    ($f:ident) => {
        #[no_mangle]
        pub extern "C" fn envoy_dynamic_module_on_program_init() -> i32 {
            $f()
        }
    };
}
