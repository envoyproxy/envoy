load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "test_binary_size",
    stamped = "True",
    deps = [":test_binary_size_lib"],
)
envoy_cc_library(
    name = "test_binary_size_lib",
    srcs = ["test_binary_size.cc"],
    external_deps = [
        "abseil_symbolize",
    ],
    deps = [
        "//source/exe:envoy_main_common_lib",
        "//source/exe:platform_impl_lib",
    ],
)

