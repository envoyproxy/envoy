use envoy_proxy_dynamic_modules_rust_sdk::declare_program_init;
use std::sync::{LazyLock, Mutex};

declare_program_init!(init);

fn init() -> bool {
    true
}

static SOME_VARIABLE: LazyLock<Mutex<i32>> = LazyLock::new(|| Mutex::new(0));

#[no_mangle]
pub extern "C" fn getSomeVariable() -> i32 {
    let mut v = SOME_VARIABLE.lock().unwrap();
    *v += 1;
    *v
}
