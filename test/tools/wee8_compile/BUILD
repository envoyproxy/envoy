load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "wee8_compile_tool",
    stamp = 0,
    deps = [":wee8_compile_lib"],
)

envoy_cc_library(
    name = "wee8_compile_lib",
    srcs = ["wee8_compile.cc"],
    copts = [
        "-Wno-comments",
        "-Wno-non-virtual-dtor",
        "-Wno-unused-parameter",
    ],
    deps = [
        # The toolshed prebuilt wee8 excludes abseil from libwee8.a by design
        # (consumers provide their own to avoid ODR/ABI clashes), so this
        # minimal v8_lib consumer must supply the absl symbols wee8 references.
        # The from-source @v8//:wee8 propagates these transitively; the prebuilt
        # does not.
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/synchronization",
        "@proxy-wasm-cpp-host//:base_lib",
        "@proxy-wasm-cpp-host//:v8_lib",
    ],
)
