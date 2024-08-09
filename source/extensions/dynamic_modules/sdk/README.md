## Envoy Dynamic Module SDKs

This directory contains the SDKs for building dynamic modules for Envoy. At the alpha stage, only the
Go and Rust SDKs will be offered. Each SDK implements the same ABI defined in the [abi.h](../abi.h),
and passes the same set of integration tests.

Each SDK is desinged to produce an shared object that can be used at multiple Envoy extension points. See
the comments in the top of [abi.h](../abi.h) for more information.
