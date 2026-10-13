# Envoy Dynamic Modules Rust SDK

This directory contains the Rust SDK for the Dynamic Modules feature. This directory is organized in the way that it can be used as a standalone Rust crate. The SDK is basically the high-level abstraction layer for the Dynamic Modules ABI defined in the [abi.h](../../abi.h).

Note that this crate references the local ABI header files, so this is intended to be used as
```
[dependencies]
envoy-proxy-dynamic-modules-rust-sdk = { git = "https://github.com/envoyproxy/envoy", tag = "vX.Y.Z" }
# Or, you can use a specific commit instead of a tag:
# envoy-proxy-dynamic-modules-rust-sdk = {  git = "https://github.com/envoyproxy/envoy", rev = "abcdef" }
```

instead of `envoy-proxy-dynamic-modules-rust-sdk = "X.Y.Z"`.

See [Examples](https://github.com/envoyproxy/dynamic-modules-examples) for more details on how to use this SDK.
