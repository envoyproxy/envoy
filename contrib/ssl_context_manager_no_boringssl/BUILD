load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "ssl_context_manager_lib",
    srcs = ["ssl_context_manager.cc"],
    deps = [
        "//envoy/registry",
        "//envoy/ssl:context_manager_interface",
    ],
)
