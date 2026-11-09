load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_rust//rust:defs.bzl", "rust_static_library")

licenses(["notice"])  # Apache 2

# The C API is built from this source archive, against the `wasmtime` crate that `@wasmtime_crates`
# (bazel/dependency_imports.bzl) resolves at the same version.

# These must match the `wasmtime` crate features requested in `@wasmtime_crates`: the C API only
# compiles the parts that the crate provides, and the headers must describe the same set.
_C_API_FEATURES = [
    "cranelift",
    "gc",
    "gc-drc",
]

# Upstream generates this header with CMake's `configure_file`.
genrule(
    name = "wasmtime_conf_h",
    srcs = ["crates/c-api/include/wasmtime/conf.h.in"],
    outs = ["crates/c-api/include/wasmtime/conf.h"],
    cmd = """
        set --
        for feature in %s; do
            set -- "$$@" -e "s/^#cmakedefine WASMTIME_FEATURE_$${feature}$$/#define WASMTIME_FEATURE_$${feature}/"
        done
        sed "$$@" -e 's/^#cmakedefine \\(WASMTIME_FEATURE_[A-Z0-9_]*\\)$$/\\/* #undef \\1 *\\//' $(<) > $@
    """ % " ".join([feature.upper().replace("-", "_") for feature in _C_API_FEATURES]),
)

cc_library(
    name = "wasmtime_lib",
    hdrs = glob([
        "crates/c-api/include/**/*.h",
        "crates/c-api/include/**/*.hh",
    ]) + [":wasmtime_conf_h"],
    includes = ["crates/c-api/include"],
    visibility = ["//visibility:public"],
    deps = [":wasmtime_c_api"],
)

rust_static_library(
    name = "wasmtime_c_api",
    srcs = glob(["crates/c-api/src/**/*.rs"]),
    aliases = {
        "@wasmtime_crates//:wasmtime-internal-c-api-macros": "wasmtime_c_api_macros",
    },
    crate_features = _C_API_FEATURES,
    crate_root = "crates/c-api/src/lib.rs",
    edition = "2024",
    proc_macro_deps = [
        "@wasmtime_crates//:wasmtime-internal-c-api-macros",
    ],
    deps = [
        "@wasmtime_crates//:log",
        "@wasmtime_crates//:tracing",
        "@wasmtime_crates//:wasmtime",
    ],
)
