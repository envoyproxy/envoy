use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  envoy_extension_config.signal_init_complete();
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

/// Static data used as shared state. Using static variables instead of heap allocations
/// avoids ASAN leak reports, since the shared data registry is process-wide and never cleared.
static INITIAL_VALUE: u64 = 42;
static UPDATED_VALUE: u64 = 84;

struct MyBootstrapExtension {}

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // Register a static data pointer via the shared data registry. The registry is
    // process-wide, so this may already exist from a prior parameterized test run
    // (e.g., IPv4 vs IPv6) in the same process.
    let _ = unsafe {
      register_shared_data(
        "test.shared.value",
        &INITIAL_VALUE as *const u64 as *const std::ffi::c_void,
      )
    };

    // Retrieve and verify the shared data.
    let retrieved_ptr = get_shared_data("test.shared.value").expect("shared data should be found");
    let retrieved: &u64 = unsafe { &*(retrieved_ptr as *const u64) };
    assert_eq!(*retrieved, 42);

    // Verify overwrite semantics: registering under the same key should succeed.
    let overwritten = unsafe {
      register_shared_data(
        "test.shared.value",
        &UPDATED_VALUE as *const u64 as *const std::ffi::c_void,
      )
    };
    assert!(overwritten, "overwrite should succeed");

    // Verify the overwritten value.
    let updated_ptr =
      get_shared_data("test.shared.value").expect("shared data should still be found");
    let updated: &u64 = unsafe { &*(updated_ptr as *const u64) };
    assert_eq!(*updated, 84);

    // Verify non-existent key returns None.
    assert!(
      get_shared_data("no_such_data").is_none(),
      "non-existent key should return None"
    );

    envoy_log_info!("Bootstrap shared data registry test completed successfully!");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}
}
