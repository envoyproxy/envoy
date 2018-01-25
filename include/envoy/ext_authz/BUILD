licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ext_authz_interface",
    hdrs = ["ext_authz.h"],
    external_deps = ["envoy_api_auth_auth"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/tracing:http_tracer_interface",
    ],
)
