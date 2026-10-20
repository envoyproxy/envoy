Fixed linking the wasmtime engine alongside built-in Rust dynamic modules. Built-in Rust modules
now share the toolchain's Rust runtime instead of embedding duplicate copies, and can coexist
with other built-in Rust modules in the same binary.
An intentionally empty shared staticlib supplies the allocator shim so binaries containing
only rlib-based Rust modules also link successfully.
