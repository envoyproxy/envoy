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

/// Declare the init functions for the dynamic module.
///
/// The first argument has [`ProgramInitFunction`] type, and it is called when the dynamic module is loaded.
///
/// The second argument has [`NewHttpFilterConfigFunction`] type, and it is called when the new HTTP filter configuration is created.
///
/// # Example
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// declare_init_functions!(my_program_init, my_new_http_filter_config_fn);
///
/// fn my_program_init() -> bool {
///    true
/// }
///
/// fn my_new_http_filter_config_fn(
///   _envoy_filter_factory: EnvoyHttpConfig,
///   _name: &str,
///   _config: &str,
/// ) -> Option<Box<dyn HttpConfig>> {
///   Some(Box::new(MyHttpConfig {}))
/// }
///
/// struct MyHttpConfig {}
///
/// impl HttpConfig for MyHttpConfig {}
/// ```
#[macro_export]
macro_rules! declare_init_functions {
    ($f:ident,$new_http_filter_config_fn:expr) => {
        #[no_mangle]
        pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
            unsafe {
                // We can assume that this is only called once at the beginning of the program, so it is safe to set a mutable global variable.
                envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION =
                    $new_http_filter_config_fn
            };
            if ($f()) {
                envoy_proxy_dynamic_modules_rust_sdk::abi::kAbiVersion.as_ptr()
                    as *const ::std::os::raw::c_char
            } else {
                ::std::ptr::null()
            }
        }
    };
}

/// The function signature for the program init function.
///
/// This is called when the dynamic module is loaded, and it must return true on success, and false on failure. When it returns false,
/// the dynamic module will not be loaded.
///
/// This is useful to perform any process-wide initialization that the dynamic module needs.
pub type ProgramInitFunction = fn() -> bool;

/// The function signature for the new HTTP filter configuration function.
///
/// This is called when a new HTTP filter configuration is created, and it must return a new instance of the [`HttpConfig`] object.
/// Returning `None` will cause the HTTP filter configuration to be rejected.
//
// TODO(@mathetake): I guess there would be a way to avoid the use of dyn in the first place. E.g. one idea is to accept a type parameter declare_init_functions! macro.
pub type NewHttpFilterConfigFunction = fn(
    envoy_filter_factory: EnvoyHttpConfig,
    name: &str,
    config: &str,
) -> Option<Box<dyn HttpConfig>>;

/// The global init function for HTTP filter configurations. This is set via the `declare_init_functions` macro,
/// and is not intended to be set directly.
pub static mut NEW_HTTP_FILTER_CONFIG_FUNCTION: NewHttpFilterConfigFunction = |_, _, _| {
    panic!("NEW_HTTP_FILTER_CONFIG_FUNCTION is not set");
};

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// destroyed when the corresponding Envoy Http filter config is destroyed.
pub trait HttpConfig {
    /// This is called when a HTTP filter chain is created for a new stream.
    fn new_http_filter(&self) -> Box<dyn HttpFilter> {
        unimplemented!() // TODO.
    }

    /// This is called when the corresponding Envoy Http filter config is destroyed/unloaded.
    ///
    /// After this returns, this object is destructed.
    //
    // NOTE: alternatively, we can replace this with `Drop` trait, but this allows us to clarify the lifetime of the
    // object and explain exactly when the object is destroyed.
    fn destroy(&mut self) {}
}

/// The trait that represents an Envoy Http filter for each stream.
pub trait HttpFilter {} // TODO.

/// An opaque object that represents the underlying Envoy Http filter config. This has one to one
/// mapping with the Envoy Http filter config object as well as [`HttpConfig`] object.
///
/// This is a shallow wrapper around the raw pointer to the Envoy HTTP filter object, and it
/// can be copied and stored somewhere else up until the corresponding [`HttpConfig::destroy`]
/// for the corresponding [`HttpConfig`] is called.
//
// TODO(@mathetake): make this only avaialble for non-test code, and provide a mock for testing. So that users
// can write a unit tests for their HttpConfig implementations.
#[derive(Debug, Clone, Copy)]
pub struct EnvoyHttpConfig {
    raw_addr: abi::envoy_dynamic_module_type_http_filter_config_in_envoy_ptr,
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_new(
    envoy_filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_in_envoy_ptr,
    name_ptr: *const ::std::os::raw::c_char,
    name_size: usize,
    config_ptr: *const ::std::os::raw::c_char,
    config_size: usize,
) -> abi::envoy_dynamic_module_type_http_filter_config_in_module_ptr {
    // This assume that the name and config are valid UTF-8 strings. Should we relax? At the moment, both are String at protobuf level.
    let name = if name_size > 0 {
        let slice = std::slice::from_raw_parts(name_ptr as *const u8, name_size);
        std::str::from_utf8(slice).unwrap()
    } else {
        ""
    };
    let config = if config_size > 0 {
        let slice = std::slice::from_raw_parts(config_ptr as *const u8, config_size);
        std::str::from_utf8(slice).unwrap()
    } else {
        ""
    };

    let envoy_filter_config = EnvoyHttpConfig {
        raw_addr: envoy_filter_config_ptr,
    };

    let filter_config =
        if let Some(config) = NEW_HTTP_FILTER_CONFIG_FUNCTION(envoy_filter_config, name, config) {
            config
        } else {
            return std::ptr::null();
        };

    // We wrap the Box<dyn HttpConfig> in another Box to ensuure that the object is not dropped after being into a raw pointer.
    // To be honest, this seems like a hack, and we should find a better way to handle this.
    // See https://users.rust-lang.org/t/sending-a-boxed-trait-over-ffi/21708 for the exact problem.
    let boxed_filter_config_ptr = Box::into_raw(Box::new(filter_config));
    boxed_filter_config_ptr as abi::envoy_dynamic_module_type_http_filter_config_in_module_ptr
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_destroy(
    http_filter: abi::envoy_dynamic_module_type_http_filter_config_in_module_ptr,
) {
    let factory = http_filter as *mut *mut dyn HttpConfig;
    (**factory).destroy();

    // Drop the Box<dyn HttpConfig> and the Box<*mut dyn HttpConfig>
    let _outer = Box::from_raw(factory);
    let _inner = Box::from_raw(*factory);
}
