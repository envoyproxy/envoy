# Proxy-Wasm tests

WebAssembly tests are built using [Proxy-Wasm C++ SDK] and [Proxy-Wasm Rust SDK],
as such, they bring their own set of dependencies.

## Cargo dependencies.

In order to update Cargo dependencies, please make sure that Rust and Cargo
are installed, and run this tool:

```
bash tools/update_cargo.sh
```

which will regenerate Bazel rules in `bazel/external/cargo/`.


[Proxy-Wasm C++ SDK]: https://github.com/proxy-wasm/proxy-wasm-cpp-sdk
[Proxy-Wasm Rust SDK]: https://github.com/proxy-wasm/proxy-wasm-rust-sdk
