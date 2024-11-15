use envoy_proxy_dynamic_modules_rust_sdk::declare_program_init;
use std::sync::atomic::{AtomicI32, Ordering};

declare_program_init!(init);

fn init() -> bool {
    true
}

static SOME_VARIABLE: AtomicI32 = AtomicI32::new(1);

#[no_mangle]
pub extern "C" fn getSomeVariable() -> i32 {
    SOME_VARIABLE.fetch_add(1, Ordering::SeqCst)
}
