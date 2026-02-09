use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  _envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  Some(Box::new(MyBootstrapExtensionConfig {}))
}

struct MyBootstrapExtensionConfig {}

impl BootstrapExtensionConfig for MyBootstrapExtensionConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(MyBootstrapExtension {})
  }
}

struct MyBootstrapExtension {}

// Functions that will be registered and resolved via the function registry.
extern "C" fn get_answer() -> u64 {
  42
}

extern "C" fn double_it(x: u64) -> u64 {
  x * 2
}

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Register functions. The registry is process-wide, so these may already exist from a prior
    // parameterized test run (e.g., IPv4 vs IPv6) in the same process.
    let _ = unsafe { register_function("get_answer", get_answer as *const std::ffi::c_void) };
    let _ = unsafe { register_function("double_it", double_it as *const std::ffi::c_void) };

    // Resolve and call the registered functions.
    let fn_ptr = get_function("get_answer").expect("registered function should be found");
    let resolved: extern "C" fn() -> u64 = unsafe { std::mem::transmute(fn_ptr) };
    assert_eq!(resolved(), 42);

    let fn_ptr2 = get_function("double_it").expect("second function should be found");
    let resolved2: extern "C" fn(u64) -> u64 = unsafe { std::mem::transmute(fn_ptr2) };
    assert_eq!(resolved2(21), 42);

    // Non-existent key returns None.
    assert!(get_function("no_such_fn").is_none());

    envoy_log_info!("Bootstrap function registry test completed successfully!");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}
}
