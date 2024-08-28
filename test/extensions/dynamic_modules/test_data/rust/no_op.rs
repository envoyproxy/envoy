use envoy_proxy_dynamic_modules_rust_sdk::declare_program_init;

declare_program_init!(init);

fn init() -> bool {
    true
}

#[no_mangle]
pub extern "C" fn getSomeVariable() -> i32 {
    static mut SOME_VARIABLE: i32 = 0;

    unsafe {
        SOME_VARIABLE += 1;
        SOME_VARIABLE
    }
}
