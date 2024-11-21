#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

use std::sync::OnceLock;

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
///   _envoy_filter_config: EnvoyHttpFilterConfig,
///   _name: &str,
///   _config: &str,
/// ) -> Option<Box<dyn HttpFilterConfig>> {
///   Some(Box::new(MyHttpFilterConfig {}))
/// }
///
/// struct MyHttpFilterConfig {}
///
/// impl HttpFilterConfig for MyHttpFilterConfig {}
/// ```
#[macro_export]
macro_rules! declare_init_functions {
    ($f:ident,$new_http_filter_config_fn:expr) => {
        #[no_mangle]
        pub extern "C" fn envoy_dynamic_module_on_program_init() -> *const ::std::os::raw::c_char {
            envoy_proxy_dynamic_modules_rust_sdk::NEW_HTTP_FILTER_CONFIG_FUNCTION
                .get_or_init(|| $new_http_filter_config_fn);
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
/// This is called when a new HTTP filter configuration is created, and it must return a new instance of the [`HttpFilterConfig`] object.
/// Returning `None` will cause the HTTP filter configuration to be rejected.
//
// TODO(@mathetake): I guess there would be a way to avoid the use of dyn in the first place.
// E.g. one idea is to accept all concrete type parameters for HttpFilterConfig and HttpFilter traits in declare_init_functions!,
// and generate the match statement based on that.
pub type NewHttpFilterConfigFunction = fn(
    envoy_filter_config: EnvoyHttpFilterConfig,
    name: &str,
    config: &str,
) -> Option<Box<dyn HttpFilterConfig>>;

/// The global init function for HTTP filter configurations. This is set via the `declare_init_functions` macro,
/// and is not intended to be set directly.
pub static NEW_HTTP_FILTER_CONFIG_FUNCTION: OnceLock<NewHttpFilterConfigFunction> = OnceLock::new();

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpFilterConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// dropped when the corresponding Envoy Http filter config is destroyed. Therefore, the imlementation
/// is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
pub trait HttpFilterConfig {
    /// This is called when a HTTP filter chain is created for a new stream.
    fn new_http_filter(&self) -> Box<dyn HttpFilter> {
        unimplemented!() // TODO.
    }
}

/// The trait that represents an Envoy Http filter for each stream.
pub trait HttpFilter {} // TODO.

/// An opaque object that represents the underlying Envoy Http filter config. This has one to one
/// mapping with the Envoy Http filter config object as well as [`HttpFilterConfig`] object.
///
/// This is a shallow wrapper around the raw pointer to the Envoy HTTP filter config object, and it
/// can be copied and used up until the corresponding [`HttpFilterConfig`] is dropped.
//
// TODO(@mathetake): make this only avaialble for non-test code, and provide a mock for testing so that users
// can write unit tests for their HttpFilterConfig implementations.
#[derive(Debug, Clone, Copy)]
pub struct EnvoyHttpFilterConfig {
    raw_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_new(
    envoy_filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
    name_ptr: *const u8,
    name_size: usize,
    config_ptr: *const u8,
    config_size: usize,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
    // This assumes that the name and config are valid UTF-8 strings. Should we relax? At the moment, both are String at protobuf level.
    let name =
        std::str::from_utf8(std::slice::from_raw_parts(name_ptr, name_size)).unwrap_or_default();
    let config = std::str::from_utf8(std::slice::from_raw_parts(config_ptr, config_size))
        .unwrap_or_default();

    let envoy_filter_config = EnvoyHttpFilterConfig {
        raw_ptr: envoy_filter_config_ptr,
    };

    envoy_dynamic_module_on_http_filter_config_new_impl(
        envoy_filter_config,
        name,
        config,
        NEW_HTTP_FILTER_CONFIG_FUNCTION
            .get()
            .expect("NEW_HTTP_FILTER_CONFIG_FUNCTION must be set"),
    )
}

fn envoy_dynamic_module_on_http_filter_config_new_impl(
    envoy_filter_config: EnvoyHttpFilterConfig,
    name: &str,
    config: &str,
    new_fn: &NewHttpFilterConfigFunction,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
    if let Some(config) = new_fn(envoy_filter_config, name, config) {
        let boxed_filter_config_ptr = Box::into_raw(Box::new(config));
        boxed_filter_config_ptr as abi::envoy_dynamic_module_type_http_filter_config_module_ptr
    } else {
        std::ptr::null()
    }
}

#[no_mangle]
unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_destroy(
    config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
) {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig;

    // Drop the Box<*mut dyn HttpFilterConfig>, and then the Box<dyn HttpFilterConfig>, which also drops the underlying object.
    let _outer = Box::from_raw(config);
    let _inner = Box::from_raw(*config);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_envoy_dynamic_module_on_http_filter_config_new_impl() {
        struct TestHttpFilterConfig;
        impl HttpFilterConfig for TestHttpFilterConfig {}

        let envoy_filter_config = EnvoyHttpFilterConfig {
            raw_ptr: std::ptr::null_mut(),
        };
        let mut new_fn: NewHttpFilterConfigFunction =
            |_, _, _| Some(Box::new(TestHttpFilterConfig));
        let result = envoy_dynamic_module_on_http_filter_config_new_impl(
            envoy_filter_config,
            "test_name",
            "test_config",
            &new_fn,
        );
        assert!(!result.is_null());

        unsafe {
            envoy_dynamic_module_on_http_filter_config_destroy(result);
        }

        // None should result in null pointer.
        new_fn = |_, _, _| None;
        let result = envoy_dynamic_module_on_http_filter_config_new_impl(
            envoy_filter_config,
            "test_name",
            "test_config",
            &new_fn,
        );
        assert!(result.is_null());
    }

    #[test]
    fn test_envoy_dynamic_module_on_http_filter_config_destroy() {
        use std::sync::atomic::AtomicBool;

        // This test is mainly to check if the drop is called correctly after wrapping/unwrapping the Box.
        static DROPPED: AtomicBool = AtomicBool::new(false);
        struct TestHttpFilterConfig;
        impl HttpFilterConfig for TestHttpFilterConfig {}
        impl Drop for TestHttpFilterConfig {
            fn drop(&mut self) {
                DROPPED.store(true, std::sync::atomic::Ordering::SeqCst);
            }
        }

        // This is a sort of round-trip to ensure the same control flow as the actual usage.
        let new_fn: NewHttpFilterConfigFunction = |_, _, _| Some(Box::new(TestHttpFilterConfig));
        let config_ptr = envoy_dynamic_module_on_http_filter_config_new_impl(
            EnvoyHttpFilterConfig {
                raw_ptr: std::ptr::null_mut(),
            },
            "test_name",
            "test_config",
            &new_fn,
        );

        unsafe {
            envoy_dynamic_module_on_http_filter_config_destroy(config_ptr);
        }
        // Now that the drop is called, DROPPED must be set to true.
        assert!(DROPPED.load(std::sync::atomic::Ordering::SeqCst));
    }
}
