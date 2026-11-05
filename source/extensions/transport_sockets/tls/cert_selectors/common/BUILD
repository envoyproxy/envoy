load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "async_context_lib",
    srcs = ["async_context.cc"],
    hdrs = ["async_context.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:tls_certificate_config_interface",
        "//envoy/stats:stats_interface",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_lib",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
    ],
)
