load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "fluentd_base_lib",
    srcs = ["fluentd_base.cc"],
    hdrs = ["fluentd_base.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "@msgpack-cxx//:msgpack",
    ],
)
