load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "http_tracer_interface",
    hdrs = ["http_tracer.h"],
    deps = [
        ":trace_driver_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "http_tracer_manager_interface",
    hdrs = ["http_tracer_manager.h"],
    deps = [
        ":http_tracer_interface",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "trace_reason_interface",
    hdrs = ["trace_reason.h"],
)

envoy_cc_library(
    name = "trace_driver_interface",
    hdrs = ["trace_driver.h"],
    deps = [
        ":trace_context_interface",
        ":trace_reason_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "trace_context_interface",
    hdrs = ["trace_context.h"],
)
