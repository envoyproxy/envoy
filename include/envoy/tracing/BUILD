licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "http_tracer_interface",
    hdrs = ["http_tracer.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/http:header_map_interface",
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
