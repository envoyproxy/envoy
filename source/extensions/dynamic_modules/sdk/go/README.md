# Envoy Dynamic Modules Rust SDK

This directory contains the Go SDK for the Dynamic Modules feature. The SDK passes the same set of tests and is guaranteed to provide the same functionality as the other SDKs. The ABI header file is hard-copied into this directory to make it possible to use the SDK as a standalone Go module. The header file is copied from the [source/extensions/dynamic_modules/abi.h](../../abi.h).
