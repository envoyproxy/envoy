load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "opentracing_driver_lib",
    srcs = [
        "opentracing_driver_impl.cc",
    ],
    hdrs = [
        "opentracing_driver_impl.h",
    ],
    external_deps = ["opentracing"],
    deps = [
        "//envoy/runtime:runtime_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/tracing:trace_driver_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:empty_string",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/tracing:common_values_lib",
        "//source/common/tracing:null_span_lib",
    ],
)
