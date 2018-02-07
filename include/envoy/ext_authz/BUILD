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
    deps = [
        "//include/envoy/tracing:http_tracer_interface",
        "@envoy_api//envoy/service/auth/v2:external_auth_cc",
    ],
)
