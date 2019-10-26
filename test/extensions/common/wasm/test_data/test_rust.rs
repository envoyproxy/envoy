// Build using:
// $ rustc -O -C link-arg=-S -C link-arg=-zstack-size=65536 --crate-type=cdylib --target=wasm32-unknown-unknown test_rust.rs

// Import "pong" function from the host environment.
extern "C" {
    fn pong(value: u32);
}

#[no_mangle]
extern "C" fn ping(value: u32) {
    unsafe { pong(value) }
}

#[no_mangle]
extern "C" fn sum(a: u32, b: u32, c: u32) -> u32 {
    a + b + c
}
