// TODO(PiotrSikora): build test data with Bazel rules.
// See: https://github.com/envoyproxy/envoy/issues/9733
//
// Build using:
// $ rustc -C lto -C opt-level=3 -C panic=abort -C link-arg=-S -C link-arg=-zstack-size=32768 --crate-type cdylib --target wasm32-unknown-unknown test_rust.rs
// $ ../../../../../bazel-bin/test/tools/wee8_compile/wee8_compile_tool test_rust.wasm test_rust.wasm

// Import functions exported from the host environment.
extern "C" {
    fn pong(value: u32);
    fn random() -> u32;
}

#[no_mangle]
extern "C" fn ping(value: u32) {
    unsafe { pong(value) }
}

#[no_mangle]
extern "C" fn lucky(number: u32) -> bool {
    unsafe { number == random() }
}

#[no_mangle]
extern "C" fn sum(a: u32, b: u32, c: u32) -> u32 {
    a + b + c
}

#[no_mangle]
extern "C" fn div(a: u32, b: u32) -> u32 {
    a / b
}

#[no_mangle]
extern "C" fn abort() {
    panic!("abort")
}
