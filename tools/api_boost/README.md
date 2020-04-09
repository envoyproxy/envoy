# Envoy API upgrades

This directory contains tooling to support the [Envoy API versioning
guidelines](api/API_VERSIONING.md). Envoy internally tracks the latest API
version for any given package. Since each package may have a different API
version, and we have have > 15k of API protos, we require machine assistance to
scale the upgrade process.

We refer to the process of upgrading Envoy to the latest version of the API as
*API boosting*. This is a manual process, where a developer wanting to bump
major version at the API clock invokes:

```console
/tools/api_boost/api_boost.py --build_api_booster --generate_compilation_database
```

followed by `fix_format`. The full process is still WiP, but we expect that
there will be some manual fixup required of test cases (e.g. YAML fragments) as
well.

You will need to configure `LLVM_CONFIG` as per the [Clang Libtooling setup
guide](tools/clang_tools/README.md).

## Status

The API boosting tooling is still WiP. It is slated to land in the v3 release
(EOY 2019), at which point it should be considered ready for general consumption
by experienced developers who work on Envoy APIs.
