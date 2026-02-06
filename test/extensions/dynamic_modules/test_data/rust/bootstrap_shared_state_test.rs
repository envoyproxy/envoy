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

impl BootstrapExtension for MyBootstrapExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    // --- Basic publish/get round-trip ---
    let data = Box::new(12345u64);
    let data_ptr = Box::into_raw(data) as *const std::ffi::c_void;
    assert!(unsafe { publish_shared_state("tenant_snapshot", data_ptr) });

    let read_ptr = get_shared_state("tenant_snapshot").expect("shared state should exist");
    let read_data = unsafe { &*(read_ptr as *const u64) };
    assert_eq!(*read_data, 12345u64);

    assert!(get_shared_state("no_such_key").is_none());

    // --- Handle API: acquire and read ---
    let handle = SharedStateHandle::new("tenant_snapshot")
      .expect("handle should be acquired for existing key");
    let handle_ptr = handle.get().expect("handle should return data");
    let handle_data = unsafe { &*(handle_ptr as *const u64) };
    assert_eq!(*handle_data, 12345u64);

    // --- Handle sees atomic updates ---
    let data2 = Box::new(67890u64);
    let data2_ptr = Box::into_raw(data2) as *const std::ffi::c_void;
    assert!(unsafe { publish_shared_state("tenant_snapshot", data2_ptr) });

    let updated_ptr = handle.get().expect("handle should see updated data");
    let updated_data = unsafe { &*(updated_ptr as *const u64) };
    assert_eq!(*updated_data, 67890u64);

    // --- Handle survives clear and sees republished data ---
    assert!(unsafe { publish_shared_state("tenant_snapshot", std::ptr::null()) });
    assert!(handle.get().is_none());
    assert!(get_shared_state("tenant_snapshot").is_none());

    let data3 = Box::new(11111u64);
    let data3_ptr = Box::into_raw(data3) as *const std::ffi::c_void;
    assert!(unsafe { publish_shared_state("tenant_snapshot", data3_ptr) });

    let republished_ptr = handle.get().expect("handle should see republished data");
    let republished_data = unsafe { &*(republished_ptr as *const u64) };
    assert_eq!(*republished_data, 11111u64);

    // --- Handle for non-existent key returns None ---
    assert!(SharedStateHandle::new("nonexistent").is_none());

    // Clean up: clear registry entry, drop handle, then free heap allocations.
    assert!(unsafe { publish_shared_state("tenant_snapshot", std::ptr::null()) });
    drop(handle);
    let _ = unsafe { Box::from_raw(data_ptr as *mut u64) };
    let _ = unsafe { Box::from_raw(data2_ptr as *mut u64) };
    let _ = unsafe { Box::from_raw(data3_ptr as *mut u64) };

    envoy_log_info!("Bootstrap shared state test completed successfully!");
  }

  fn on_worker_thread_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {}
}
