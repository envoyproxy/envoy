// Build using:
// $ rustc -O -C link-arg=-S -C link-arg=-zstack-size=65536 --crate-type=cdylib --target=wasm32-unknown-unknown test_rust.rs

// Import "ping" function from the host environment.
extern "C" {
    fn ping();
}

#[no_mangle]
extern "C" fn _start() {
    unsafe { ping() }
}

#[no_mangle]
extern "C" fn sum(a: u32, b: u32, c: u32) -> u32 {
    a + b + c
}
